# Waveshare ESP32-P4-WIFI6 Siren Console Example

Interactive, output-only development console for the siren synthesizer,
configured specifically for the Waveshare ESP32-P4-WIFI6. The console is
registered before audio initialization, so commands remain available if the
ES8311/audio path fails. After successful audio initialization, a short C-E-G-C
chime signals that boot has completed. Siren playback is never started
automatically, and the speaker amplifier is kept disabled while idle. No
microphone is initialized.

## Audio hardware

| Signal | GPIO |
|---|---:|
| I2C SDA | 7 |
| I2C SCL | 8 |
| I2S DOUT | 9 |
| I2S WS | 10 |
| I2S BCLK | 12 |
| I2S MCLK | 13 |
| Speaker amplifier enable | 53 |

The output uses 48 kHz, 16-bit stereo PCM. The mono siren signal is sent to
both channels. Connect a speaker to the board's speaker output.
The factory output volume is 50/100; a valid saved NVS configuration takes
precedence on later boots. The boot chime uses the configured volume; set
`audio volume 0` to mute it.

The renderer models an electromechanical nine-port rotor: a wailing rotor
speed profile, motor acceleration and coast-down, blade harmonics, mains hum,
several acoustic resonances, optional thermal/jitter behavior, a radio-style
filter, and selectable room coloration. These are configurable synthetic
models, not recordings or validated measurements of a specific physical siren.

An alternate `chopper` source uses `frequency_hz = rpm * port_count / 60` and
mixes configurable high/low rotor, vane, undertone, and overtone components.
Its `single`, `multi-tone`, and `multi-chopper` modes choose which component
groups are active. It is an experimental synthesis model, not an acoustic
replica or a calibrated model of a physical siren.

## Console commands

```text
help
audio status
audio volume <0..100>
siren <1..3600 seconds>
siren 0
siren loop [safety seconds 0..3600]
siren cancel
siren brake
siren stop
siren status
siren config
siren config save
siren config load
siren config reset
siren benchmark
siren preset <warning|all-clear|catastrophe|test>
siren room <off|outdoor|bunker|night>
siren model <e57|chopper>
siren chopper <single|multi-tone|multi-chopper>
siren set <parameter> <value>
```

`siren 0`, `siren brake`, and `siren stop` request a fast brake using the
configured `release_ms`; they can interrupt a wind-down. `siren cancel` instead
ramps frequency and level down over three times the saved `motor_fall_s` (at
least one second). Playback remains active in `mode=wind_down` until the ramp
finishes. A positive duration plays once and remains interruptible by
`siren stop`; `siren loop` repeats the configured pattern until stopped. An
optional loop safety timeout (in seconds) stops playback on the device even if
the controlling application disappears; `0` means no timeout. The no-argument
form remains an unlimited loop for backwards-compatible console use.
`siren preset` without an argument lists the built-in temporal patterns:
warning is a continuous wail, all-clear is a constant tone, catastrophe
includes repeating silent intervals, and test adds variation to the wail.
`siren room` selects an optional room model; `bunker` and `night` allocate a
delay network only when playback starts.
`siren set` supports `amplitude` (0..1), `sweep_start_hz` (20..899 Hz, below
`sweep_end_hz`), `sweep_end_hz` (above `sweep_start_hz`, up to 900 Hz),
`rotor_hz`, `modulation_depth`, `wobble_hz`,
`attack_ms`, `release_ms`, and `duration_ms`. It also supports `room_size`,
`room_mix`, `radio_mix`, and `distance_mix` (0..1), and `radio`, `thermal`,
and `jitter` (0|1). `distance_mix` is an experimental creative filter/level
effect with a default of zero; it is not a calibrated distance, SPL, or
propagation model and is not acoustically accepted. A native host FFT regression
checks that it attenuates high-band energy relative to low-band energy in
rendered PCM; this does not measure the codec, speaker, or acoustic SPL.
The room size/mix controls apply to all room models; radio is a separate
high-pass/low-pass/peaking-filter effect. Thermal behavior models gradual motor
speed loss, and jitter controls small speed variations. The audio sample rate
is fixed by the board output configuration. The amplitude range is greater
than 0 through 1; use `audio volume 0` to mute the output.

For the chopper source, `siren set` also supports `peak_rpm` (100..30000),
`rotor_high_ports`, `rotor_low_ports`, `vanes_high`, `vanes_low`,
`undertone_ports`, and `overtone_ports` (each 0..64), `motor_rise_s`
(0.05..30), `motor_fall_s` (0.05..60), and each oscillator's
`mix_rotor_high`, `mix_rotor_low`, `mix_vanes_high`, `mix_vanes_low`,
`mix_undertone`, and `mix_overtone` (0..1). Zero count or mix disables a
component. The RPM sweep follows the selected signal pattern and the chopper
source derives each component frequency from RPM and its count. The core
rejects an active component whose maximum fundamental reaches Nyquist; lower
the RPM or count, mute that component, or choose a higher sample rate.

`siren benchmark` renders approximately one second of PCM in 256-sample blocks
and reports min/P95/max/mean DSP render time against the 5.33 ms block budget,
plus rendered PCM RMS/peak levels. It does not play audio and excludes
I2S/codec output; it is a device-specific measurement, not an acoustic-quality
test. Run it with playback stopped.

Every accepted siren-setting or volume change is committed to the `active_cfg`
blob in the `siren_dev` NVS namespace. Factory defaults are written there on
first boot; saved settings are loaded at boot, but playback is never restored or
started. Only the current NVS configuration schema is supported; an
incompatible saved blob is reported and left untouched rather than migrated or
erased. `siren config save` can explicitly re-save the current settings, and
`siren config load` reapplies the stored settings. `siren config reset` (or
`siren reset`) stores factory defaults in that same key; it does not erase the
NVS partition. If NVS is unavailable or a saved configuration is invalid, the
console reports the problem, uses factory defaults, and does not erase existing
NVS data.

For applying a full sound preset without a flash commit for every individual
parameter, the console also provides `siren config begin`, `commit`, and
`abort`. Changes made between `begin` and `commit` are staged in RAM and saved
as one NVS update; `abort` (or a failed commit) restores the previous settings.

Settings are stored as ordinary, unencrypted NVS data. This example does not
enable NVS/flash encryption or program eFuses; the boot ROM's eFuse revision
line is informational silicon-revision output.

There is one saved user configuration rather than a multi-profile manager:
the built-in siren patterns are selectable independently, and named profiles
can be added if actual workflows need several persistent effect setups. The
Core synthesizer remains independent of NVS and board/console code.

The console uses the board's default UART0 at 115200 baud. This is the same
serial port used for the UART monitor and flashing. The project-root standalone
[HTML browser console](../../tools-web/siren_console.html) connects through the
browser's Web Serial API without a Python bridge or local server and is the
only supported PC serial console. Users do not need Python, dependencies, or a
batch launcher. Close ESP-IDF Monitor before connecting with the browser.

## Build and flash

From an ESP-IDF terminal, run:

```powershell
idf.py build flash monitor
```

This example has been built with ESP-IDF v6.0.2 and
`espressif/esp_codec_dev` v1.6.2. The ESP Component Manager resolves the codec
dependency from its declared version.
