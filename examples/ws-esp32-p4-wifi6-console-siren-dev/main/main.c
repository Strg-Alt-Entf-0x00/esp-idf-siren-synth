#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "es8311_codec.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_console.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_timer.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "siren_synth.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "console_siren_dev";

enum {
    BOARD_I2C_SDA_GPIO = 7,
    BOARD_I2C_SCL_GPIO = 8,
    BOARD_I2S_DOUT_GPIO = 9,
    BOARD_I2S_WS_GPIO = 10,
    BOARD_I2S_BCLK_GPIO = 12,
    BOARD_I2S_MCLK_GPIO = 13,
    BOARD_PA_ENABLE_GPIO = 53,
    AUDIO_SAMPLE_RATE = 48000,
    AUDIO_CHUNK_SAMPLES = 256,
    BENCHMARK_BLOCK_COUNT =
        (AUDIO_SAMPLE_RATE + AUDIO_CHUNK_SAMPLES - 1) / AUDIO_CHUNK_SAMPLES,
};

typedef enum {
    AUDIO_STATE_INITIALIZING,
    AUDIO_STATE_READY,
    AUDIO_STATE_FAILED,
} audio_state_t;

typedef struct {
    siren_synth_config_t config;
    uint32_t duration_ms;
    bool loop;
} playback_request_t;

static i2c_master_bus_handle_t s_i2c_bus;
static i2s_chan_handle_t s_i2s_tx;
static esp_codec_dev_handle_t s_codec;
static audio_state_t s_audio_state = AUDIO_STATE_INITIALIZING;
static int s_volume = 50;
static siren_synth_config_t s_synth_config;
static siren_synth_config_t s_transaction_config;
static int s_transaction_volume;
static bool s_persistence_ready;
static bool s_config_transaction_active;
static siren_synth_handle_t s_synth;
static TaskHandle_t s_playback_task;
static volatile bool s_playback_active;
static volatile bool s_playback_loop;
static volatile uint32_t s_playback_duration_ms;
static volatile uint32_t s_playback_safety_timeout_ms;
typedef enum {
    PLAYBACK_STOP_NONE,
    PLAYBACK_STOP_BRAKE,
    PLAYBACK_STOP_WIND_DOWN,
} playback_stop_mode_t;
static volatile playback_stop_mode_t s_playback_stop_mode;
static volatile uint32_t s_playback_wind_down_duration_ms;
static uint32_t s_benchmark_block_us[BENCHMARK_BLOCK_COUNT];

enum {
    SAVED_CONFIG_VERSION = 3,
};

typedef struct {
    uint32_t version;
    uint32_t duration_ms;
    float amplitude;
    float sweep_start_hz;
    float sweep_end_hz;
    float rotor_hz;
    float modulation_depth;
    float wobble_hz;
    float attack_ms;
    float release_ms;
    float room_size;
    float room_mix;
    float radio_mix;
    float peak_rpm;
    float rotor_ports_high;
    float rotor_ports_low;
    float vanes_high;
    float vanes_low;
    float undertone_ports;
    float overtone_ports;
    float motor_rise_s;
    float motor_fall_s;
    float mix_rotor_high;
    float mix_rotor_low;
    float mix_vanes_high;
    float mix_vanes_low;
    float mix_undertone;
    float mix_overtone;
    int32_t volume;
    uint8_t pattern;
    uint8_t room;
    uint8_t source;
    uint8_t chopper_config;
    uint8_t radio_enabled;
    uint8_t thermal_enabled;
    uint8_t jitter_enabled;
    uint8_t reserved;
    float distance_mix;
} saved_config_t;

_Static_assert(offsetof(saved_config_t, reserved) == 123U,
               "Saved configuration schema v3 reserved-byte offset changed");
_Static_assert(offsetof(saved_config_t, distance_mix) == 124U,
               "Saved configuration schema v3 field layout changed");
_Static_assert(sizeof(saved_config_t) == 128U,
               "Saved configuration schema v3 size changed");

static void saved_config_from_runtime(saved_config_t *saved,
                                      const siren_synth_config_t *config,
                                      int volume)
{
    *saved = (saved_config_t) {
        .version = SAVED_CONFIG_VERSION,
        .duration_ms = config->duration_ms,
        .amplitude = config->amplitude,
        .sweep_start_hz = config->sweep_start_hz,
        .sweep_end_hz = config->sweep_end_hz,
        .rotor_hz = config->rotor_hz,
        .modulation_depth = config->modulation_depth,
        .wobble_hz = config->wobble_hz,
        .attack_ms = config->attack_ms,
        .release_ms = config->release_ms,
        .room_size = config->room_size,
        .room_mix = config->room_mix,
        .radio_mix = config->radio_mix,
        .distance_mix = config->distance_mix,
        .peak_rpm = config->peak_rpm,
        .rotor_ports_high = config->rotor_ports_high,
        .rotor_ports_low = config->rotor_ports_low,
        .vanes_high = config->vanes_high,
        .vanes_low = config->vanes_low,
        .undertone_ports = config->undertone_ports,
        .overtone_ports = config->overtone_ports,
        .motor_rise_s = config->motor_rise_s,
        .motor_fall_s = config->motor_fall_s,
        .mix_rotor_high = config->mix_rotor_high,
        .mix_rotor_low = config->mix_rotor_low,
        .mix_vanes_high = config->mix_vanes_high,
        .mix_vanes_low = config->mix_vanes_low,
        .mix_undertone = config->mix_undertone,
        .mix_overtone = config->mix_overtone,
        .volume = volume,
        .pattern = (uint8_t)config->pattern,
        .room = (uint8_t)config->room,
        .source = (uint8_t)config->source,
        .chopper_config = (uint8_t)config->chopper_config,
        .radio_enabled = config->radio_enabled ? 1U : 0U,
        .thermal_enabled = config->thermal_enabled ? 1U : 0U,
        .jitter_enabled = config->jitter_enabled ? 1U : 0U,
        .reserved = 0U,
    };
}

