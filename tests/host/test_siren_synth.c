#include "siren_synth.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #condition); \
        return false; \
    } \
} while (0)

#define FFT_SAMPLE_COUNT 2048U

static void radix2_fft(double *real, double *imag, size_t count) {
    size_t reversed = 0U;
    for (size_t index = 1U; index < count; ++index) {
        size_t bit = count >> 1U;
        while ((reversed & bit) != 0U) {
            reversed ^= bit;
            bit >>= 1U;
        }
        reversed ^= bit;
        if (index < reversed) {
            const double real_value = real[index];
            const double imag_value = imag[index];
            real[index] = real[reversed];
            imag[index] = imag[reversed];
            real[reversed] = real_value;
            imag[reversed] = imag_value;
        }
    }

    for (size_t span = 2U; span <= count; span <<= 1U) {
        const size_t half_span = span >> 1U;
        const double angle = -6.2831853071795864769 / (double)span;
        const double root_real = cos(angle);
        const double root_imag = sin(angle);
        for (size_t base = 0U; base < count; base += span) {
            double phase_real = 1.0;
            double phase_imag = 0.0;
            for (size_t offset = 0U; offset < half_span; ++offset) {
                const size_t even_index = base + offset;
                const size_t odd_index = even_index + half_span;
                const double odd_real =
                    phase_real * real[odd_index] - phase_imag * imag[odd_index];
                const double odd_imag =
                    phase_real * imag[odd_index] + phase_imag * real[odd_index];
                real[odd_index] = real[even_index] - odd_real;
                imag[odd_index] = imag[even_index] - odd_imag;
                real[even_index] += odd_real;
                imag[even_index] += odd_imag;
                const double next_phase_real =
                    phase_real * root_real - phase_imag * root_imag;
                phase_imag = phase_real * root_imag + phase_imag * root_real;
                phase_real = next_phase_real;
            }
        }
    }
}

static double fft_band_power(const int16_t *samples,
                             uint32_t sample_rate,
                             double lower_hz,
                             double upper_hz) {
    double real[FFT_SAMPLE_COUNT];
    double imag[FFT_SAMPLE_COUNT] = {0.0};
    for (size_t index = 0U; index < FFT_SAMPLE_COUNT; ++index) {
        real[index] = (double)samples[index];
    }
    radix2_fft(real, imag, FFT_SAMPLE_COUNT);

    double power = 0.0;
    for (size_t bin = 1U; bin <= FFT_SAMPLE_COUNT / 2U; ++bin) {
        const double frequency =
            (double)bin * (double)sample_rate / (double)FFT_SAMPLE_COUNT;
        if (frequency >= lower_hz && frequency < upper_hz) {
            power += real[bin] * real[bin] + imag[bin] * imag[bin];
        }
    }
    return power;
}

static bool render_chopper_spectrum(float distance_mix, int16_t *output) {
    siren_synth_config_t config;
    siren_synth_config_defaults(&config, 48000U);
    config.duration_ms = 0U;
    config.pattern = SIREN_PATTERN_ALL_CLEAR;
    config.source = SIREN_SOURCE_CHOPPER;
    config.chopper_config = SIREN_CHOPPER_MULTI_TONE;
    config.peak_rpm = 5625.0f;
    config.rotor_ports_high = 64.0f;
    config.rotor_ports_low = 0.0f;
    config.vanes_high = 4.0f;
    config.vanes_low = 0.0f;
    config.undertone_ports = 0.0f;
    config.overtone_ports = 0.0f;
    config.mix_rotor_high = 1.0f;
    config.mix_rotor_low = 0.0f;
    config.mix_vanes_high = 1.0f;
    config.mix_vanes_low = 0.0f;
    config.mix_undertone = 0.0f;
    config.mix_overtone = 0.0f;
    config.motor_rise_s = 0.05f;
    config.thermal_enabled = false;
    config.jitter_enabled = false;
    config.distance_mix = distance_mix;

    siren_synth_handle_t handle;
    CHECK(siren_synth_init(&handle, &config) == ESP_OK);
    int16_t warmup[256];
    for (size_t sample = 0U; sample < config.sample_rate; sample += 256U) {
        CHECK(siren_synth_render(&handle, warmup, 256U) == ESP_OK);
    }
    CHECK(siren_synth_render(&handle, output, FFT_SAMPLE_COUNT) == ESP_OK);
    CHECK(siren_synth_deinit(&handle) == ESP_OK);
    return true;
}

