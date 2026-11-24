#include "siren_synth.h"

#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static const float SIREN_BLADE_DUTY = 0.485f;
static const float SIREN_RESONATOR_HZ[SIREN_SYNTH_RESONATOR_COUNT] = {
    392.0f, 645.0f, 1165.0f, 1880.0f
};
static const float SIREN_RESONATOR_Q[SIREN_SYNTH_RESONATOR_COUNT] = {
    7.5f, 5.0f, 3.8f, 2.8f
};
static const float SIREN_RESONATOR_GAIN[SIREN_SYNTH_RESONATOR_COUNT] = {
    1.10f, 1.55f, 0.85f, 0.45f
};
static const float SIREN_ROOM_DELAYS_MS[SIREN_ROOM_COUNT][SIREN_SYNTH_REVERB_LINE_COUNT] = {
    {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f},
    {18.7f, 23.1f, 29.7f, 34.9f, 5.3f, 12.4f},
    {22.5f, 26.4f, 31.7f, 36.8f, 5.3f, 12.4f},
    {55.0f, 70.0f, 86.0f, 101.0f, 7.3f, 15.1f},
};

static inline float clampf(float value, float min_value, float max_value) {
    if (value < min_value) {
        return min_value;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
}

static void siren_biquad_set(siren_synth_handle_t *handle, size_t index,
                             float b0, float b1, float b2, float a0, float a1, float a2) {
    handle->radio_b0[index] = b0 / a0;
    handle->radio_b1[index] = b1 / a0;
    handle->radio_b2[index] = b2 / a0;
    handle->radio_a1[index] = a1 / a0;
    handle->radio_a2[index] = a2 / a0;
}

static void siren_radio_filter_init(siren_synth_handle_t *handle) {
    const float sample_rate = (float)handle->sample_rate;
    const float highpass_omega = 2.0f * (float)M_PI * 220.0f / sample_rate;
    const float highpass_cos = cosf(highpass_omega);
    const float highpass_alpha = sinf(highpass_omega) / (2.0f * 0.7071f);
    siren_biquad_set(handle, 0,
                     (1.0f + highpass_cos) * 0.5f, -(1.0f + highpass_cos),
                     (1.0f + highpass_cos) * 0.5f, 1.0f + highpass_alpha,
                     -2.0f * highpass_cos, 1.0f - highpass_alpha);

    const float lowpass_omega = 2.0f * (float)M_PI * 3800.0f / sample_rate;
    const float lowpass_cos = cosf(lowpass_omega);
    const float lowpass_alpha = sinf(lowpass_omega) / (2.0f * 0.7071f);
    siren_biquad_set(handle, 1,
                     (1.0f - lowpass_cos) * 0.5f, 1.0f - lowpass_cos,
                     (1.0f - lowpass_cos) * 0.5f, 1.0f + lowpass_alpha,
                     -2.0f * lowpass_cos, 1.0f - lowpass_alpha);

    const float peak_omega = 2.0f * (float)M_PI * 1200.0f / sample_rate;
    const float peak_alpha = sinf(peak_omega) / (2.0f * 5.0f);
    const float peak_gain = powf(10.0f, 5.0f / 40.0f);
    siren_biquad_set(handle, 2,
                     1.0f + peak_alpha * peak_gain, -2.0f * cosf(peak_omega),
                     1.0f - peak_alpha * peak_gain, 1.0f + peak_alpha / peak_gain,
                     -2.0f * cosf(peak_omega), 1.0f - peak_alpha / peak_gain);
}

static float siren_biquad_process(siren_synth_handle_t *handle, size_t index, float input) {
    const float output = handle->radio_b0[index] * input + handle->radio_d1[index];
    handle->radio_d1[index] = handle->radio_b1[index] * input -
                              handle->radio_a1[index] * output + handle->radio_d2[index];
    handle->radio_d2[index] = handle->radio_b2[index] * input -
                              handle->radio_a2[index] * output;
    return output;
}

static esp_err_t siren_room_init(siren_synth_handle_t *handle, float room_size) {
    if (handle->room == SIREN_ROOM_OFF || handle->room_mix <= 0.0f) {
        return ESP_OK;
    }

    size_t total_samples = 0;
    for (size_t line = 0; line < SIREN_SYNTH_REVERB_LINE_COUNT; ++line) {
        float delay_ms = SIREN_ROOM_DELAYS_MS[handle->room][line];
        if (line < 4U) {
            delay_ms *= 0.75f + room_size * 0.5f;
        }
        const uint32_t length = (uint32_t)(delay_ms * (float)handle->sample_rate / 1000.0f + 0.5f);
        if (length == 0U || total_samples > SIZE_MAX - length) {
            return ESP_ERR_INVALID_SIZE;
        }
        handle->room_line_lengths[line] = length;
        handle->room_line_offsets[line] = (uint32_t)total_samples;
        total_samples += length;
    }

    if (total_samples > SIZE_MAX / sizeof(float)) {
        return ESP_ERR_INVALID_SIZE;
    }
    handle->room_buffer = calloc(total_samples, sizeof(float));
    if (handle->room_buffer == NULL) {
        return ESP_ERR_NO_MEM;
    }
    handle->room_buffer_samples = total_samples;
    handle->room_comb_feedback = handle->room == SIREN_ROOM_NIGHT ? 0.82f :
                                 handle->room == SIREN_ROOM_BUNKER ? 0.72f : 0.48f;
    handle->room_allpass_feedback = 0.58f;
    return ESP_OK;
}

static float siren_room_process(siren_synth_handle_t *handle, float input) {
    if (handle->room_buffer == NULL) {
        return input;
    }

    float comb_sum = 0.0f;
    for (size_t line = 0; line < 4U; ++line) {
        const uint32_t position = handle->room_line_positions[line];
        const size_t index = handle->room_line_offsets[line] + position;
        const float delayed = handle->room_buffer[index];
        handle->room_buffer[index] = input + delayed * handle->room_comb_feedback;
        handle->room_line_positions[line] =
            position + 1U == handle->room_line_lengths[line] ? 0U : position + 1U;
        comb_sum += delayed;
    }

    float output = comb_sum * 0.25f;
    for (size_t line = 4U; line < SIREN_SYNTH_REVERB_LINE_COUNT; ++line) {
        const uint32_t position = handle->room_line_positions[line];
        const size_t index = handle->room_line_offsets[line] + position;
        const float delayed = handle->room_buffer[index];
        handle->room_buffer[index] = output + delayed * handle->room_allpass_feedback;
        handle->room_line_positions[line] =
            position + 1U == handle->room_line_lengths[line] ? 0U : position + 1U;
        output = delayed - output * handle->room_allpass_feedback;
    }

    return input * (1.0f - handle->room_mix) + output * handle->room_mix;
}

static inline float siren_sin_cycles(const siren_synth_handle_t *handle, float phase_cycles) {
    if (phase_cycles < 0.0f) {
        phase_cycles += 1.0f;
    } else if (phase_cycles >= 1.0f) {
        phase_cycles -= 1.0f;
    }

    const float table_position = phase_cycles * (float)SIREN_SYNTH_PHASE_TABLE_SIZE;
    const size_t table_index = (size_t)table_position;
    const size_t next_table_index = (table_index + 1U) & (SIREN_SYNTH_PHASE_TABLE_SIZE - 1U);
    const float fraction = table_position - (float)table_index;
    const float current = handle->phase_wavetable[table_index];
    return current + (handle->phase_wavetable[next_table_index] - current) * fraction;
}

static inline float siren_advance_phase(float phase_cycles, float phase_step) {
    phase_cycles += phase_step;
    if (phase_cycles >= 1.0f) {
        phase_cycles -= 1.0f;
    }
    return phase_cycles;
}

static float siren_profile_frequency(siren_synth_handle_t *handle) {
    const float wail_phase = handle->profile_phase_cycles;
    const float center_hz = (handle->sweep_start_hz + handle->sweep_end_hz) * 0.5f;
    const float swing_hz = (handle->sweep_end_hz - handle->sweep_start_hz) * 0.5f;
    const float wail_hz = center_hz + swing_hz * siren_sin_cycles(handle, wail_phase - 0.25f);

    switch (handle->pattern) {
    case SIREN_PATTERN_WARNING:
        return wail_hz;
    case SIREN_PATTERN_ALL_CLEAR:
        return handle->sweep_end_hz;
    case SIREN_PATTERN_CATASTROPHE: {
        const float pattern_time = handle->pattern_elapsed_s;
        if ((pattern_time >= 12.0f && pattern_time < 24.0f) ||
            (pattern_time >= 36.0f && pattern_time < 48.0f) ||
            pattern_time >= 60.0f) {
            return 0.0f;
        }
        return wail_hz;
    }
    case SIREN_PATTERN_TEST:
    default:
        return center_hz + swing_hz * siren_sin_cycles(handle, wail_phase) +
               0.03f * center_hz * siren_sin_cycles(handle, handle->phase_wobble);
    }
}

static float siren_resonator_process(const siren_synth_handle_t *handle,
                                     size_t index,
                                     float input,
                                     float *d1,
                                     float *d2) {
    const float output = handle->resonator_b0[index] * input + *d1;
    *d1 = -handle->resonator_a1[index] * output + *d2;
    *d2 = handle->resonator_b2[index] * input - handle->resonator_a2[index] * output;
    return output;
}

static float siren_chopper_render_sample(siren_synth_handle_t *handle, float target_hz) {
    const float sweep_range = handle->sweep_end_hz - handle->sweep_start_hz;
    const float speed_fraction = clampf((target_hz - handle->sweep_start_hz) / sweep_range,
                                        0.0f, 1.0f);
    float target_rpm = speed_fraction * handle->peak_rpm;
    if (handle->thermal_enabled) {
        target_rpm *= 1.0f - 0.016f * handle->thermal_rise;
    }
    const float time_constant = target_rpm > handle->motor_rpm
        ? handle->motor_rise_s : handle->motor_fall_s;
    handle->motor_rpm += (target_rpm - handle->motor_rpm) /
                         (time_constant * (float)handle->sample_rate);

    const float flutter = handle->jitter_enabled
        ? 1.0f + 0.004f * siren_sin_cycles(handle, handle->flutter_phase_cycles) +
          0.0011f * siren_sin_cycles(handle, handle->flutter2_phase_cycles)
        : 1.0f;
    handle->flutter_phase_cycles = siren_advance_phase(
        handle->flutter_phase_cycles, 8.3f / (float)handle->sample_rate);
    handle->flutter2_phase_cycles = siren_advance_phase(
        handle->flutter2_phase_cycles, 17.1f / (float)handle->sample_rate);

    static const uint8_t multi_tone_mask = 0x0fU;
    static const uint8_t multi_chopper_mask = 0x3fU;
    const uint8_t active_mask = handle->chopper_config == SIREN_CHOPPER_SINGLE
        ? 0x05U
        : handle->chopper_config == SIREN_CHOPPER_MULTI_TONE
            ? multi_tone_mask : multi_chopper_mask;
    float output = 0.0f;
    float total_mix = 0.0f;
    const float effective_rpm = handle->motor_rpm * flutter;
    for (size_t oscillator = 0; oscillator < 6U; ++oscillator) {
        const float ports = handle->oscillator_ports[oscillator];
        const float mix = handle->oscillator_mix[oscillator];
        if ((active_mask & (1U << oscillator)) == 0U || ports <= 0.0f || mix <= 0.0f) {
            continue;
        }

        const float frequency_hz = ports * effective_rpm / 60.0f;
        handle->oscillator_phase[oscillator] = siren_advance_phase(
            handle->oscillator_phase[oscillator], frequency_hz / (float)handle->sample_rate);
        output += siren_sin_cycles(handle, handle->oscillator_phase[oscillator]) * mix;
        total_mix += mix;
    }

    return total_mix > 0.0f ? output / total_mix : 0.0f;
}

static float siren_envelope(const siren_synth_handle_t *handle, uint64_t sample_index) {
    const uint64_t total = handle->total_samples;
    if (total == 0U) {
        return 1.0f;
    }

    const float attack_samples = handle->attack_ms * (float)handle->sample_rate / 1000.0f;
    const float release_samples = handle->release_ms * (float)handle->sample_rate / 1000.0f;
    const float time_index = (float)sample_index;

    if (time_index < attack_samples) {
        return clampf(time_index / attack_samples, 0.0f, 1.0f);
    }
    if (time_index > (float)total - release_samples) {
        const float release_index = (float)total - time_index;
        return clampf(release_index / release_samples, 0.0f, 1.0f);
    }
    return 1.0f;
}

void siren_synth_config_defaults(siren_synth_config_t *config, uint32_t sample_rate) {
    if (config == NULL) {
        return;
    }

    *config = (siren_synth_config_t) {
        .sample_rate = sample_rate,
        .duration_ms = 3000U,
        .amplitude = 0.62f,
        .sweep_start_hz = 210.0f,
        .sweep_end_hz = 420.0f,
        .rotor_hz = 9.0f,
        .modulation_depth = 0.26f,
        .wobble_hz = 2.2f,
        .attack_ms = 50.0f,
        .release_ms = 180.0f,
        .pattern = SIREN_PATTERN_WARNING,
        .room = SIREN_ROOM_OFF,
        .room_size = 0.5f,
        .room_mix = 0.25f,
        .radio_enabled = false,
        .radio_mix = 0.5f,
        .distance_mix = 0.0f,
        .thermal_enabled = true,
        .jitter_enabled = true,
        .source = SIREN_SOURCE_E57,
        .chopper_config = SIREN_CHOPPER_MULTI_CHOPPER,
        .peak_rpm = 3450.0f,
        .rotor_ports_high = 12.0f,
        .rotor_ports_low = 10.0f,
        .vanes_high = 5.0f,
        .vanes_low = 4.0f,
        .undertone_ports = 2.0f,
        .overtone_ports = 3.0f,
        .motor_rise_s = 1.55f,
        .motor_fall_s = 5.80f,
        .mix_rotor_high = 1.0f,
        .mix_rotor_low = 0.95f,
        .mix_vanes_high = 0.45f,
        .mix_vanes_low = 0.40f,
        .mix_undertone = 0.30f,
        .mix_overtone = 0.50f,
    };
}

esp_err_t siren_synth_init(siren_synth_handle_t *handle, const siren_synth_config_t *config) {
    if (handle == NULL || config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (config->sample_rate < 8000U || config->sample_rate > 192000U) {
        return ESP_ERR_INVALID_ARG;
    }
    if (config->pattern < SIREN_PATTERN_WARNING || config->pattern >= SIREN_PATTERN_COUNT ||
        config->room < SIREN_ROOM_OFF || config->room >= SIREN_ROOM_COUNT ||
        config->source < SIREN_SOURCE_E57 || config->source >= SIREN_SOURCE_COUNT ||
        config->chopper_config < SIREN_CHOPPER_SINGLE ||
        config->chopper_config >= SIREN_CHOPPER_CONFIG_COUNT ||
        !isfinite(config->amplitude) ||
        !isfinite(config->sweep_start_hz) ||
        !isfinite(config->sweep_end_hz) ||
        !isfinite(config->rotor_hz) ||
        !isfinite(config->modulation_depth) ||
        !isfinite(config->wobble_hz) ||
        !isfinite(config->attack_ms) ||
        !isfinite(config->release_ms) ||
        !isfinite(config->room_size) || config->room_size < 0.0f || config->room_size > 1.0f ||
        !isfinite(config->room_mix) || config->room_mix < 0.0f || config->room_mix > 1.0f ||
        !isfinite(config->radio_mix) || config->radio_mix < 0.0f || config->radio_mix > 1.0f ||
        !isfinite(config->distance_mix) || config->distance_mix < 0.0f ||
        config->distance_mix > 1.0f) {
        return ESP_ERR_INVALID_ARG;
    }
    const float sweep_start_hz = config->sweep_start_hz > 0.0f
        ? config->sweep_start_hz : 420.0f;
    const float sweep_end_hz = config->sweep_end_hz > 0.0f
        ? config->sweep_end_hz : 1800.0f;
    const float rotor_hz = config->rotor_hz > 0.0f ? config->rotor_hz : 7.0f;
    const float wobble_hz = config->wobble_hz > 0.0f ? config->wobble_hz : 1.5f;
    const float nyquist_hz = (float)config->sample_rate * 0.5f;
    if (sweep_end_hz <= sweep_start_hz || sweep_end_hz >= nyquist_hz ||
        rotor_hz >= nyquist_hz || wobble_hz >= nyquist_hz) {
        return ESP_ERR_INVALID_ARG;
    }

    if (config->source == SIREN_SOURCE_CHOPPER &&
        (!isfinite(config->peak_rpm) || config->peak_rpm < 100.0f ||
         config->peak_rpm > 30000.0f ||
         !isfinite(config->motor_rise_s) || config->motor_rise_s < 0.05f ||
         config->motor_rise_s > 30.0f ||
         !isfinite(config->motor_fall_s) || config->motor_fall_s < 0.05f ||
         config->motor_fall_s > 60.0f ||
         !isfinite(config->rotor_ports_high) || config->rotor_ports_high < 0.0f ||
         config->rotor_ports_high > 64.0f ||
         !isfinite(config->rotor_ports_low) || config->rotor_ports_low < 0.0f ||
         config->rotor_ports_low > 64.0f ||
         !isfinite(config->vanes_high) || config->vanes_high < 0.0f ||
         config->vanes_high > 64.0f ||
         !isfinite(config->vanes_low) || config->vanes_low < 0.0f ||
         config->vanes_low > 64.0f ||
         !isfinite(config->undertone_ports) || config->undertone_ports < 0.0f ||
         config->undertone_ports > 64.0f ||
         !isfinite(config->overtone_ports) || config->overtone_ports < 0.0f ||
         config->overtone_ports > 64.0f ||
         !isfinite(config->mix_rotor_high) || config->mix_rotor_high < 0.0f ||
         config->mix_rotor_high > 1.0f ||
         !isfinite(config->mix_rotor_low) || config->mix_rotor_low < 0.0f ||
         config->mix_rotor_low > 1.0f ||
         !isfinite(config->mix_vanes_high) || config->mix_vanes_high < 0.0f ||
         config->mix_vanes_high > 1.0f ||
         !isfinite(config->mix_vanes_low) || config->mix_vanes_low < 0.0f ||
         config->mix_vanes_low > 1.0f ||
         !isfinite(config->mix_undertone) || config->mix_undertone < 0.0f ||
         config->mix_undertone > 1.0f ||
         !isfinite(config->mix_overtone) || config->mix_overtone < 0.0f ||
         config->mix_overtone > 1.0f)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (config->source == SIREN_SOURCE_CHOPPER) {
        const float counts[6] = {
            config->rotor_ports_high, config->rotor_ports_low,
            config->vanes_high, config->vanes_low,
            config->undertone_ports, config->overtone_ports,
        };
        const float mixes[6] = {
            config->mix_rotor_high, config->mix_rotor_low,
            config->mix_vanes_high, config->mix_vanes_low,
            config->mix_undertone, config->mix_overtone,
        };
        const uint8_t active_mask = config->chopper_config == SIREN_CHOPPER_SINGLE
            ? 0x05U
            : config->chopper_config == SIREN_CHOPPER_MULTI_TONE
                ? 0x0fU : 0x3fU;
        for (size_t oscillator = 0; oscillator < 6U; ++oscillator) {
            if ((active_mask & (1U << oscillator)) != 0U &&
                counts[oscillator] > 0.0f && mixes[oscillator] > 0.0f &&
                config->peak_rpm * counts[oscillator] / 60.0f >=
                    (float)config->sample_rate * 0.5f) {
                return ESP_ERR_INVALID_ARG;
            }
        }
    }

    memset(handle, 0, sizeof(*handle));
    handle->sample_rate = config->sample_rate;
    handle->total_samples = (config->duration_ms > 0U)
        ? ((uint64_t)config->duration_ms * config->sample_rate / 1000U)
        : 0U;
    handle->samples_rendered = 0U;
    handle->amplitude = (config->amplitude > 0.0f) ? config->amplitude : 0.7f;
    handle->sweep_start_hz = (config->sweep_start_hz > 0.0f) ? config->sweep_start_hz : 420.0f;
    handle->sweep_end_hz = (config->sweep_end_hz > 0.0f) ? config->sweep_end_hz : 1800.0f;
    handle->rotor_hz = (config->rotor_hz > 0.0f) ? config->rotor_hz : 7.0f;
    handle->modulation_depth = clampf(config->modulation_depth, 0.0f, 1.0f);
    handle->wobble_hz = (config->wobble_hz > 0.0f) ? config->wobble_hz : 1.5f;
    handle->attack_ms = (config->attack_ms > 0.0f) ? config->attack_ms : 40.0f;
    handle->release_ms = (config->release_ms > 0.0f) ? config->release_ms : 120.0f;
    handle->phase = 0.0f;
    handle->phase_mod = 0.0f;
    handle->phase_rotor = 0.0f;
    handle->phase_wobble = 0.0f;
    handle->pattern = config->pattern;
    handle->room = config->room;
    handle->room_mix = config->room_mix;
    handle->radio_enabled = config->radio_enabled;
    handle->radio_mix = config->radio_mix;
    handle->distance_mix = config->distance_mix;
    if (handle->distance_mix > 0.0f) {
        const float cutoff_hz = 18000.0f - 17100.0f * handle->distance_mix;
        const float nyquist_limit_hz = (float)handle->sample_rate * 0.45f;
        const float effective_cutoff_hz = cutoff_hz < nyquist_limit_hz
            ? cutoff_hz : nyquist_limit_hz;
        handle->distance_filter_alpha = 1.0f - expf(
            -2.0f * (float)M_PI * effective_cutoff_hz / (float)handle->sample_rate);
    }
    handle->thermal_enabled = config->thermal_enabled;
    handle->jitter_enabled = config->jitter_enabled;
    handle->source = config->source;
    handle->chopper_config = config->chopper_config;
    handle->peak_rpm = config->peak_rpm;
    handle->motor_rise_s = config->motor_rise_s;
    handle->motor_fall_s = config->motor_fall_s;
    handle->oscillator_ports[0] = config->rotor_ports_high;
    handle->oscillator_ports[1] = config->rotor_ports_low;
    handle->oscillator_ports[2] = config->vanes_high;
    handle->oscillator_ports[3] = config->vanes_low;
    handle->oscillator_ports[4] = config->undertone_ports;
    handle->oscillator_ports[5] = config->overtone_ports;
    handle->oscillator_mix[0] = config->mix_rotor_high;
    handle->oscillator_mix[1] = config->mix_rotor_low;
    handle->oscillator_mix[2] = config->mix_vanes_high;
    handle->oscillator_mix[3] = config->mix_vanes_low;
    handle->oscillator_mix[4] = config->mix_undertone;
    handle->oscillator_mix[5] = config->mix_overtone;
    handle->radio_noise_state = 0x9e3779b9U;

    siren_radio_filter_init(handle);
    const esp_err_t room_result = siren_room_init(handle, config->room_size);
    if (room_result != ESP_OK) {
        return room_result;
    }

    for (size_t harmonic = 0; harmonic < SIREN_SYNTH_HARMONIC_COUNT; ++harmonic) {
        const float order = (float)(harmonic + 1U);
        handle->harmonic_coefficients[harmonic] =
            2.0f * sinf(order * (float)M_PI * SIREN_BLADE_DUTY) / (order * (float)M_PI);
    }

    for (size_t sample = 0; sample < SIREN_SYNTH_WAVETABLE_SIZE; ++sample) {
        const float phase = 2.0f * (float)M_PI * (float)sample /
                            (float)SIREN_SYNTH_WAVETABLE_SIZE;
        float rotor_wave = 0.0f;
        for (size_t harmonic = 0; harmonic < SIREN_SYNTH_HARMONIC_COUNT; ++harmonic) {
            const float order = (float)(harmonic + 1U);
            rotor_wave += handle->harmonic_coefficients[harmonic] * cosf(order * phase);
        }
        handle->rotor_wavetable[sample] = rotor_wave;
    }

    for (size_t sample = 0; sample < SIREN_SYNTH_PHASE_TABLE_SIZE; ++sample) {
        const float phase = 2.0f * (float)M_PI * (float)sample /
                            (float)SIREN_SYNTH_PHASE_TABLE_SIZE;
        handle->phase_wavetable[sample] = sinf(phase);
    }

    for (size_t resonator = 0; resonator < SIREN_SYNTH_RESONATOR_COUNT; ++resonator) {
        const float omega = 2.0f * (float)M_PI * SIREN_RESONATOR_HZ[resonator] /
                            (float)handle->sample_rate;
        const float alpha = sinf(omega) / (2.0f * SIREN_RESONATOR_Q[resonator]);
        const float a0 = 1.0f + alpha;
        handle->resonator_b0[resonator] = alpha / a0;
        handle->resonator_b2[resonator] = -alpha / a0;
        handle->resonator_a1[resonator] = -2.0f * cosf(omega) / a0;
        handle->resonator_a2[resonator] = (1.0f - alpha) / a0;
    }

    return ESP_OK;
}

esp_err_t siren_synth_begin_wind_down(siren_synth_handle_t *handle, uint32_t duration_ms) {
    if (handle == NULL || handle->sample_rate == 0U || duration_ms == 0U) {
        return ESP_ERR_INVALID_ARG;
    }
    float start_hz = siren_profile_frequency(handle);
    if (handle->source == SIREN_SOURCE_CHOPPER && handle->peak_rpm > 0.0f) {
        const float speed_fraction = clampf(handle->motor_rpm / handle->peak_rpm, 0.0f, 1.0f);
        start_hz = handle->sweep_start_hz +
                   speed_fraction * (handle->sweep_end_hz - handle->sweep_start_hz);
    } else if (handle->source == SIREN_SOURCE_E57 && handle->motor_frequency_hz > 0.0f) {
        start_hz = handle->motor_frequency_hz;
    }

    handle->wind_down_start_hz = start_hz;
    handle->wind_down_total_samples =
        (uint64_t)duration_ms * handle->sample_rate / 1000U;
    if (handle->wind_down_total_samples == 0U) {
        return ESP_ERR_INVALID_ARG;
    }
    handle->wind_down_samples_rendered = 0U;
    handle->total_samples = 0U;
    return ESP_OK;
}

esp_err_t siren_synth_render(siren_synth_handle_t *handle, int16_t *buffer, size_t samples) {
    if (handle == NULL || buffer == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (samples == 0U) {
        return ESP_OK;
    }

    const uint64_t render_limit = (handle->total_samples > 0U)
        ? (handle->total_samples - handle->samples_rendered)
        : UINT64_MAX;
    const size_t usable_samples = (render_limit < samples) ? (size_t)render_limit : samples;

    for (size_t i = 0; i < usable_samples; ++i) {
        const uint64_t sample_index = handle->samples_rendered + i;
        const bool winding_down = handle->wind_down_total_samples > 0U;
        const uint64_t wind_down_sample = handle->wind_down_samples_rendered + i;
        const float wind_down_progress = winding_down
            ? clampf((float)wind_down_sample / (float)handle->wind_down_total_samples, 0.0f, 1.0f)
            : 0.0f;
        const float wind_down_remaining = 1.0f - wind_down_progress;
        float target_hz = winding_down
            ? handle->wind_down_start_hz * wind_down_remaining * wind_down_remaining
            : siren_profile_frequency(handle);
        handle->profile_phase_cycles = siren_advance_phase(
            handle->profile_phase_cycles, 1.0f / (12.0f * (float)handle->sample_rate));
        handle->pattern_elapsed_s += 1.0f / (float)handle->sample_rate;
        if (handle->pattern_elapsed_s >= 100.0f) {
            handle->pattern_elapsed_s -= 100.0f;
        }
        if (handle->thermal_enabled) {
            const float thermal_step = 1.0f / (290.0f * (float)handle->sample_rate);
            if (target_hz > 0.0f) {
                handle->thermal_rise += (1.0f - handle->thermal_rise) * thermal_step;
            } else {
                handle->thermal_rise -= handle->thermal_rise /
                                        (600.0f * (float)handle->sample_rate);
            }
            if (handle->source == SIREN_SOURCE_E57) {
                target_hz *= 1.0f - 0.016f * handle->thermal_rise;
            }
        }
        handle->phase_wobble = siren_advance_phase(
            handle->phase_wobble, handle->wobble_hz / (float)handle->sample_rate);

        float composite;
        if (handle->source == SIREN_SOURCE_CHOPPER) {
            composite = siren_chopper_render_sample(handle, target_hz);
        } else {
            const float time_constant =
                target_hz > handle->motor_frequency_hz ? 1.55f : 5.80f;
            handle->motor_frequency_hz +=
                (target_hz - handle->motor_frequency_hz) /
                time_constant / (float)handle->sample_rate;

            const float flutter = handle->jitter_enabled
                ? 1.0f + 0.004f * siren_sin_cycles(handle, handle->flutter_phase_cycles) +
                  0.0011f * siren_sin_cycles(handle, handle->flutter2_phase_cycles)
                : 1.0f;
            handle->flutter_phase_cycles = siren_advance_phase(
                handle->flutter_phase_cycles, 8.3f / (float)handle->sample_rate);
            handle->flutter2_phase_cycles = siren_advance_phase(
                handle->flutter2_phase_cycles, 17.1f / (float)handle->sample_rate);

            const float rotor_hz = handle->motor_frequency_hz * flutter;
            handle->rotor_phase_cycles = siren_advance_phase(
                handle->rotor_phase_cycles, rotor_hz / (float)handle->sample_rate);

            const float table_position =
                handle->rotor_phase_cycles * (float)SIREN_SYNTH_WAVETABLE_SIZE;
            const size_t table_index = (size_t)table_position;
            const size_t next_table_index =
                (table_index + 1U) & (SIREN_SYNTH_WAVETABLE_SIZE - 1U);
            const float table_fraction = table_position - (float)table_index;
            const float rotor_wave = handle->rotor_wavetable[table_index] +
                (handle->rotor_wavetable[next_table_index] -
                 handle->rotor_wavetable[table_index]) * table_fraction;

            handle->cogging_phase_cycles = siren_advance_phase(
                handle->cogging_phase_cycles, 280.0f / (float)handle->sample_rate);
            handle->slip_phase_cycles = siren_advance_phase(
                handle->slip_phase_cycles, 3.33f / (float)handle->sample_rate);
            handle->mains_phase_cycles = siren_advance_phase(
                handle->mains_phase_cycles, 100.0f / (float)handle->sample_rate);
            handle->mains2_phase_cycles = siren_advance_phase(
                handle->mains2_phase_cycles, 200.0f / (float)handle->sample_rate);

            handle->phase_mod = siren_advance_phase(
                handle->phase_mod, handle->rotor_hz / (float)handle->sample_rate);
            const float motor_envelope =
                clampf(handle->motor_frequency_hz / 420.0f, 0.0f, 1.0f);
            composite = rotor_wave *
                (0.32f + 0.022f * siren_sin_cycles(handle, handle->cogging_phase_cycles));
            composite += siren_sin_cycles(handle, handle->mains_phase_cycles) *
                         0.055f * motor_envelope * motor_envelope;
            composite += siren_sin_cycles(handle, handle->mains2_phase_cycles) *
                         0.022f * motor_envelope * motor_envelope;
            composite *= 1.0f +
                0.017f * siren_sin_cycles(handle, handle->slip_phase_cycles);
            composite *= 1.0f + handle->modulation_depth *
                         siren_sin_cycles(handle, handle->phase_mod);
            composite += 0.025f * siren_sin_cycles(handle, handle->phase_wobble);

            float resonant = 0.0f;
            for (size_t r = 0; r < SIREN_SYNTH_RESONATOR_COUNT; ++r) {
                resonant += siren_resonator_process(handle, r, composite,
                                                     &handle->resonator_d1[r],
                                                     &handle->resonator_d2[r]) *
                            SIREN_RESONATOR_GAIN[r];
            }
            composite = composite * 0.32f + resonant;
        }
        if (handle->radio_enabled) {
            handle->radio_noise_state = handle->radio_noise_state * 1664525U + 1013904223U;
            const float noise = ((float)(handle->radio_noise_state >> 8) / 8388608.0f - 1.0f) * 0.004f;
            float radio = composite + noise;
            for (size_t filter = 0; filter < SIREN_SYNTH_RADIO_FILTER_COUNT; ++filter) {
                radio = siren_biquad_process(handle, filter, radio);
            }
            composite += (radio - composite) * handle->radio_mix;
        }
        composite = siren_room_process(handle, composite);
        if (handle->distance_mix > 0.0f) {
            handle->distance_filter_state += handle->distance_filter_alpha *
                (composite - handle->distance_filter_state);
            composite += (handle->distance_filter_state - composite) *
                         handle->distance_mix;
            composite *= 1.0f - 0.75f * handle->distance_mix;
        }
        composite = clampf(composite, -1.0f, 1.0f);
        const float envelope = siren_envelope(handle, sample_index);
        const float wind_down_gain = winding_down ? wind_down_remaining : 1.0f;
        const float output = clampf(
            composite * envelope * handle->amplitude * wind_down_gain, -1.0f, 1.0f);

        buffer[i] = (int16_t)(output * 32767.0f);
    }

    for (size_t i = usable_samples; i < samples; ++i) {
        buffer[i] = 0;
    }

    handle->samples_rendered += usable_samples;
    if (handle->wind_down_total_samples > 0U) {
        handle->wind_down_samples_rendered += usable_samples;
    }
    return ESP_OK;
}

esp_err_t siren_synth_deinit(siren_synth_handle_t *handle) {
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    free(handle->room_buffer);
    memset(handle, 0, sizeof(*handle));
    return ESP_OK;
}