static bool saved_config_is_valid(const saved_config_t *saved)
{
    return saved->version == SAVED_CONFIG_VERSION &&
           saved->duration_ms >= 100U && saved->duration_ms <= 60000U &&
           isfinite(saved->amplitude) && saved->amplitude > 0.0f && saved->amplitude <= 1.0f &&
           isfinite(saved->sweep_start_hz) && saved->sweep_start_hz >= 20.0f &&
           saved->sweep_start_hz < saved->sweep_end_hz &&
           isfinite(saved->sweep_end_hz) && saved->sweep_end_hz <= 900.0f &&
           isfinite(saved->rotor_hz) && saved->rotor_hz > 0.0f && saved->rotor_hz <= 30.0f &&
           isfinite(saved->modulation_depth) && saved->modulation_depth >= 0.0f &&
           saved->modulation_depth <= 1.0f &&
           isfinite(saved->wobble_hz) && saved->wobble_hz > 0.0f && saved->wobble_hz <= 30.0f &&
           isfinite(saved->attack_ms) && saved->attack_ms >= 1.0f && saved->attack_ms <= 5000.0f &&
           isfinite(saved->release_ms) && saved->release_ms >= 1.0f && saved->release_ms <= 5000.0f &&
           isfinite(saved->room_size) && saved->room_size >= 0.0f && saved->room_size <= 1.0f &&
           isfinite(saved->room_mix) && saved->room_mix >= 0.0f && saved->room_mix <= 1.0f &&
           isfinite(saved->radio_mix) && saved->radio_mix >= 0.0f && saved->radio_mix <= 1.0f &&
           isfinite(saved->distance_mix) && saved->distance_mix >= 0.0f &&
           saved->distance_mix <= 1.0f &&
           isfinite(saved->peak_rpm) && saved->peak_rpm >= 100.0f && saved->peak_rpm <= 30000.0f &&
           isfinite(saved->rotor_ports_high) && saved->rotor_ports_high >= 0.0f &&
           saved->rotor_ports_high <= 64.0f &&
           isfinite(saved->rotor_ports_low) && saved->rotor_ports_low >= 0.0f &&
           saved->rotor_ports_low <= 64.0f &&
           isfinite(saved->vanes_high) && saved->vanes_high >= 0.0f &&
           saved->vanes_high <= 64.0f &&
           isfinite(saved->vanes_low) && saved->vanes_low >= 0.0f &&
           saved->vanes_low <= 64.0f &&
           isfinite(saved->undertone_ports) && saved->undertone_ports >= 0.0f &&
           saved->undertone_ports <= 64.0f &&
           isfinite(saved->overtone_ports) && saved->overtone_ports >= 0.0f &&
           saved->overtone_ports <= 64.0f &&
           isfinite(saved->motor_rise_s) && saved->motor_rise_s >= 0.05f &&
           saved->motor_rise_s <= 30.0f &&
           isfinite(saved->motor_fall_s) && saved->motor_fall_s >= 0.05f &&
           saved->motor_fall_s <= 60.0f &&
           isfinite(saved->mix_rotor_high) && saved->mix_rotor_high >= 0.0f &&
           saved->mix_rotor_high <= 1.0f &&
           isfinite(saved->mix_rotor_low) && saved->mix_rotor_low >= 0.0f &&
           saved->mix_rotor_low <= 1.0f &&
           isfinite(saved->mix_vanes_high) && saved->mix_vanes_high >= 0.0f &&
           saved->mix_vanes_high <= 1.0f &&
           isfinite(saved->mix_vanes_low) && saved->mix_vanes_low >= 0.0f &&
           saved->mix_vanes_low <= 1.0f &&
           isfinite(saved->mix_undertone) && saved->mix_undertone >= 0.0f &&
           saved->mix_undertone <= 1.0f &&
           isfinite(saved->mix_overtone) && saved->mix_overtone >= 0.0f &&
           saved->mix_overtone <= 1.0f &&
           saved->volume >= 0 && saved->volume <= 100 &&
           saved->pattern < SIREN_PATTERN_COUNT && saved->room < SIREN_ROOM_COUNT &&
           saved->source < SIREN_SOURCE_COUNT &&
           saved->chopper_config < SIREN_CHOPPER_CONFIG_COUNT &&
           saved->radio_enabled <= 1U && saved->thermal_enabled <= 1U &&
           saved->jitter_enabled <= 1U;
}

static esp_err_t save_runtime_config(const siren_synth_config_t *config, int volume)
{
    if (!s_persistence_ready) {
        return ESP_ERR_INVALID_STATE;
    }

    saved_config_t saved;
    saved_config_from_runtime(&saved, config, volume);
    if (!saved_config_is_valid(&saved)) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t nvs;
    esp_err_t result = nvs_open("siren_dev", NVS_READWRITE, &nvs);
    if (result != ESP_OK) {
        return result;
    }
    result = nvs_set_blob(nvs, "active_cfg", &saved, sizeof(saved));
    if (result == ESP_OK) {
        result = nvs_commit(nvs);
    }
    nvs_close(nvs);
    return result;
}

static esp_err_t load_runtime_config(siren_synth_config_t *config, int *volume)
{
    if (!s_persistence_ready) {
        return ESP_ERR_INVALID_STATE;
    }

    nvs_handle_t nvs;
    esp_err_t result = nvs_open("siren_dev", NVS_READONLY, &nvs);
    if (result != ESP_OK) {
        return result;
    }
    size_t stored_size = 0U;
    result = nvs_get_blob(nvs, "active_cfg", NULL, &stored_size);
    saved_config_t saved = {0};
    if (result == ESP_OK && stored_size == sizeof(saved)) {
        size_t saved_size = sizeof(saved);
        result = nvs_get_blob(nvs, "active_cfg", &saved, &saved_size);
        if (result == ESP_OK && saved_size != sizeof(saved)) {
            result = ESP_ERR_INVALID_STATE;
        }
    } else if (result == ESP_OK) {
        result = ESP_ERR_INVALID_STATE;
    }
    nvs_close(nvs);
    if (result != ESP_OK) {
        return result;
    }
    if (!saved_config_is_valid(&saved)) {
        return ESP_ERR_INVALID_STATE;
    }

    siren_synth_config_defaults(config, AUDIO_SAMPLE_RATE);
    config->duration_ms = saved.duration_ms;
    config->amplitude = saved.amplitude;
    config->sweep_start_hz = saved.sweep_start_hz;
    config->sweep_end_hz = saved.sweep_end_hz;
    config->rotor_hz = saved.rotor_hz;
    config->modulation_depth = saved.modulation_depth;
    config->wobble_hz = saved.wobble_hz;
    config->attack_ms = saved.attack_ms;
    config->release_ms = saved.release_ms;
    config->room_size = saved.room_size;
    config->room_mix = saved.room_mix;
    config->radio_mix = saved.radio_mix;
    config->distance_mix = saved.distance_mix;
    config->peak_rpm = saved.peak_rpm;
    config->rotor_ports_high = saved.rotor_ports_high;
    config->rotor_ports_low = saved.rotor_ports_low;
    config->vanes_high = saved.vanes_high;
    config->vanes_low = saved.vanes_low;
    config->undertone_ports = saved.undertone_ports;
    config->overtone_ports = saved.overtone_ports;
    config->motor_rise_s = saved.motor_rise_s;
    config->motor_fall_s = saved.motor_fall_s;
    config->mix_rotor_high = saved.mix_rotor_high;
    config->mix_rotor_low = saved.mix_rotor_low;
    config->mix_vanes_high = saved.mix_vanes_high;
    config->mix_vanes_low = saved.mix_vanes_low;
    config->mix_undertone = saved.mix_undertone;
    config->mix_overtone = saved.mix_overtone;
    config->pattern = (siren_pattern_t)saved.pattern;
    config->room = (siren_room_t)saved.room;
    config->source = (siren_source_t)saved.source;
    config->chopper_config = (siren_chopper_config_t)saved.chopper_config;
    config->radio_enabled = saved.radio_enabled != 0U;
    config->thermal_enabled = saved.thermal_enabled != 0U;
    config->jitter_enabled = saved.jitter_enabled != 0U;
    *volume = saved.volume;
    return ESP_OK;
}

static int persist_siren_config_change(const siren_synth_config_t *previous_config)
{
    if (s_config_transaction_active) {
        return 0;
    }

    const esp_err_t result = save_runtime_config(&s_synth_config, s_volume);
    if (result == ESP_OK) {
        return 0;
    }

    s_synth_config = *previous_config;
    printf("Could not save setting to NVS; change was reverted: %s\n",
           esp_err_to_name(result));
    return 1;
}

static int begin_config_transaction(void)
{
    if (!s_persistence_ready) {
        printf("Cannot start a settings transaction while NVS is unavailable.\n");
        return 1;
    }
    if (s_config_transaction_active) {
        printf("A settings transaction is already active.\n");
        return 1;
    }

    s_transaction_config = s_synth_config;
    s_transaction_volume = s_volume;
    s_config_transaction_active = true;
    printf("Settings transaction started; changes are staged until commit.\n");
    return 0;
}

static int abort_config_transaction(void)
{
    if (!s_config_transaction_active) {
        printf("No settings transaction is active.\n");
        return 1;
    }

    s_synth_config = s_transaction_config;
    s_volume = s_transaction_volume;
    s_config_transaction_active = false;
    printf("Settings transaction aborted; staged changes were discarded.\n");
    return 0;
}