static bool test_finite_render_is_deterministic_and_zero_padded(void) {
    siren_synth_config_t config;
    siren_synth_config_defaults(&config, 8000U);
    config.duration_ms = 100U;

    siren_synth_handle_t first;
    siren_synth_handle_t second;
    CHECK(siren_synth_init(&first, &config) == ESP_OK);
    CHECK(siren_synth_init(&second, &config) == ESP_OK);

    int16_t first_output[1024];
    int16_t second_output[1024];
    CHECK(siren_synth_render(&first, first_output, 1024U) == ESP_OK);
    CHECK(siren_synth_render(&second, second_output, 1024U) == ESP_OK);
    CHECK(memcmp(first_output, second_output, sizeof(first_output)) == 0);
    CHECK(first.samples_rendered == 800U);

    bool has_signal = false;
    for (size_t i = 0; i < 800U; ++i) {
        CHECK(first_output[i] >= -32767 && first_output[i] <= 32767);
        has_signal |= first_output[i] != 0;
    }
    CHECK(has_signal);
    for (size_t i = 800U; i < 1024U; ++i) {
        CHECK(first_output[i] == 0);
    }

    CHECK(siren_synth_deinit(&first) == ESP_OK);
    CHECK(siren_synth_deinit(&second) == ESP_OK);
    return true;
}

static bool test_wind_down_reaches_silence(void) {
    siren_synth_config_t config;
    siren_synth_config_defaults(&config, 8000U);
    config.duration_ms = 0U;

    siren_synth_handle_t handle;
    CHECK(siren_synth_init(&handle, &config) == ESP_OK);

    int16_t output[1024];
    CHECK(siren_synth_render(&handle, output, 256U) == ESP_OK);
    CHECK(siren_synth_begin_wind_down(&handle, 100U) == ESP_OK);
    CHECK(handle.wind_down_total_samples == 800U);
    CHECK(siren_synth_render(&handle, output, 1024U) == ESP_OK);
    for (size_t i = 800U; i < 1024U; ++i) {
        CHECK(output[i] == 0);
    }
    CHECK(siren_synth_begin_wind_down(&handle, 0U) == ESP_ERR_INVALID_ARG);

    CHECK(siren_synth_deinit(&handle) == ESP_OK);
    return true;
}

static bool test_effects_are_deterministic_and_deinit_releases_room(void) {
    siren_synth_config_t config;
    siren_synth_config_defaults(&config, 48000U);
    config.duration_ms = 0U;
    config.room = SIREN_ROOM_NIGHT;
    config.room_mix = 0.4f;
    config.radio_enabled = true;

    siren_synth_handle_t first;
    siren_synth_handle_t second;
    CHECK(siren_synth_init(&first, &config) == ESP_OK);
    CHECK(siren_synth_init(&second, &config) == ESP_OK);
    CHECK(first.room_buffer != NULL);

    int16_t first_output[512];
    int16_t second_output[512];
    CHECK(siren_synth_render(&first, first_output, 512U) == ESP_OK);
    CHECK(siren_synth_render(&second, second_output, 512U) == ESP_OK);
    CHECK(memcmp(first_output, second_output, sizeof(first_output)) == 0);

    CHECK(siren_synth_deinit(&first) == ESP_OK);
    CHECK(first.room_buffer == NULL);
    CHECK(first.sample_rate == 0U);
    CHECK(siren_synth_deinit(&second) == ESP_OK);
    return true;
}

