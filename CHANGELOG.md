# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [Unreleased]

### Added
- Board-independent PCM renderer for one E57-inspired electromechanical siren
  model, with warning, all-clear, catastrophe, and test patterns.
- Interactive Waveshare ESP32-P4 development example with ES8311 playback.
- `siren benchmark` in the development console for per-block DSP timing.
- Configurable room, radio, thermal, and rotor-jitter effects in the synthesizer
  core, with runtime controls in `console_siren_dev`.
- Experimental RPM-driven chopper source with selectable single/multi-tone/
  multi-chopper component groups and independently adjustable rotor/vane mixes.
- Persistent NVS storage for the development console's active settings and
  volume, with explicit save/load/reset commands and begin/commit/abort
  transactions for batching multi-setting updates.
- A device-enforced safety timeout for loop playback (`siren loop <seconds>`),
  with `0` retaining an explicitly unlimited mode.
- A standalone browser console in `tools-web/siren_console.html` using Web Serial,
  live device state, local presets, and separate Cancel and Brake controls.
- `siren cancel` for a motor-fall-derived wind-down and `siren brake`/`siren stop`
  for a fast release that can interrupt it.
- Native C renderer regressions and standalone browser-console checks.
- An independent, optional distance-character effect with a normalized control
  and no physical-distance or SPL calibration claim.
- Versioned preset catalogs now include distance character and migrate older
  preset schemas; incompatible NVS blobs are not automatically migrated.

### Changed
- Stopping held/loop playback fades to silence over the configured release time
  before the firmware reports playback idle.
- The development-console tool identity is version 3 for the distance-character
  setting, and chopper controls are gated by the selected source model.
- Chopper initialization rejects enabled oscillators whose maximum fundamental
  reaches or exceeds Nyquist rather than silently accepting aliased settings.
- Renderer initialization rejects non-finite DSP parameters and sweep,
  modulation, or wobble frequencies at or above Nyquist.
- Browser playback-stop controls remain available during status polling and
  queue behind an in-flight serial command; Brake takes precedence over Cancel.
- The browser serial console sends the LF command terminator expected by the
  ESP-IDF console. Its firmware identity handshake limits startup retries and
  accepts the verified ID even when later console output is already buffered.

### Known limitations
- The benchmark measures synthesis only, not the I2S/codec path. Initial repeated
  results cover only one configuration; the supported performance envelope
  remains uncharacterized.
- The model has not been validated against real-world recordings or
  measurements; no rights-verified acoustic acceptance reference is included.
- Distance Character is an experimental, uncalibrated creative effect and has
  no physical distance or sound-pressure interpretation.