static int commit_config_transaction(void)
{
    if (!s_config_transaction_active) {
        printf("No settings transaction is active.\n");
        return 1;
    }

    if (s_audio_state == AUDIO_STATE_READY &&
        esp_codec_dev_set_out_vol(s_codec, s_volume) != ESP_CODEC_DEV_OK) {
        s_synth_config = s_transaction_config;
        s_volume = s_transaction_volume;
        s_config_transaction_active = false;
        printf("Could not apply staged speaker volume; transaction was rolled back.\n");
        return 1;
    }

    const esp_err_t result = save_runtime_config(&s_synth_config, s_volume);
    if (result != ESP_OK) {
        if (s_audio_state == AUDIO_STATE_READY &&
            esp_codec_dev_set_out_vol(s_codec, s_transaction_volume) != ESP_CODEC_DEV_OK) {
            printf("Could not restore the previous codec volume after NVS save failed.\n");
        }
        s_synth_config = s_transaction_config;
        s_volume = s_transaction_volume;
        s_config_transaction_active = false;
        printf("Could not commit settings to NVS; transaction was rolled back: %s\n",
               esp_err_to_name(result));
        return 1;
    }

    s_config_transaction_active = false;
    printf("Settings transaction committed and saved to NVS.\n");
    return 0;
}

static esp_err_t audio_output_init(void)
{
    const gpio_config_t pa_config = {
        .pin_bit_mask = 1ULL << BOARD_PA_ENABLE_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&pa_config), TAG, "PA enable GPIO init failed");
    ESP_RETURN_ON_ERROR(gpio_set_level(BOARD_PA_ENABLE_GPIO, 0), TAG, "Could not disable speaker amplifier");

    const i2c_master_bus_config_t i2c_config = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = I2C_NUM_0,
        .scl_io_num = BOARD_I2C_SCL_GPIO,
        .sda_io_num = BOARD_I2C_SDA_GPIO,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&i2c_config, &s_i2c_bus), TAG, "I2C init failed");

    i2s_chan_config_t channel_config = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    channel_config.auto_clear = true;
    ESP_RETURN_ON_ERROR(i2s_new_channel(&channel_config, &s_i2s_tx, NULL), TAG, "I2S channel creation failed");

    const i2s_std_config_t i2s_config = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = BOARD_I2S_MCLK_GPIO,
            .bclk = BOARD_I2S_BCLK_GPIO,
            .ws = BOARD_I2S_WS_GPIO,
            .dout = BOARD_I2S_DOUT_GPIO,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_i2s_tx, &i2s_config), TAG, "I2S init failed");

    audio_codec_i2c_cfg_t codec_i2c_config = {
        .port = I2C_NUM_0,
        .addr = ES8311_CODEC_DEFAULT_ADDR,
        .bus_handle = s_i2c_bus,
    };
    const audio_codec_ctrl_if_t *control_if = audio_codec_new_i2c_ctrl(&codec_i2c_config);
    audio_codec_i2s_cfg_t codec_i2s_config = {
        .port = I2S_NUM_0,
        .tx_handle = s_i2s_tx,
        .rx_handle = NULL,
    };
    const audio_codec_data_if_t *data_if = audio_codec_new_i2s_data(&codec_i2s_config);
    const audio_codec_gpio_if_t *gpio_if = audio_codec_new_gpio();
    if (control_if == NULL || data_if == NULL || gpio_if == NULL) {
        ESP_LOGE(TAG, "Failed to create ES8311 codec interfaces");
        return ESP_FAIL;
    }

    es8311_codec_cfg_t es8311_config = {
        .ctrl_if = control_if,
        .gpio_if = gpio_if,
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC,
        .pa_pin = -1,
        .pa_reverted = false,
        .master_mode = false,
        .use_mclk = true,
        .digital_mic = false,
    };
    const audio_codec_if_t *codec_if = es8311_codec_new(&es8311_config);
    if (codec_if == NULL) {
        ESP_LOGE(TAG, "Failed to create ES8311 codec");
        return ESP_FAIL;
    }

    esp_codec_dev_cfg_t device_config = {
        .dev_type = ESP_CODEC_DEV_TYPE_OUT,
        .codec_if = codec_if,
        .data_if = data_if,
    };
    s_codec = esp_codec_dev_new(&device_config);
    if (s_codec == NULL) {
        ESP_LOGE(TAG, "Failed to create codec device");
        return ESP_FAIL;
    }

    esp_codec_dev_sample_info_t sample_info = {
        .bits_per_sample = 16,
        .channel = 2,
        .sample_rate = AUDIO_SAMPLE_RATE,
    };
    // esp_codec_dev reconfigures its I2S data interface during open and first disables TX.
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_i2s_tx), TAG, "I2S TX enable failed");
    if (esp_codec_dev_open(s_codec, &sample_info) != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "Failed to open ES8311 output");
        return ESP_FAIL;
    }
    if (esp_codec_dev_set_out_vol(s_codec, s_volume) != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "Failed to set ES8311 output volume");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "ES8311 output ready at 48 kHz; speaker amplifier remains off while idle");
    return ESP_OK;
}

static esp_err_t play_boot_chime(void)
{
    static const float frequencies[] = {261.63f, 329.63f, 392.00f, 523.25f};
    const size_t samples_per_note = (AUDIO_SAMPLE_RATE * 150U) / 1000U;
    const float amplitude = 8000.0f;
    const float pi = 3.14159265358979323846f;
    int16_t stereo_buffer[AUDIO_CHUNK_SAMPLES * 2];

    esp_err_t result = gpio_set_level(BOARD_PA_ENABLE_GPIO, 1);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "Could not enable speaker amplifier for boot chime: %s",
                 esp_err_to_name(result));
        return result;
    }
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_LOGI(TAG, "Playing C-E-G-C boot chime");

    for (size_t note = 0; note < sizeof(frequencies) / sizeof(frequencies[0]); ++note) {
        for (size_t offset = 0; offset < samples_per_note; offset += AUDIO_CHUNK_SAMPLES) {
            const size_t count = samples_per_note - offset < AUDIO_CHUNK_SAMPLES
                ? samples_per_note - offset : AUDIO_CHUNK_SAMPLES;
            for (size_t i = 0; i < count; ++i) {
                const float t = (float)(offset + i) / AUDIO_SAMPLE_RATE;
                const int16_t sample = (int16_t)(
                    amplitude * sinf(2.0f * pi * frequencies[note] * t));
                stereo_buffer[i * 2] = sample;
                stereo_buffer[i * 2 + 1] = sample;
            }
            const int write_result = esp_codec_dev_write(
                s_codec, stereo_buffer, count * 2 * sizeof(stereo_buffer[0]));
            if (write_result != ESP_CODEC_DEV_OK) {
                ESP_LOGE(TAG, "Boot chime playback failed: %d", write_result);
                result = ESP_FAIL;
                goto cleanup;
            }
        }
    }

    memset(stereo_buffer, 0, sizeof(stereo_buffer));
    if (esp_codec_dev_write(s_codec, stereo_buffer, sizeof(stereo_buffer)) != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "Could not flush boot chime audio");
        result = ESP_FAIL;
    }
    vTaskDelay(pdMS_TO_TICKS(30));

cleanup:
    {
        const esp_err_t disable_result = gpio_set_level(BOARD_PA_ENABLE_GPIO, 0);
        if (disable_result != ESP_OK) {
            ESP_LOGE(TAG, "Could not disable speaker amplifier after boot chime: %s",
                     esp_err_to_name(disable_result));
            if (result == ESP_OK) {
                result = disable_result;
            }
        }
    }
    return result;
}