static bool test_distance_character_is_opt_in_and_reduces_output(void) {
    siren_synth_config_t config;
    siren_synth_config_defaults(&config, 48000U);
    config.duration_ms = 0U;
    CHECK(config.distance_mix == 0.0f);

    siren_synth_handle_t dry;
    siren_synth_handle_t distant;
    CHECK(siren_synth_init(&dry, &config) == ESP_OK);

    config.distance_mix = 1.0f;
    CHECK(siren_synth_init(&distant, &config) == ESP_OK);
    CHECK(distant.distance_filter_alpha > 0.0f &&
          distant.distance_filter_alpha < 1.0f);

    int16_t dry_output[2048];
    int16_t distant_output[2048];
    CHECK(siren_synth_render(&dry, dry_output, 2048U) == ESP_OK);
    CHECK(siren_synth_render(&distant, distant_output, 2048U) == ESP_OK);

    double dry_energy = 0.0;
    double distant_energy = 0.0;
    bool differs = false;
    for (size_t i = 0; i < 2048U; ++i) {
        CHECK(distant_output[i] >= -32767 && distant_output[i] <= 32767);
        dry_energy += (double)dry_output[i] * (double)dry_output[i];
        distant_energy += (double)distant_output[i] * (double)distant_output[i];
        differs |= dry_output[i] != distant_output[i];
    }
    CHECK(differs);
    CHECK(distant_energy < dry_energy);

    CHECK(siren_synth_deinit(&dry) == ESP_OK);
    CHECK(siren_synth_deinit(&distant) == ESP_OK);
    return true;
}

static bool test_distance_character_reduces_high_band_relative_to_low_band(void) {
    int16_t dry_output[FFT_SAMPLE_COUNT];
    int16_t filtered_output[FFT_SAMPLE_COUNT];
    CHECK(render_chopper_spectrum(0.0f, dry_output));
    CHECK(render_chopper_spectrum(1.0f, filtered_output));

    const double dry_low_power =
        fft_band_power(dry_output, 48000U, 250.0, 500.0);
    const double dry_high_power =
        fft_band_power(dry_output, 48000U, 5000.0, 7000.0);
    const double filtered_low_power =
        fft_band_power(filtered_output, 48000U, 250.0, 500.0);
    const double filtered_high_power =
        fft_band_power(filtered_output, 48000U, 5000.0, 7000.0);
    CHECK(dry_low_power > 0.0 && dry_high_power > 0.0);
    CHECK(filtered_low_power > 0.0 && filtered_high_power > 0.0);
    CHECK(filtered_high_power / filtered_low_power <
          0.1 * dry_high_power / dry_low_power);
    return true;
}

static bool test_invalid_arguments_and_chopper_nyquist_guard(void) {
    siren_synth_config_t config;
    siren_synth_config_defaults(&config, 48000U);
    siren_synth_handle_t handle;
    int16_t sample = 0;

    CHECK(siren_synth_init(NULL, &config) == ESP_ERR_INVALID_ARG);
    CHECK(siren_synth_init(&handle, NULL) == ESP_ERR_INVALID_ARG);
    CHECK(siren_synth_render(NULL, &sample, 1U) == ESP_ERR_INVALID_ARG);
    CHECK(siren_synth_render(&handle, NULL, 1U) == ESP_ERR_INVALID_ARG);
    CHECK(siren_synth_deinit(NULL) == ESP_ERR_INVALID_ARG);

    config.sample_rate = 7999U;
    CHECK(siren_synth_init(&handle, &config) == ESP_ERR_INVALID_ARG);

    siren_synth_config_defaults(&config, 8000U);
    config.source = SIREN_SOURCE_CHOPPER;
    config.peak_rpm = 30000.0f;
    config.rotor_ports_high = 64.0f;
    config.rotor_ports_low = 0.0f;
    config.vanes_high = 0.0f;
    config.vanes_low = 0.0f;
    config.undertone_ports = 0.0f;
    config.overtone_ports = 0.0f;
    config.mix_rotor_low = 0.0f;
    config.mix_vanes_high = 0.0f;
    config.mix_vanes_low = 0.0f;
    config.mix_undertone = 0.0f;
    config.mix_overtone = 0.0f;
    CHECK(siren_synth_init(&handle, &config) == ESP_ERR_INVALID_ARG);

    config.peak_rpm = 3000.0f;
    CHECK(siren_synth_init(&handle, &config) == ESP_OK);
    CHECK(siren_synth_deinit(&handle) == ESP_OK);
    return true;
}

