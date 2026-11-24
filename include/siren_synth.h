#pragma once

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#define SIREN_SYNTH_HARMONIC_COUNT 26
#define SIREN_SYNTH_RESONATOR_COUNT 4
#define SIREN_SYNTH_WAVETABLE_SIZE 1024
#define SIREN_SYNTH_PHASE_TABLE_SIZE 256
#define SIREN_SYNTH_REVERB_LINE_COUNT 6
#define SIREN_SYNTH_RADIO_FILTER_COUNT 3

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SIREN_PATTERN_WARNING = 0,
    SIREN_PATTERN_ALL_CLEAR = 1,
    SIREN_PATTERN_CATASTROPHE = 2,
    SIREN_PATTERN_TEST = 3,
    SIREN_PATTERN_COUNT = 4,
} siren_pattern_t;

typedef enum {
    SIREN_ROOM_OFF = 0,
    SIREN_ROOM_OUTDOOR,
    SIREN_ROOM_BUNKER,
    SIREN_ROOM_NIGHT,
    SIREN_ROOM_COUNT,
} siren_room_t;

typedef enum {
    SIREN_SOURCE_E57 = 0,
    SIREN_SOURCE_CHOPPER,
    SIREN_SOURCE_COUNT,
} siren_source_t;

typedef enum {
    SIREN_CHOPPER_SINGLE = 0,
    SIREN_CHOPPER_MULTI_TONE,
    SIREN_CHOPPER_MULTI_CHOPPER,
    SIREN_CHOPPER_CONFIG_COUNT,
} siren_chopper_config_t;

typedef struct {
    uint32_t sample_rate;
    uint32_t duration_ms;
    float amplitude;
    float sweep_start_hz;
    float sweep_end_hz;
    float rotor_hz;
    float modulation_depth;
    float wobble_hz;
    float attack_ms;
    float release_ms;
    siren_pattern_t pattern;
    siren_room_t room;
    float room_size;
    float room_mix;
    bool radio_enabled;
    float radio_mix;
    float distance_mix; /* Stylized effect amount; not physical distance. */
    bool thermal_enabled;
    bool jitter_enabled;
    siren_source_t source;
    siren_chopper_config_t chopper_config;
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
} siren_synth_config_t;

typedef struct {
    uint32_t sample_rate;
    uint64_t total_samples;
    uint64_t samples_rendered;
    uint64_t wind_down_total_samples;
    uint64_t wind_down_samples_rendered;
    float wind_down_start_hz;
    float amplitude;
    float sweep_start_hz;
    float sweep_end_hz;
    float rotor_hz;
    float modulation_depth;
    float wobble_hz;
    float attack_ms;
    float release_ms;
    float phase;
    float phase_mod;
    float phase_rotor;
    float phase_wobble;
    siren_pattern_t pattern;
    siren_room_t room;
    float room_mix;
    bool radio_enabled;
    float radio_mix;
    float distance_mix;
    float distance_filter_alpha;
    float distance_filter_state;
    bool thermal_enabled;
    bool jitter_enabled;
    siren_source_t source;
    siren_chopper_config_t chopper_config;
    float peak_rpm;
    float motor_rpm;
    float motor_rise_s;
    float motor_fall_s;
    float oscillator_ports[6];
    float oscillator_mix[6];
    float oscillator_phase[6];
    float motor_frequency_hz;
    float thermal_rise;
    float profile_phase_cycles;
    float pattern_elapsed_s;
    float rotor_phase_cycles;
    float flutter_phase_cycles;
    float flutter2_phase_cycles;
    float cogging_phase_cycles;
    float slip_phase_cycles;
    float mains_phase_cycles;
    float mains2_phase_cycles;
    float harmonic_coefficients[SIREN_SYNTH_HARMONIC_COUNT];
    float rotor_wavetable[SIREN_SYNTH_WAVETABLE_SIZE];
    float phase_wavetable[SIREN_SYNTH_PHASE_TABLE_SIZE];
    float resonator_b0[SIREN_SYNTH_RESONATOR_COUNT];
    float resonator_b2[SIREN_SYNTH_RESONATOR_COUNT];
    float resonator_a1[SIREN_SYNTH_RESONATOR_COUNT];
    float resonator_a2[SIREN_SYNTH_RESONATOR_COUNT];
    float resonator_d1[SIREN_SYNTH_RESONATOR_COUNT];
    float resonator_d2[SIREN_SYNTH_RESONATOR_COUNT];
    float radio_b0[SIREN_SYNTH_RADIO_FILTER_COUNT];
    float radio_b1[SIREN_SYNTH_RADIO_FILTER_COUNT];
    float radio_b2[SIREN_SYNTH_RADIO_FILTER_COUNT];
    float radio_a1[SIREN_SYNTH_RADIO_FILTER_COUNT];
    float radio_a2[SIREN_SYNTH_RADIO_FILTER_COUNT];
    float radio_d1[SIREN_SYNTH_RADIO_FILTER_COUNT];
    float radio_d2[SIREN_SYNTH_RADIO_FILTER_COUNT];
    uint32_t radio_noise_state;
    float *room_buffer;
    size_t room_buffer_samples;
    uint32_t room_line_lengths[SIREN_SYNTH_REVERB_LINE_COUNT];
    uint32_t room_line_offsets[SIREN_SYNTH_REVERB_LINE_COUNT];
    uint32_t room_line_positions[SIREN_SYNTH_REVERB_LINE_COUNT];
    float room_comb_feedback;
    float room_allpass_feedback;
} siren_synth_handle_t;

/* Initialize a complete factory configuration for the requested sample rate. */
void siren_synth_config_defaults(siren_synth_config_t *config, uint32_t sample_rate);
/*
 * Rejects non-finite parameters and control frequencies at or above Nyquist.
 * Pair each successful init with deinit; reinitialize only after deinit.
 */
esp_err_t siren_synth_init(siren_synth_handle_t *handle, const siren_synth_config_t *config);
esp_err_t siren_synth_begin_wind_down(siren_synth_handle_t *handle, uint32_t duration_ms);
esp_err_t siren_synth_render(siren_synth_handle_t *handle, int16_t *buffer, size_t samples);
esp_err_t siren_synth_deinit(siren_synth_handle_t *handle);

#ifdef __cplusplus
}
#endif