static void playback_task(void *arg)
{
    playback_request_t request = *(playback_request_t *)arg;
    free(arg);

    int16_t mono_buffer[AUDIO_CHUNK_SAMPLES];
    int16_t stereo_buffer[AUDIO_CHUNK_SAMPLES * 2];
    uint32_t chunks_since_yield = 0;
    const int64_t playback_started_us = esp_timer_get_time();
    request.config.duration_ms = request.loop ? 0 : request.duration_ms;
    esp_err_t result = siren_synth_init(&s_synth, &request.config);
    bool finished = ulTaskNotifyTake(pdTRUE, 0) != 0;
    bool stop_requested = false;
    if (finished) {
        result = ESP_OK;
    }
    if (result == ESP_OK && !finished) {
        result = gpio_set_level(BOARD_PA_ENABLE_GPIO, 1);
    }
    if (result == ESP_OK && !finished) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    while (result == ESP_OK && !finished) {
        while (request.loop || s_synth.samples_rendered < s_synth.total_samples) {
            if (request.loop && request.duration_ms > 0U &&
                esp_timer_get_time() - playback_started_us >=
                    (int64_t)request.duration_ms * 1000) {
                finished = true;
                stop_requested = true;
                break;
            }
            if (ulTaskNotifyTake(pdTRUE, 0) != 0) {
                finished = true;
                stop_requested = true;
                break;
            }

            result = siren_synth_render(&s_synth, mono_buffer, AUDIO_CHUNK_SAMPLES);
            if (result != ESP_OK) {
                break;
            }
            for (size_t i = 0; i < AUDIO_CHUNK_SAMPLES; ++i) {
                stereo_buffer[i * 2] = mono_buffer[i];
                stereo_buffer[i * 2 + 1] = mono_buffer[i];
            }

            const int write_result = esp_codec_dev_write(s_codec, stereo_buffer, sizeof(stereo_buffer));
            if (write_result != ESP_CODEC_DEV_OK) {
                ESP_LOGE(TAG, "ES8311 playback failed: %d", write_result);
                result = ESP_FAIL;
                break;
            }

            if (++chunks_since_yield >= 128U) {
                vTaskDelay(1);
                chunks_since_yield = 0;
            }
        }

        break;
    }

    bool fast_release_requested = stop_requested;
    if (result == ESP_OK && stop_requested &&
        s_playback_stop_mode == PLAYBACK_STOP_WIND_DOWN) {
        uint64_t wind_down_total_samples =
            (uint64_t)s_playback_wind_down_duration_ms * s_synth.sample_rate / 1000U;
        result = siren_synth_begin_wind_down(&s_synth, s_playback_wind_down_duration_ms);
        uint64_t wind_down_samples_written = 0U;
        fast_release_requested = false;
        bool brake_override = false;
        while (result == ESP_OK && wind_down_samples_written < wind_down_total_samples) {
            if (!brake_override && ulTaskNotifyTake(pdTRUE, 0) != 0 &&
                s_playback_stop_mode == PLAYBACK_STOP_BRAKE) {
                const uint32_t brake_duration_ms = (uint32_t)s_synth.release_ms;
                result = siren_synth_begin_wind_down(&s_synth, brake_duration_ms);
                wind_down_total_samples =
                    (uint64_t)brake_duration_ms * s_synth.sample_rate / 1000U;
                wind_down_samples_written = 0U;
                brake_override = true;
                continue;
            }

            result = siren_synth_render(&s_synth, mono_buffer, AUDIO_CHUNK_SAMPLES);
            if (result != ESP_OK) {
                break;
            }

            const uint64_t samples_left = wind_down_total_samples - wind_down_samples_written;
            const size_t samples_this_chunk = samples_left < AUDIO_CHUNK_SAMPLES
                ? (size_t)samples_left : AUDIO_CHUNK_SAMPLES;
            for (size_t i = 0; i < AUDIO_CHUNK_SAMPLES; ++i) {
                stereo_buffer[i * 2] = i < samples_this_chunk ? mono_buffer[i] : 0;
                stereo_buffer[i * 2 + 1] = stereo_buffer[i * 2];
            }

            const int write_result = esp_codec_dev_write(s_codec, stereo_buffer, sizeof(stereo_buffer));
            if (write_result != ESP_CODEC_DEV_OK) {
                ESP_LOGE(TAG, "ES8311 wind-down playback failed: %d", write_result);
                result = ESP_FAIL;
                break;
            }
            wind_down_samples_written += samples_this_chunk;
            if (++chunks_since_yield >= 32U) {
                vTaskDelay(1);
                chunks_since_yield = 0;
            }
        }
    }

    if (result == ESP_OK && fast_release_requested) {
        const uint64_t release_total_samples = (uint64_t)(
            s_synth.release_ms * (float)s_synth.sample_rate / 1000.0f);
        uint64_t release_samples_written = 0U;
        while (release_samples_written < release_total_samples) {
            result = siren_synth_render(&s_synth, mono_buffer, AUDIO_CHUNK_SAMPLES);
            if (result != ESP_OK) {
                break;
            }

            const uint64_t samples_left = release_total_samples - release_samples_written;
            const size_t samples_this_chunk = samples_left < AUDIO_CHUNK_SAMPLES
                ? (size_t)samples_left : AUDIO_CHUNK_SAMPLES;
            for (size_t i = 0; i < AUDIO_CHUNK_SAMPLES; ++i) {
                int16_t sample = 0;
                if (i < samples_this_chunk) {
                    const uint64_t sample_position = release_samples_written + i;
                    const float gain = (float)(release_total_samples - sample_position - 1U) /
                                       (float)release_total_samples;
                    sample = (int16_t)((float)mono_buffer[i] * gain);
                }
                stereo_buffer[i * 2] = sample;
                stereo_buffer[i * 2 + 1] = sample;
            }

            const int write_result = esp_codec_dev_write(s_codec, stereo_buffer, sizeof(stereo_buffer));
            if (write_result != ESP_CODEC_DEV_OK) {
                ESP_LOGE(TAG, "ES8311 release playback failed: %d", write_result);
                result = ESP_FAIL;
                break;
            }
            release_samples_written += samples_this_chunk;
        }
    }

    siren_synth_deinit(&s_synth);

    if (result != ESP_OK) {
        ESP_LOGE(TAG, "Siren renderer failed: %s", esp_err_to_name(result));
    }

    static int16_t silence[AUDIO_CHUNK_SAMPLES * 2] = {0};
    size_t bytes_written = 0;
    i2s_channel_write(s_i2s_tx, silence, sizeof(silence), &bytes_written, pdMS_TO_TICKS(100));
    vTaskDelay(pdMS_TO_TICKS(30));
    gpio_set_level(BOARD_PA_ENABLE_GPIO, 0);

    s_playback_active = false;
    s_playback_loop = false;
    s_playback_duration_ms = 0;
    s_playback_safety_timeout_ms = 0;
    s_playback_stop_mode = PLAYBACK_STOP_NONE;
    s_playback_wind_down_duration_ms = 0;
    s_playback_task = NULL;
    vTaskDelete(NULL);
}

static int start_playback(uint32_t duration_ms, bool loop)
{
    if (s_audio_state != AUDIO_STATE_READY) {
        printf("Audio is not ready.\n");
        return 1;
    }
    if (s_playback_active) {
        printf("Siren is already playing; use 'siren stop' first.\n");
        return 1;
    }

    playback_request_t *request = malloc(sizeof(*request));
    if (request == NULL) {
        printf("Could not allocate playback request.\n");
        return 1;
    }
    request->config = s_synth_config;
    request->duration_ms = duration_ms;
    request->loop = loop;

    s_playback_active = true;
    s_playback_loop = loop;
    s_playback_duration_ms = duration_ms;
    s_playback_safety_timeout_ms = loop ? duration_ms : 0U;
    s_playback_stop_mode = PLAYBACK_STOP_NONE;
    s_playback_wind_down_duration_ms = 0;
    if (xTaskCreate(playback_task, "siren_play", 6144, request, 5, &s_playback_task) != pdPASS) {
        s_playback_active = false;
        s_playback_loop = false;
        s_playback_duration_ms = 0;
        s_playback_safety_timeout_ms = 0;
        s_playback_stop_mode = PLAYBACK_STOP_NONE;
        s_playback_wind_down_duration_ms = 0;
        free(request);
        printf("Could not create playback task.\n");
        return 1;
    }

    if (loop) {
        if (duration_ms == 0U) {
            printf("Siren loop started without a safety timeout.\n");
        } else {
            printf("Siren loop started; safety timeout is %" PRIu32 " seconds.\n",
                   duration_ms / 1000U);
        }
    } else {
        printf("Siren playing for %" PRIu32 " seconds.\n", duration_ms / 1000);
    }
    return 0;
}

