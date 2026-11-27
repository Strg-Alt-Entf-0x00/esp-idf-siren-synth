# esp-idf-siren-synth

> Reusable ESP-IDF component for real-time electromechanical siren synthesis.

The component renders PCM for an E57-inspired electromechanical source and an
experimental RPM-driven chopper source. `warning`, `all-clear`, `catastrophe`,
and `test` are temporal signal patterns, not four different validated siren
hardware models.

Project-authored source and documentation are licensed under the MIT License
in [LICENSE](./LICENSE). Third-party components retain their own licenses;
include the applicable notices when redistributing dependencies or firmware.

The `ws-esp32-p4-wifi6-console-siren-dev` example has been built and exercised
on a Waveshare ESP32-P4-WIFI6 (chip revision v1.3) with the ES8311 output path.
Siren playback is user-triggered; a short boot chime plays after successful
audio initialization. The included DSP benchmark measures synthesis only; it
does not characterize codec performance or acoustic output. This project is
not a certified or operational life-safety warning system.

No independent, rights-verified acoustic reference or acceptance recording is
included. The component keeps the siren model separate from output hardware and
does not claim calibrated distance or real-world acoustic behavior.

## Component Integration

The siren synthesizer is intentionally hardware-agnostic. The component renders
PCM frames into a caller-provided `int16_t` buffer and returns control to the
consumer application, which owns the actual audio output path.

### Recommended integration pattern

- Keep the siren synthesizer in a reusable component library.
- Use a board-specific audio layer (I2S, codec, DAC, or audio queue) outside the
  component.
- Render fixed-size blocks, then hand them to the output driver.
- Keep emergency-state logic and legal warning semantics in the application, not
  inside the reusable DSP engine.

### Example usage

```c
siren_synth_handle_t synth;
siren_synth_config_t cfg = {
    .sample_rate = 16000,
    .duration_ms = 2000,
    .amplitude = 0.75f,
    .sweep_start_hz = 420.0f,
    .sweep_end_hz = 1800.0f,
    .rotor_hz = 7.0f,
    .modulation_depth = 0.20f,
    .wobble_hz = 1.5f,
    .pattern = SIREN_PATTERN_WARNING,
};

siren_synth_init(&synth, &cfg);
int16_t buffer[256];
siren_synth_render(&synth, buffer, 256);
siren_synth_deinit(&synth);
```

## Intended Scope

- Render siren-model audio as PCM in real time.
- Keep synthesis independent of the board-specific audio output driver.
- Provide a baseline E57-style source and an optional experimental chopper
  source with controllable patterns.
- Keep speech/TTS, alarm-system control, networking, and I2S/codec ownership
  outside this component.

Additional electromechanical variants and pneumatic, vehicle, or electronic
warning-sound families are not implemented. They should be added only with a
defined use case and suitable reference evidence.

## Component API

The public API is exposed through `include/siren_synth.h` and supports:

- pattern selection (`warning`, `all-clear`, `catastrophe`, `test`)
- configurable sample rate, amplitude, sweep range, rotor modulation
- optional room reverberation, radio-style filtering, thermal motor loss, and
  rotor jitter
- an experimental stylized distance-character filter/level effect; its
  normalized control does not represent physical distance or calibrated SPL
- optional RPM-driven chopper source with independently mixed high/low rotor,
  vane, undertone, and overtone oscillators
- finite or continuous rendering into a caller-owned PCM buffer
- clean separation of synthesis logic from hardware I/O

`siren_synth_config_defaults()` supplies the factory values. Room delay memory
is allocated only for an enabled room model, during synthesizer initialization,
and released by `siren_synth_deinit()`. Board examples can layer explicit
storage on top; the core has no NVS dependency.

The chopper model uses the kinematic relationship
`frequency_hz = rpm * port_count / 60`. It is an independent source option;
the original E57-inspired model remains the factory default. It is an
experimental synthesis model, not a calibrated physical siren. Initialization
rejects active chopper fundamentals at or above Nyquist to avoid silent
aliasing.

## Repository Structure

```text
include/                         Public component API
src/                             Component implementation
  siren_synth.c                  Real-time siren renderer
examples/
  ws-esp32-p4-wifi6-console-siren-dev/  Waveshare ESP32-P4-WIFI6 ES8311 example
tests/
  host/                          Native C renderer regression tests
  test_web_console.py            Standalone browser-console checks
tools-web/
  index.html                     Standalone Web Serial console entry point
  styles.css                     Console presentation
  js/
    app.js                       Serial protocol and device UI
    console.js                   Clear/copy/download actions
```

`ws-esp32-p4-wifi6-console-siren-dev` owns the Waveshare-specific
I2C/I2S/ES8311 setup and interactive playback console. See its README for
board-specific build and flash instructions.

The reusable DSP component does not restrict its manifest to a specific chip:
it renders caller-owned PCM and contains no board-specific audio drivers.
Component Manager can therefore resolve it for ESP-IDF targets supported by the
declared ESP-IDF version. This is not a claim that every target has been tested;
the included audio/console example is specifically for the Waveshare
ESP32-P4-WIFI6 with ES8311, and that is the hardware validated for this project.
See the [Waveshare example README](./examples/ws-esp32-p4-wifi6-console-siren-dev/README.md)
for board-specific details.

## Development

The implementation is intentionally conservative and model-based rather than a
certified emergency warning system. It provides a reusable synthesis primitive
for consumer applications that still own their hardware output, codec, and alarm
logic.

See [CHANGELOG.md](./CHANGELOG.md) for project changes and the
[Waveshare example README](./examples/ws-esp32-p4-wifi6-console-siren-dev/README.md)
for NVS behavior and board-specific build/flash instructions.

### Automated tests

The native renderer tests require CMake and a C11 compiler:

```powershell
cmake -S tests/host -B build/host-tests
cmake --build build/host-tests --config Release
ctest --test-dir build/host-tests -C Release --output-on-failure
```

The standalone browser-console checks use Python 3's standard library only;
Python is not required to use the HTML console:

```powershell
python -m unittest discover -s tests -p "test_*.py"
```

### Standalone browser console

Open [tools-web/index.html](./tools-web/index.html) in Chrome, Edge, or
the VS Code Chromium browser, then choose the USB serial device from the
browser's Web Serial picker. This is a small set of local HTML, CSS, and JavaScript
files: no Python bridge, batch launcher, local server, network connection, or
browser extension is required. Firefox does not currently expose the Web Serial API.
The browser verifies the development firmware before enabling controls and
never starts playback automatically. Close ESP-IDF Monitor and other serial
applications before connecting because the UART is exclusive.

The page provides all current firmware settings, playback/volume controls,
firmware configuration reload, the DSP benchmark, the firmware command
console, and validated local preset save/import/export. Playback and presets
are in the toolbar below the header; all Signal, Room & effects, and Chopper &
RPM settings are shown together on one page. Device settings remain unknown,
and the header volume level remains unset, until values are read from firmware;
they are not presented as live factory defaults. Connection, playback, and
configuration state are summarized in the bottom status bar. Playback offers
a single Play/Brake icon button and a separate wind-down icon button. Brake
can interrupt a wind-down. Unbounded playback and reset commands require
confirmation. Firmware build/flash controls are not included. The standalone
browser console is the only PC serial console; it
needs no Python installation, dependencies, batch launcher, local server,
network connection, or browser extension.

---

## License

This project is licensed under the [PolyForm Noncommercial License 1.0.0](LICENSE).
- **Free for noncommercial use**: Personal, educational, research, and hobbyist projects are completely free.
- **Commercial use**: Not permitted without prior authorization. For commercial inquiries, please contact the author via GitHub.