static bool test_non_finite_dsp_parameters_are_rejected(void) {
    siren_synth_config_t config;
    siren_synth_config_defaults(&config, 48000U);
    siren_synth_handle_t handle;

    config.modulation_depth = NAN;
    CHECK(siren_synth_init(&handle, &config) == ESP_ERR_INVALID_ARG);

    siren_synth_config_defaults(&config, 48000U);
    config.sweep_end_hz = INFINITY;
    CHECK(siren_synth_init(&handle, &config) == ESP_ERR_INVALID_ARG);

    siren_synth_config_defaults(&config, 48000U);
    config.sweep_start_hz = NAN;
    CHECK(siren_synth_init(&handle, &config) == ESP_ERR_INVALID_ARG);

    siren_synth_config_defaults(&config, 48000U);
    config.rotor_hz = INFINITY;
    CHECK(siren_synth_init(&handle, &config) == ESP_ERR_INVALID_ARG);

    siren_synth_config_defaults(&config, 48000U);
    config.wobble_hz = INFINITY;
    CHECK(siren_synth_init(&handle, &config) == ESP_ERR_INVALID_ARG);

    siren_synth_config_defaults(&config, 48000U);
    config.attack_ms = INFINITY;
    CHECK(siren_synth_init(&handle, &config) == ESP_ERR_INVALID_ARG);

    siren_synth_config_defaults(&config, 48000U);
    config.release_ms = NAN;
    CHECK(siren_synth_init(&handle, &config) == ESP_ERR_INVALID_ARG);

    siren_synth_config_defaults(&config, 48000U);
    config.distance_mix = NAN;
    CHECK(siren_synth_init(&handle, &config) == ESP_ERR_INVALID_ARG);

    siren_synth_config_defaults(&config, 48000U);
    config.distance_mix = 1.01f;
    CHECK(siren_synth_init(&handle, &config) == ESP_ERR_INVALID_ARG);

    siren_synth_config_defaults(&config, 48000U);
    config.amplitude = INFINITY;
    CHECK(siren_synth_init(&handle, &config) == ESP_ERR_INVALID_ARG);

    siren_synth_config_defaults(&config, 8000U);
    config.sweep_end_hz = 4000.0f;
    CHECK(siren_synth_init(&handle, &config) == ESP_ERR_INVALID_ARG);

    siren_synth_config_defaults(&config, 8000U);
    config.rotor_hz = 4000.0f;
    CHECK(siren_synth_init(&handle, &config) == ESP_ERR_INVALID_ARG);
    return true;
}

int main(void) {
    const struct {
        const char *name;
        bool (*run)(void);
    } tests[] = {
        {"finite render is deterministic and zero-padded",
         test_finite_render_is_deterministic_and_zero_padded},
        {"wind-down reaches silence", test_wind_down_reaches_silence},
        {"effects are deterministic and deinit releases room",
         test_effects_are_deterministic_and_deinit_releases_room},
        {"distance character is opt-in and reduces output",
         test_distance_character_is_opt_in_and_reduces_output},
        {"distance character reduces high band relative to low band",
         test_distance_character_reduces_high_band_relative_to_low_band},
        {"invalid arguments and chopper Nyquist guard",
         test_invalid_arguments_and_chopper_nyquist_guard},
        {"non-finite DSP parameters are rejected",
         test_non_finite_dsp_parameters_are_rejected},
    };

    for (size_t i = 0; i < sizeof(tests) / sizeof(tests[0]); ++i) {
        if (!tests[i].run()) {
            fprintf(stderr, "FAILED: %s\n", tests[i].name);
            return 1;
        }
        printf("PASS: %s\n", tests[i].name);
    }
    return 0;
}