static void print_siren_config(void)
{
    static const char *patterns[] = {"warning", "all-clear", "catastrophe", "test"};
    static const char *rooms[] = {"off", "outdoor", "bunker", "night"};
    static const char *sources[] = {"e57", "chopper"};
    static const char *chopper_configs[] = {"single", "multi-tone", "multi-chopper"};
    printf("pattern=%s sample_rate=%" PRIu32 " (fixed by audio hardware)\n",
           patterns[s_synth_config.pattern], s_synth_config.sample_rate);
    printf("duration_ms=%" PRIu32 " amplitude=%.3f sweep_start_hz=%.1f sweep_end_hz=%.1f\n",
           s_synth_config.duration_ms, s_synth_config.amplitude,
           s_synth_config.sweep_start_hz, s_synth_config.sweep_end_hz);
    printf("rotor_hz=%.2f modulation_depth=%.3f wobble_hz=%.2f attack_ms=%.1f release_ms=%.1f\n",
           s_synth_config.rotor_hz, s_synth_config.modulation_depth,
           s_synth_config.wobble_hz, s_synth_config.attack_ms, s_synth_config.release_ms);
    printf("room=%s room_size=%.2f room_mix=%.2f radio=%s radio_mix=%.2f "
           "distance_mix=%.2f "
           "thermal=%s jitter=%s volume=%d/100\n",
           rooms[s_synth_config.room], s_synth_config.room_size, s_synth_config.room_mix,
           s_synth_config.radio_enabled ? "on" : "off", s_synth_config.radio_mix,
           s_synth_config.distance_mix,
           s_synth_config.thermal_enabled ? "on" : "off",
           s_synth_config.jitter_enabled ? "on" : "off", s_volume);
    printf("source=%s chopper=%s peak_rpm=%.0f rise=%.2fs fall=%.2fs\n",
           sources[s_synth_config.source], chopper_configs[s_synth_config.chopper_config],
           s_synth_config.peak_rpm, s_synth_config.motor_rise_s, s_synth_config.motor_fall_s);
    printf("ports: rotor_high=%.0f rotor_low=%.0f vane_high=%.0f vane_low=%.0f "
           "undertone=%.0f overtone=%.0f\n",
           s_synth_config.rotor_ports_high, s_synth_config.rotor_ports_low,
           s_synth_config.vanes_high, s_synth_config.vanes_low,
           s_synth_config.undertone_ports, s_synth_config.overtone_ports);
    printf("mix: rotor_high=%.2f rotor_low=%.2f vane_high=%.2f vane_low=%.2f "
           "undertone=%.2f overtone=%.2f\n",
           s_synth_config.mix_rotor_high, s_synth_config.mix_rotor_low,
           s_synth_config.mix_vanes_high, s_synth_config.mix_vanes_low,
           s_synth_config.mix_undertone, s_synth_config.mix_overtone);
}

static int reset_runtime_config(void)
{
    if (s_config_transaction_active) {
        printf("Abort the active settings transaction before resetting configuration.\n");
        return 1;
    }

    siren_synth_config_t defaults;
    siren_synth_config_defaults(&defaults, AUDIO_SAMPLE_RATE);

    const int previous_volume = s_volume;
    if (s_audio_state == AUDIO_STATE_READY &&
        esp_codec_dev_set_out_vol(s_codec, 50) != ESP_CODEC_DEV_OK) {
        printf("Could not apply factory speaker volume.\n");
        return 1;
    }

    const esp_err_t save_result = save_runtime_config(&defaults, 50);
    if (save_result != ESP_OK) {
        if (s_audio_state == AUDIO_STATE_READY &&
            esp_codec_dev_set_out_vol(s_codec, previous_volume) != ESP_CODEC_DEV_OK) {
            printf("Could not restore the previous codec volume after NVS save failed.\n");
        }
        printf("Could not reset settings in NVS: %s\n", esp_err_to_name(save_result));
        return 1;
    }

    s_synth_config = defaults;
    s_volume = 50;
    printf("Factory settings restored and saved to NVS. Playback was not started.\n");
    return 0;
}

static bool parse_float(const char *text, float *value)
{
    char *end = NULL;
    errno = 0;
    const float parsed = strtof(text, &end);
    if (errno != 0 || end == text || *end != '\0' || !isfinite(parsed)) {
        return false;
    }
    *value = parsed;
    return true;
}

static int compare_u32(const void *left, const void *right)
{
    const uint32_t left_value = *(const uint32_t *)left;
    const uint32_t right_value = *(const uint32_t *)right;
    return (left_value > right_value) - (left_value < right_value);
}

static int run_siren_benchmark(void)
{
    if (s_playback_active) {
        printf("Stop playback before running the DSP benchmark.\n");
        return 1;
    }

    siren_synth_config_t config = s_synth_config;
    config.duration_ms = 0;
    esp_err_t result = siren_synth_init(&s_synth, &config);
    if (result != ESP_OK) {
        printf("Could not initialize benchmark renderer: %s\n", esp_err_to_name(result));
        return 1;
    }

    int16_t buffer[AUDIO_CHUNK_SAMPLES];
    uint64_t total_render_us = 0;
    uint64_t total_sample_power = 0;
    uint16_t peak_sample = 0;
    for (size_t block = 0; block < BENCHMARK_BLOCK_COUNT; ++block) {
        const int64_t start_us = esp_timer_get_time();
        result = siren_synth_render(&s_synth, buffer, AUDIO_CHUNK_SAMPLES);
        const int64_t elapsed_us = esp_timer_get_time() - start_us;
        if (result != ESP_OK) {
            const esp_err_t deinit_result = siren_synth_deinit(&s_synth);
            if (deinit_result != ESP_OK) {
                printf("Benchmark renderer cleanup failed: %s\n",
                       esp_err_to_name(deinit_result));
            }
            printf("DSP benchmark render failed: %s\n", esp_err_to_name(result));
            return 1;
        }

        s_benchmark_block_us[block] = (uint32_t)elapsed_us;
        total_render_us += (uint64_t)elapsed_us;
        for (size_t sample = 0; sample < AUDIO_CHUNK_SAMPLES; ++sample) {
            const int32_t pcm = buffer[sample];
            const uint16_t magnitude = (uint16_t)(pcm < 0 ? -pcm : pcm);
            total_sample_power += (uint64_t)(pcm * pcm);
            if (magnitude > peak_sample) {
                peak_sample = magnitude;
            }
        }
        if ((block & 0x0fU) == 0x0fU) {
            taskYIELD();
        }
    }
    result = siren_synth_deinit(&s_synth);
    if (result != ESP_OK) {
        printf("Benchmark renderer cleanup failed: %s\n", esp_err_to_name(result));
        return 1;
    }

    qsort(s_benchmark_block_us, BENCHMARK_BLOCK_COUNT,
          sizeof(s_benchmark_block_us[0]), compare_u32);
    const uint32_t percentile_95_index =
        (BENCHMARK_BLOCK_COUNT * 95U + 99U) / 100U - 1U;
    const uint32_t budget_us =
        (1000000U * AUDIO_CHUNK_SAMPLES) / AUDIO_SAMPLE_RATE;
    const uint32_t maximum_us = s_benchmark_block_us[BENCHMARK_BLOCK_COUNT - 1U];
    printf("DSP-only render: %" PRIu32 " blocks, %" PRIu32 " samples at %d Hz\n",
           (uint32_t)BENCHMARK_BLOCK_COUNT,
           (uint32_t)(BENCHMARK_BLOCK_COUNT * AUDIO_CHUNK_SAMPLES), AUDIO_SAMPLE_RATE);
    printf("block time us: min=%" PRIu32 " p95=%" PRIu32 " max=%" PRIu32
           " mean=%" PRIu32 " budget=%" PRIu32 " (%s)\n",
           s_benchmark_block_us[0],
           s_benchmark_block_us[percentile_95_index],
           maximum_us,
           (uint32_t)(total_render_us / BENCHMARK_BLOCK_COUNT),
           budget_us,
           maximum_us < budget_us ? "PASS" : "FAIL");
    const uint32_t rendered_samples =
        (uint32_t)(BENCHMARK_BLOCK_COUNT * AUDIO_CHUNK_SAMPLES);
    const float rms = sqrtf((float)total_sample_power / (float)rendered_samples);
    printf("DSP output: rms=%.1f peak=%u/32767\n", rms, (unsigned int)peak_sample);
    printf("This measures synthesis only, not I2S/codec output or acoustic quality.\n");
    return maximum_us < budget_us ? 0 : 1;
}

static int command_siren(int argc, char **argv)
{
    if (argc < 2) {
        printf("Usage: siren <seconds|0|loop [safety seconds 0..3600]|cancel|brake|stop|status|config [show|save|load|reset|begin|commit|abort]|reset|benchmark|preset|room|model|chopper|set>\n");
        return 1;
    }
    const siren_synth_config_t previous_config = s_synth_config;

    if (strcmp(argv[1], "benchmark") == 0 && argc == 2) {
        return run_siren_benchmark();
    }
    if (strcmp(argv[1], "cancel") == 0) {
        if (argc != 2) {
            printf("Usage: siren cancel\n");
            return 1;
        }
        if (!s_playback_active || s_playback_task == NULL) {
            printf("Siren is already stopped.\n");
            return 0;
        }
        if (s_playback_stop_mode == PLAYBACK_STOP_WIND_DOWN) {
            printf("Siren is already winding down; use 'siren stop' to brake sooner.\n");
            return 0;
        }
        float wind_down_seconds = s_synth_config.motor_fall_s * 3.0f;
        if (wind_down_seconds < 1.0f) {
            wind_down_seconds = 1.0f;
        }
        s_playback_wind_down_duration_ms = (uint32_t)(wind_down_seconds * 1000.0f);
        s_playback_stop_mode = PLAYBACK_STOP_WIND_DOWN;
        xTaskNotifyGive(s_playback_task);
        printf("Siren wind-down requested for %" PRIu32
               " ms; use 'siren stop' to brake sooner.\n",
               s_playback_wind_down_duration_ms);
        return 0;
    }
    if (strcmp(argv[1], "stop") == 0 || strcmp(argv[1], "brake") == 0 ||
        strcmp(argv[1], "0") == 0) {
        if (argc != 2) {
            printf("Usage: siren brake | siren stop | siren 0\n");
            return 1;
        }
        if (!s_playback_active || s_playback_task == NULL) {
            printf("Siren is already stopped.\n");
            return 0;
        }
        s_playback_stop_mode = PLAYBACK_STOP_BRAKE;
        xTaskNotifyGive(s_playback_task);
        printf(strcmp(argv[1], "brake") == 0
                   ? "Siren brake requested.\n"
                   : "Siren stop requested.\n");
        return 0;
    }
    if (strcmp(argv[1], "status") == 0 && argc == 2) {
        if (s_playback_stop_mode == PLAYBACK_STOP_WIND_DOWN) {
            printf("playback=active mode=wind_down wind_down_ms=%" PRIu32 "\n",
                   s_playback_wind_down_duration_ms);
        } else if (s_playback_stop_mode == PLAYBACK_STOP_BRAKE) {
            printf("playback=%s mode=braking\n", s_playback_active ? "active" : "idle");
        } else if (s_playback_loop) {
            printf("playback=%s mode=loop safety_timeout_ms=%" PRIu32 "\n",
                   s_playback_active ? "active" : "idle", s_playback_safety_timeout_ms);
        } else {
            printf("playback=%s mode=timed duration_ms=%" PRIu32 "\n",
                   s_playback_active ? "active" : "idle", s_playback_duration_ms);
        }
        return 0;
    }
    if (strcmp(argv[1], "config") == 0) {
        if (argc == 2 || (argc == 3 && strcmp(argv[2], "show") == 0)) {
            print_siren_config();
            return 0;
        }
        if (argc == 3 && strcmp(argv[2], "begin") == 0) {
            return begin_config_transaction();
        }
        if (argc == 3 && strcmp(argv[2], "commit") == 0) {
            return commit_config_transaction();
        }
        if (argc == 3 && strcmp(argv[2], "abort") == 0) {
            return abort_config_transaction();
        }
        if (argc == 3 && strcmp(argv[2], "save") == 0) {
            if (s_config_transaction_active) {
                printf("Commit or abort the active settings transaction before saving.\n");
                return 1;
            }
            const esp_err_t result = save_runtime_config(&s_synth_config, s_volume);
            if (result != ESP_OK) {
                printf("Could not save configuration: %s\n", esp_err_to_name(result));
                return 1;
            }
            printf("Configuration saved to NVS. Settings are also saved automatically when changed.\n");
            return 0;
        }
        if (argc == 3 && strcmp(argv[2], "load") == 0) {
            if (s_config_transaction_active) {
                printf("Commit or abort the active settings transaction before loading.\n");
                return 1;
            }
            siren_synth_config_t loaded_config;
            int loaded_volume;
            const esp_err_t result = load_runtime_config(&loaded_config, &loaded_volume);
            if (result != ESP_OK) {
                printf("Could not load configuration: %s\n", esp_err_to_name(result));
                return 1;
            }
            if (s_audio_state == AUDIO_STATE_READY &&
                esp_codec_dev_set_out_vol(s_codec, loaded_volume) != ESP_CODEC_DEV_OK) {
                printf("Configuration loaded, but codec volume could not be applied.\n");
                return 1;
            }
            s_synth_config = loaded_config;
            s_volume = loaded_volume;
            printf("Saved configuration loaded. It applies to the next playback.\n");
            return 0;
        }
        if (argc == 3 && strcmp(argv[2], "reset") == 0) {
            return reset_runtime_config();
        }
        printf("Usage: siren config [show|save|load|reset|begin|commit|abort]\n");
        return 1;
    }
    if (strcmp(argv[1], "reset") == 0 && argc == 2) {
        return reset_runtime_config();
    }
    if (strcmp(argv[1], "loop") == 0 && (argc == 2 || argc == 3)) {
        uint32_t safety_timeout_ms = 0U;
        if (argc == 3) {
            char *end = NULL;
            errno = 0;
            const unsigned long seconds = strtoul(argv[2], &end, 10);
            if (errno != 0 || end == argv[2] || *end != '\0' || seconds > 3600UL) {
                printf("Usage: siren loop [safety seconds 0..3600]\n");
                return 1;
            }
            safety_timeout_ms = (uint32_t)seconds * 1000U;
        }
        return start_playback(safety_timeout_ms, true);
    }
    if (strcmp(argv[1], "preset") == 0) {
        if (argc == 2) {
            printf("Presets: warning, all-clear (Entwarnung), catastrophe, test\n");
            return 0;
        }
        if (argc != 3) {
            printf("Usage: siren preset [warning|all-clear|catastrophe|test]\n");
            return 1;
        }
        if (strcmp(argv[2], "warning") == 0) {
            s_synth_config.pattern = SIREN_PATTERN_WARNING;
        } else if (strcmp(argv[2], "all-clear") == 0) {
            s_synth_config.pattern = SIREN_PATTERN_ALL_CLEAR;
        } else if (strcmp(argv[2], "catastrophe") == 0) {
            s_synth_config.pattern = SIREN_PATTERN_CATASTROPHE;
        } else if (strcmp(argv[2], "test") == 0) {
            s_synth_config.pattern = SIREN_PATTERN_TEST;
        } else {
            printf("Unknown preset: %s\n", argv[2]);
            return 1;
        }
        if (persist_siren_config_change(&previous_config) != 0) {
            return 1;
        }
        printf("Preset selected and %s: %s\n",
               s_config_transaction_active ? "staged in transaction" : "saved to NVS",
               argv[2]);
        return 0;
    }
    if (strcmp(argv[1], "room") == 0) {
        if (argc != 3) {
            printf("Usage: siren room <off|outdoor|bunker|night>\n");
            return 1;
        }
        if (strcmp(argv[2], "off") == 0) {
            s_synth_config.room = SIREN_ROOM_OFF;
        } else if (strcmp(argv[2], "outdoor") == 0) {
            s_synth_config.room = SIREN_ROOM_OUTDOOR;
        } else if (strcmp(argv[2], "bunker") == 0) {
            s_synth_config.room = SIREN_ROOM_BUNKER;
        } else if (strcmp(argv[2], "night") == 0) {
            s_synth_config.room = SIREN_ROOM_NIGHT;
        } else {
            printf("Unknown room profile: %s\n", argv[2]);
            return 1;
        }
        if (persist_siren_config_change(&previous_config) != 0) {
            return 1;
        }
        printf("Room profile selected and %s: %s. It applies to the next playback.\n",
               s_config_transaction_active ? "staged in transaction" : "saved to NVS",
               argv[2]);
        return 0;
    }
    if (strcmp(argv[1], "model") == 0) {
        if (argc != 3) {
            printf("Usage: siren model <e57|chopper>\n");
            return 1;
        }
        if (strcmp(argv[2], "e57") == 0) {
            s_synth_config.source = SIREN_SOURCE_E57;
        } else if (strcmp(argv[2], "chopper") == 0) {
            s_synth_config.source = SIREN_SOURCE_CHOPPER;
        } else {
            printf("Unknown source model: %s\n", argv[2]);
            return 1;
        }
        if (persist_siren_config_change(&previous_config) != 0) {
            return 1;
        }
        printf("Source model selected and %s: %s. It applies to the next playback.\n",
               s_config_transaction_active ? "staged in transaction" : "saved to NVS",
               argv[2]);
        return 0;
    }
    if (strcmp(argv[1], "chopper") == 0) {
        if (argc != 3) {
            printf("Usage: siren chopper <single|multi-tone|multi-chopper>\n");
            return 1;
        }
        if (strcmp(argv[2], "single") == 0) {
            s_synth_config.chopper_config = SIREN_CHOPPER_SINGLE;
        } else if (strcmp(argv[2], "multi-tone") == 0) {
            s_synth_config.chopper_config = SIREN_CHOPPER_MULTI_TONE;
        } else if (strcmp(argv[2], "multi-chopper") == 0) {
            s_synth_config.chopper_config = SIREN_CHOPPER_MULTI_CHOPPER;
        } else {
            printf("Unknown chopper configuration: %s\n", argv[2]);
            return 1;
        }
        if (persist_siren_config_change(&previous_config) != 0) {
            return 1;
        }
        printf("Chopper configuration selected and %s: %s.\n",
               s_config_transaction_active ? "staged in transaction" : "saved to NVS",
               argv[2]);
        return 0;
    }
    if (strcmp(argv[1], "set") == 0) {
        if (argc != 4) {
            printf("Usage: siren set <parameter> <value>\n");
            return 1;
        }

        float value;
        if (!parse_float(argv[3], &value)) {
            printf("Invalid numeric value: %s\n", argv[3]);
            return 1;
        }
        if (strcmp(argv[2], "amplitude") == 0 && value > 0.0f && value <= 1.0f) {
            s_synth_config.amplitude = value;
        } else if (strcmp(argv[2], "sweep_start_hz") == 0 && value >= 20.0f &&
                   value < s_synth_config.sweep_end_hz && value <= 900.0f) {
            s_synth_config.sweep_start_hz = value;
        } else if (strcmp(argv[2], "sweep_end_hz") == 0 && value > s_synth_config.sweep_start_hz &&
                   value <= 900.0f) {
            s_synth_config.sweep_end_hz = value;
        } else if (strcmp(argv[2], "rotor_hz") == 0 && value > 0.0f && value <= 30.0f) {
            s_synth_config.rotor_hz = value;
        } else if (strcmp(argv[2], "modulation_depth") == 0 && value >= 0.0f && value <= 1.0f) {
            s_synth_config.modulation_depth = value;
        } else if (strcmp(argv[2], "wobble_hz") == 0 && value > 0.0f && value <= 30.0f) {
            s_synth_config.wobble_hz = value;
        } else if (strcmp(argv[2], "attack_ms") == 0 && value >= 1.0f && value <= 5000.0f) {
            s_synth_config.attack_ms = value;
        } else if (strcmp(argv[2], "release_ms") == 0 && value >= 1.0f && value <= 5000.0f) {
            s_synth_config.release_ms = value;
        } else if (strcmp(argv[2], "duration_ms") == 0 && value >= 100.0f && value <= 60000.0f) {
            s_synth_config.duration_ms = (uint32_t)value;
        } else if (strcmp(argv[2], "room_size") == 0 && value >= 0.0f && value <= 1.0f) {
            s_synth_config.room_size = value;
        } else if (strcmp(argv[2], "room_mix") == 0 && value >= 0.0f && value <= 1.0f) {
            s_synth_config.room_mix = value;
        } else if (strcmp(argv[2], "radio_mix") == 0 && value >= 0.0f && value <= 1.0f) {
            s_synth_config.radio_mix = value;
        } else if (strcmp(argv[2], "distance_mix") == 0 &&
                   value >= 0.0f && value <= 1.0f) {
            s_synth_config.distance_mix = value;
        } else if (strcmp(argv[2], "peak_rpm") == 0 && value >= 100.0f && value <= 30000.0f) {
            s_synth_config.peak_rpm = value;
        } else if (strcmp(argv[2], "rotor_high_ports") == 0 && value >= 0.0f && value <= 64.0f) {
            s_synth_config.rotor_ports_high = value;
        } else if (strcmp(argv[2], "rotor_low_ports") == 0 && value >= 0.0f && value <= 64.0f) {
            s_synth_config.rotor_ports_low = value;
        } else if (strcmp(argv[2], "vanes_high") == 0 && value >= 0.0f && value <= 64.0f) {
            s_synth_config.vanes_high = value;
        } else if (strcmp(argv[2], "vanes_low") == 0 && value >= 0.0f && value <= 64.0f) {
            s_synth_config.vanes_low = value;
        } else if (strcmp(argv[2], "undertone_ports") == 0 && value >= 0.0f && value <= 64.0f) {
            s_synth_config.undertone_ports = value;
        } else if (strcmp(argv[2], "overtone_ports") == 0 && value >= 0.0f && value <= 64.0f) {
            s_synth_config.overtone_ports = value;
        } else if (strcmp(argv[2], "motor_rise_s") == 0 && value >= 0.05f && value <= 30.0f) {
            s_synth_config.motor_rise_s = value;
        } else if (strcmp(argv[2], "motor_fall_s") == 0 && value >= 0.05f && value <= 60.0f) {
            s_synth_config.motor_fall_s = value;
        } else if (strcmp(argv[2], "mix_rotor_high") == 0 && value >= 0.0f && value <= 1.0f) {
            s_synth_config.mix_rotor_high = value;
        } else if (strcmp(argv[2], "mix_rotor_low") == 0 && value >= 0.0f && value <= 1.0f) {
            s_synth_config.mix_rotor_low = value;
        } else if (strcmp(argv[2], "mix_vanes_high") == 0 && value >= 0.0f && value <= 1.0f) {
            s_synth_config.mix_vanes_high = value;
        } else if (strcmp(argv[2], "mix_vanes_low") == 0 && value >= 0.0f && value <= 1.0f) {
            s_synth_config.mix_vanes_low = value;
        } else if (strcmp(argv[2], "mix_undertone") == 0 && value >= 0.0f && value <= 1.0f) {
            s_synth_config.mix_undertone = value;
        } else if (strcmp(argv[2], "mix_overtone") == 0 && value >= 0.0f && value <= 1.0f) {
            s_synth_config.mix_overtone = value;
        } else if ((strcmp(argv[2], "radio") == 0 ||
                    strcmp(argv[2], "thermal") == 0 ||
                    strcmp(argv[2], "jitter") == 0) &&
                   (value == 0.0f || value == 1.0f)) {
            const bool enabled = value == 1.0f;
            if (strcmp(argv[2], "radio") == 0) {
                s_synth_config.radio_enabled = enabled;
            } else if (strcmp(argv[2], "thermal") == 0) {
                s_synth_config.thermal_enabled = enabled;
            } else {
                s_synth_config.jitter_enabled = enabled;
            }
        } else {
            printf("Unknown parameter or value out of range: %s=%s\n", argv[2], argv[3]);
            printf("Valid: amplitude >0..1, sweep_start_hz 20..899 Hz below sweep_end_hz, "
                   "sweep_end_hz above sweep_start_hz..900 Hz, rotor/wobble >0..30 Hz, "
                   "modulation/room_size/room_mix/radio_mix/distance_mix/mix_* 0..1, "
                   "radio/thermal/jitter 0|1, "
                   "peak_rpm 100..30000, port/vane counts 0..64, motor_rise_s 0.05..30, "
                   "motor_fall_s 0.05..60, "
                   "attack/release 1..5000 ms, duration_ms 100..60000.\n");
            return 1;
        }
        if (persist_siren_config_change(&previous_config) != 0) {
            return 1;
        }
        printf("%s updated and %s.\n", argv[2],
               s_config_transaction_active ? "staged in transaction" : "saved to NVS");
        return 0;
    }

    char *end = NULL;
    errno = 0;
    const unsigned long seconds = strtoul(argv[1], &end, 10);
    if (errno == 0 && end != argv[1] && *end == '\0' && seconds > 0 && seconds <= 3600 && argc == 2) {
        return start_playback((uint32_t)seconds * 1000U, false);
    }

    printf("Usage: siren <1..3600 seconds|0|loop [safety seconds 0..3600]|cancel|brake|stop|status|config [show|save|load|reset|begin|commit|abort]|reset|benchmark|preset|room|model|chopper|set>\n");
    return 1;
}

static int command_audio(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "status") == 0) {
        const char *state = s_audio_state == AUDIO_STATE_READY ? "ready" :
                            s_audio_state == AUDIO_STATE_FAILED ? "failed" : "initializing";
        printf("audio=%s output=ES8311 sample_rate=%d Hz volume=%d/100\n",
               state, AUDIO_SAMPLE_RATE, s_volume);
        return 0;
    }
    if (argc == 3 && strcmp(argv[1], "volume") == 0) {
        char *end = NULL;
        errno = 0;
        const long volume = strtol(argv[2], &end, 10);
        if (errno != 0 || end == argv[2] || *end != '\0' || volume < 0 || volume > 100) {
            printf("Usage: audio volume <0..100>\n");
            return 1;
        }
        if (s_audio_state != AUDIO_STATE_READY) {
            printf("Audio is not ready.\n");
            return 1;
        }
        if (s_config_transaction_active) {
            s_volume = (int)volume;
            printf("Volume staged in settings transaction.\n");
            return 0;
        }
        const int result = esp_codec_dev_set_out_vol(s_codec, (int)volume);
        if (result != ESP_CODEC_DEV_OK) {
            printf("Could not set codec volume: %d\n", result);
            return 1;
        }
        const int previous_volume = s_volume;
        const esp_err_t save_result =
            save_runtime_config(&s_synth_config, (int)volume);
        if (save_result != ESP_OK) {
            const int rollback_result = esp_codec_dev_set_out_vol(s_codec, previous_volume);
            if (rollback_result != ESP_CODEC_DEV_OK) {
                printf("Could not restore the previous codec volume after NVS save failed: %d\n",
                       rollback_result);
            }
            printf("Could not save volume to NVS; volume change was reverted: %s\n",
                   esp_err_to_name(save_result));
            return 1;
        }
        s_volume = (int)volume;
        printf("Volume set to %d/100 and saved to NVS.\n", s_volume);
        return 0;
    }

    printf("Usage: audio status | audio volume <0..100>\n");
    return 1;
}

static esp_err_t register_command(const char *command, const char *help, esp_console_cmd_func_t function)
{
    const esp_console_cmd_t definition = {
        .command = command,
        .help = help,
        .hint = NULL,
        .func = function,
        .argtable = NULL,
    };
    return esp_console_cmd_register(&definition);
}

static int command_tool_id(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    printf("TOOL_ID:esp-idf-siren-synth:console_siren_dev:3\n");
    return 0;
}

void app_main(void)
{
    siren_synth_config_defaults(&s_synth_config, AUDIO_SAMPLE_RATE);
    esp_err_t nvs_result = nvs_flash_init();
    if (nvs_result == ESP_OK) {
        s_persistence_ready = true;
        siren_synth_config_t saved_config;
        int saved_volume;
        nvs_result = load_runtime_config(&saved_config, &saved_volume);
        if (nvs_result == ESP_OK) {
            s_synth_config = saved_config;
            s_volume = saved_volume;
            ESP_LOGI(TAG, "Loaded saved siren settings; playback remains stopped");
        } else if (nvs_result == ESP_ERR_NVS_NOT_FOUND) {
            nvs_result = save_runtime_config(&s_synth_config, s_volume);
            if (nvs_result == ESP_OK) {
                ESP_LOGI(TAG, "Initialized factory siren settings in NVS");
            } else {
                ESP_LOGE(TAG, "Could not initialize factory settings in NVS: %s",
                         esp_err_to_name(nvs_result));
            }
        } else {
            ESP_LOGW(TAG, "Saved settings unavailable (%s); using factory defaults",
                     esp_err_to_name(nvs_result));
        }
    } else {
        ESP_LOGE(TAG, "NVS unavailable (%s); persistence commands will fail and settings use defaults",
                 esp_err_to_name(nvs_result));
    }

    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_config.prompt = "siren> ";
    repl_config.max_cmdline_length = 256;
    repl_config.task_stack_size = 4096;
    esp_console_dev_uart_config_t uart_config = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_uart(&uart_config, &repl_config, &repl));
    ESP_ERROR_CHECK(register_command("TOOL_ID", "Report the serial tool identity", command_tool_id));
    ESP_ERROR_CHECK(register_command("siren", "Siren controls: siren <seconds|0|loop [safety seconds]|stop|status|config [show|save|load|reset|begin|commit|abort]|reset|preset|room|model|chopper|set|benchmark>", command_siren));
    ESP_ERROR_CHECK(register_command("audio", "Audio controls: audio status | audio volume <0..100>", command_audio));
    ESP_ERROR_CHECK(esp_console_start_repl(repl));
    printf("Siren dev console started. Type 'help'. Siren playback remains stopped at boot.\n");

    if (audio_output_init() == ESP_OK) {
        printf("Audio output ready (ES8311, 48 kHz). Speaker amplifier is off while idle.\n");
        const esp_err_t chime_result = play_boot_chime();
        s_audio_state = AUDIO_STATE_READY;
        if (chime_result == ESP_OK) {
            printf("Boot complete.\n");
        } else {
            printf("Boot chime failed; siren console remains available.\n");
        }
    } else {
        s_audio_state = AUDIO_STATE_FAILED;
        ESP_LOGE(TAG, "Audio initialization failed; console remains available");
        printf("Audio initialization failed; console remains available. Check the board and speaker path.\n");
    }
}
