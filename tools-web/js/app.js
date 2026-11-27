"use strict";
"use strict";

const FIRMWARE_ID = "TOOL_ID:esp-idf-siren-synth:console_siren_dev:3";
const PRESET_STORAGE_KEY = "esp-idf-siren-synth.presets.v3";
const LEGACY_PRESET_STORAGE_KEY = "esp-idf-siren-synth.presets.v2";
const DEFAULTS = {
  pattern: "warning", room: "off", source: "e57", chopper: "multi-chopper",
  radio: "0", thermal: "1", jitter: "1", volume: "50", amplitude: "0.62",
  sweep_start_hz: "210", sweep_end_hz: "420", rotor_hz: "9",
  modulation_depth: "0.26", wobble_hz: "2.2", attack_ms: "50",
  release_ms: "180", duration_ms: "3000", room_size: "0.5",
  room_mix: "0.25", radio_mix: "0.5", distance_mix: "0", peak_rpm: "3450",
  motor_rise_s: "1.55", motor_fall_s: "5.8", rotor_high_ports: "12",
  rotor_low_ports: "10", vanes_high: "5", vanes_low: "4",
  undertone_ports: "2", overtone_ports: "3", mix_rotor_high: "1",
  mix_rotor_low: "0.95", mix_vanes_high: "0.45", mix_vanes_low: "0.4",
  mix_undertone: "0.3", mix_overtone: "0.5",
};
const SELECTS = [
  { key: "pattern", label: "Signal pattern", options: ["warning", "all-clear", "catastrophe", "test"] },
  { key: "room", label: "Room profile", options: ["off", "outdoor", "bunker", "night"] },
  { key: "source", label: "Sound source", options: ["e57", "chopper"] },
  { key: "chopper", label: "Chopper configuration", options: ["single", "multi-tone", "multi-chopper"] },
];
const BOOLEANS = [
  { key: "radio", label: "Radio filter" },
  { key: "thermal", label: "Thermal motor loss" },
  { key: "jitter", label: "Rotor jitter" },
];
const NUMBERS = [
  ["amplitude", "Amplitude", 0.0001, 1, 0.01],
  ["sweep_start_hz", "Sweep Start (Hz)", 20, 899, 1],
  ["sweep_end_hz", "Sweep End (Hz)", 21, 900, 1],
  ["rotor_hz", "Rotor (Hz)", 0.01, 30, 0.1],
  ["modulation_depth", "Modulation Depth", 0, 1, 0.01],
  ["wobble_hz", "Wobble (Hz)", 0.01, 30, 0.1],
  ["attack_ms", "Attack (ms)", 1, 5000, 1],
  ["release_ms", "Release (ms)", 1, 5000, 1],
  ["duration_ms", "Signal duration (ms)", 100, 60000, 100],
  ["room_size", "Room Size", 0, 1, 0.01],
  ["room_mix", "Room Mix", 0, 1, 0.01],
  ["radio_mix", "Radio Mix", 0, 1, 0.01],
  ["distance_mix", "Distance Character Mix", 0, 1, 0.01],
  ["peak_rpm", "Peak RPM", 100, 30000, 10],
  ["motor_rise_s", "Motor Rise (s)", 0.05, 30, 0.05],
  ["motor_fall_s", "Motor Fall (s)", 0.05, 60, 0.05],
  ["rotor_high_ports", "High Rotor · Ports", 0, 64, 0.25],
  ["rotor_low_ports", "Low Rotor · Ports", 0, 64, 0.25],
  ["vanes_high", "High Vanes", 0, 64, 0.25],
  ["vanes_low", "Low Vanes", 0, 64, 0.25],
  ["undertone_ports", "Undertone · Ports", 0, 64, 0.25],
  ["overtone_ports", "Overtone · Ports", 0, 64, 0.25],
  ["mix_rotor_high", "High Rotor Mix", 0, 1, 0.01],
  ["mix_rotor_low", "Low Rotor Mix", 0, 1, 0.01],
  ["mix_vanes_high", "High Vanes Mix", 0, 1, 0.01],
  ["mix_vanes_low", "Low Vanes Mix", 0, 1, 0.01],
  ["mix_undertone", "Undertone Mix", 0, 1, 0.01],
  ["mix_overtone", "Overtone Mix", 0, 1, 0.01],
].map(([key, label, minimum, maximum, step]) => ({
  key, label, minimum, maximum, step, default: DEFAULTS[key],
}));
const SELECT_COMMANDS = {
  pattern: "siren preset", room: "siren room",
  source: "siren model", chopper: "siren chopper",
};
const CONFIG_KEYS = {
  pattern: "pattern", room: "room", room_size: "room_size", room_mix: "room_mix",
  radio_mix: "radio_mix", distance_mix: "distance_mix",
  thermal: "thermal", jitter: "jitter", radio: "radio",
  volume: "volume", source: "source", chopper: "chopper", peak_rpm: "peak_rpm",
  rise: "motor_rise_s", fall: "motor_fall_s", amplitude: "amplitude",
  sweep_start_hz: "sweep_start_hz", sweep_end_hz: "sweep_end_hz",
  rotor_hz: "rotor_hz", modulation_depth: "modulation_depth", wobble_hz: "wobble_hz",
  attack_ms: "attack_ms", release_ms: "release_ms", duration_ms: "duration_ms",
};
const PORT_KEYS = {
  rotor_high: "rotor_high_ports", rotor_low: "rotor_low_ports",
  vane_high: "vanes_high", vane_low: "vanes_low",
  undertone: "undertone_ports", overtone: "overtone_ports",
};
const MIX_KEYS = {
  rotor_high: "mix_rotor_high", rotor_low: "mix_rotor_low",
  vane_high: "mix_vanes_high", vane_low: "mix_vanes_low",
  undertone: "mix_undertone", overtone: "mix_overtone",
};
const BUILTINS = Object.fromEntries(
  ["Warning", "All Clear", "Catastrophe", "Test"].map((name, index) => [
    name,
    { ...DEFAULTS, pattern: ["warning", "all-clear", "catastrophe", "test"][index] },
  ]),
);
const CHOPPER_ACTIVE = {
  single: [0, 2],
  "multi-tone": [0, 1, 2, 3],
  "multi-chopper": [0, 1, 2, 3, 4, 5],
};
const PORT_COUNT_KEYS = [
  "rotor_high_ports", "rotor_low_ports", "vanes_high", "vanes_low",
  "undertone_ports", "overtone_ports",
];
const PORT_MIX_KEYS = [
  "mix_rotor_high", "mix_rotor_low", "mix_vanes_high", "mix_vanes_low",
  "mix_undertone", "mix_overtone",
];
const SETTING_GROUPS = [
  { id: "signal", title: "Signal", keys: ["pattern", "amplitude", "sweep_start_hz", "sweep_end_hz", "rotor_hz", "modulation_depth", "wobble_hz", "attack_ms", "release_ms", "duration_ms"] },
  { id: "room", title: "Room & effects", keys: ["room", "room_size", "room_mix", "radio_mix", "distance_mix", "radio", "thermal", "jitter"] },
  { id: "chopper", title: "Chopper & RPM", keys: ["source", "chopper", "peak_rpm", "motor_rise_s", "motor_fall_s", "rotor_high_ports", "rotor_low_ports", "vanes_high", "vanes_low", "undertone_ports", "overtone_ports", "mix_rotor_high", "mix_rotor_low", "mix_vanes_high", "mix_vanes_low", "mix_undertone", "mix_overtone"] },
];

const volumeSlider = document.getElementById("header-volume-slider");
const volumeValue = document.getElementById("header-volume-value");
const headerVolume = document.getElementById("header-volume");
const connectionMessage = document.getElementById("connection-message");
const commandInput = document.getElementById("command-input");
const eventLog = document.getElementById("event-log");
const connectionBadge = document.getElementById("connection-badge");
let connected = false;
let busy = false;
let pendingPlaybackStop = null;
let currentSettings = {};
let presetCatalog = { schema_version: 3, presets: {} };
let lastPresetSelection = "";
let configLoaded = false;
let playbackActive = null;
let deviceVolume = null;
let connectionStage = "Disconnected";
let serialPort = null;
let serialReader = null;
let serialWriter = null;
let pendingRead = null;
let configTransactionActive = false;

function appendLogRow(level, text) {
  const row = document.createElement("li");
  row.className = `log-entry log-${level.toLowerCase()}`;
  const time = document.createElement("time");
  time.dateTime = new Date().toISOString();
  time.textContent = new Date().toLocaleTimeString();
  const label = document.createElement("span");
  label.className = "log-level";
  label.textContent = level;
  const message = document.createElement("span");
  message.className = "log-message";
  message.textContent = text;
  row.append(time, label, message);
  eventLog.append(row);
  while (eventLog.children.length > 500) eventLog.firstElementChild.remove();
  eventLog.scrollTop = eventLog.scrollHeight;
}

function showError(error) {
  const message = error instanceof Error ? error.message : String(error);
  connectionMessage.textContent = message;
  appendLogRow("ERROR", message);
}

function cleanSerialText(text) {
  return text.replace(/\x1b\[[0-?]*[ -/]*[@-~]/g, "").replace(/\x00/g, "");
}

function promptReceived(text) {
  return /(?:^|\n)siren>[ \t]+$/.test(cleanSerialText(text).replace(/\r/g, ""));
}

function commandWasRejected(text) {
  return /(?:^|\n)(?:Unrecognized command|Unknown (?:preset|room profile|source model|chopper configuration|parameter)|Unknown parameter or value out of range|Invalid numeric value|Usage:|Command returned non-zero error code|Could not )/i.test(cleanSerialText(text));
}

function setBusy(value) {
  busy = value;
  const availableWhileDisconnected = [
    "export-presets", "import-presets", "btnConnect", "btnToggleTerm", "btnHelp",
    "close-help-dialog",
    "drawerCopyBtn", "drawerDownloadBtn", "drawerClearBtn", "drawerCloseBtn",
  ];
  for (const button of document.querySelectorAll("button")) {
    if (!availableWhileDisconnected.includes(button.id)) {
      button.disabled = value || (!connected && button.id !== "btnConnect");
    } else if (button.id === "btnConnect" || button.id === "btnToggleTerm") {
      button.disabled = value;
    }
  }
  commandInput.disabled = value || !connected;
  for (const control of document.querySelectorAll("[data-setting-key]")) {
    control.disabled = value || !connected;
  }
  renderState();
}

function renderState() {
  connectionBadge.textContent = connectionStage;
  const failed = connectionStage === "Connection failed" ||
    connectionStage === "Web Serial unavailable";
  connectionBadge.className = `badge ${connected ? "badge-connected" : failed ? "badge-error" : ""}`;
  const btnConnect = document.getElementById("btnConnect");
  if (btnConnect) {
    btnConnect.classList.toggle("connected", connected);
    btnConnect.dataset.tooltip = connected ? "Disconnect ESP32" : "Connect ESP32 (Web Serial)";
    btnConnect.setAttribute("aria-label", connected ? "Disconnect ESP32" : "Connect ESP32");
    btnConnect.setAttribute("aria-pressed", String(connected));
  }
  const playbackButton = document.getElementById("playback-button");
  const playbackIsActive = playbackActive === true;
  const playbackLabel = playbackIsActive ? "Brake now" : "Start current pattern";
  playbackButton.disabled = !connected || (busy && !playbackIsActive);
  playbackButton.dataset.tooltip = playbackLabel;
  playbackButton.setAttribute("aria-label", playbackLabel);
  playbackButton.setAttribute("aria-pressed", String(playbackIsActive));
  playbackButton.classList.toggle("danger", playbackIsActive);
  playbackButton.classList.toggle("primary", !playbackIsActive);
  playbackButton.querySelector(".playback-play-icon").hidden = playbackIsActive;
  playbackButton.querySelector(".playback-stop-icon").hidden = !playbackIsActive;
  document.getElementById("cancel-button").disabled = !connected || playbackActive !== true;
  volumeSlider.disabled = !connected || busy || deviceVolume === null;
  document.getElementById("refresh-config").disabled = !connected || busy;
  document.getElementById("run-benchmark").disabled = !connected || busy;
  document.getElementById("save-preset").disabled = !connected || busy || !configLoaded;
  document.getElementById("send-command").disabled = !connected || busy;
  updatePresetButtons();
}

async function readChunk(timeoutMs) {
  if (!serialReader) throw new Error("The serial reader is not connected.");
  if (!pendingRead) {
    pendingRead = serialReader.read().finally(() => {
      pendingRead = null;
    });
  }
  const activeRead = pendingRead;
  let timer;
  try {
    const result = await Promise.race([
      activeRead,
      new Promise((resolve) => {
        timer = window.setTimeout(() => resolve({ timedOut: true }), timeoutMs);
      }),
    ]);
    return result;
  } finally {
    window.clearTimeout(timer);
  }
}

async function writeSerial(text) {
  if (!serialWriter) throw new Error("The serial writer is not connected.");
  await serialWriter.write(new TextEncoder().encode(text));
}

async function readPrompt(timeoutMs, command) {
  let response = "";
  const deadline = Date.now() + timeoutMs;
  while (Date.now() < deadline) {
    const result = await readChunk(deadline - Date.now());
    if (result.timedOut) break;
    if (result.done) throw new Error("The serial device disconnected.");
    response += new TextDecoder().decode(result.value, { stream: true });
    if (response.length > 32768) response = response.slice(-16384);
    const normalized = cleanSerialText(response).replace(/\r/g, "");
    if (promptReceived(normalized)) {
      const output = normalized.slice(0, normalized.lastIndexOf("siren>")).trim();
      if (!output || output === command) {
        response = "";
        continue;
      }
      appendLogRow("RX", output || "(no output)");
      recordPlayback(output);
      if (commandWasRejected(output)) throw new Error(output);
      if (command === "siren config begin") configTransactionActive = true;
      if (["siren config commit", "siren config abort"].includes(command)) {
        configTransactionActive = false;
      }
      return output;
    }
  }
  throw new Error(`Timed out waiting for the firmware prompt after '${command}'.`);
}

async function waitForFirmwareIdentity(timeoutMs = 12000) {
  let response = "";
  const deadline = Date.now() + timeoutMs;
  let probeCount = 0;
  let nextProbeAt = 0;
  let identitySeen = false;
  const sendProbe = async () => {
    appendLogRow("TX", "TOOL_ID");
    await writeSerial("TOOL_ID\n");
    probeCount += 1;
  };
  while (Date.now() < deadline) {
    if (probeCount < 3 && !identitySeen && Date.now() >= nextProbeAt) {
      await sendProbe();
      nextProbeAt = Date.now() + (probeCount === 1 ? 2000 : 3000);
    }
    const result = await readChunk(Math.max(1, Math.min(500, deadline - Date.now())));
    if (result.done) {
      throw new Error("The serial device disconnected before firmware identity was verified.");
    }
    if (result.timedOut) continue;
    response += new TextDecoder().decode(result.value, { stream: true });
    if (response.length > 32768) response = response.slice(-16384);
    const normalized = cleanSerialText(response).replace(/\r/g, "");
    const identityIndex = normalized.indexOf(FIRMWARE_ID);
    if (identityIndex >= 0) {
      identitySeen = true;
      const identityEnd = identityIndex + FIRMWARE_ID.length;
      if (normalized.indexOf("siren>", identityEnd) >= 0) {
        appendLogRow("RX", FIRMWARE_ID);
        return FIRMWARE_ID;
      }
    }
  }
  const received = cleanSerialText(response).replace(/\r/g, "").trim().slice(-1000);
  if (received) appendLogRow("RX", received);
  throw new Error(
    `Firmware identity was not verified. Expected ${FIRMWARE_ID}. ` +
    (received ? `Last serial output:\n${received}` : "No serial output was received."),
  );
}

async function exchange(command, timeoutMs = 6000) {
  validateCommand(command);
  if (!connected || !serialWriter || !serialReader) {
    throw new Error("Connect to verified siren firmware before sending commands.");
  }
  appendLogRow("TX", command);
  await writeSerial(`${command}\n`);
  try {
    return await readPrompt(timeoutMs, command);
  } catch (error) {
    if (error.message.startsWith("Timed out")) {
      try {
        await closeSerial();
      } catch (closeError) {
        throw new Error(`${error.message} ${closeError.message}`);
      }
    }
    throw error;
  }
}

function validateCommand(command) {
  if (typeof command !== "string" || !command || command.length > 128) {
    throw new Error("Enter one siren or audio command (maximum 128 characters).");
  }
  if (/[\x00-\x1f\x7f]/.test(command) || !/^(?:siren|audio)(?: [A-Za-z0-9_.-]+)*$/.test(command)) {
    throw new Error("Only one firmware siren or audio command is allowed.");
  }
}

async function connectDevice() {
  if (!("serial" in navigator)) {
    throw new Error("Web Serial is unavailable. Open this file in Chrome, Edge, or the VS Code Chromium browser.");
  }
  connectionStage = "Choose a USB serial device…";
  connectionMessage.textContent = "";
  renderState();
  const port = await navigator.serial.requestPort();
  connectionStage = "Opening serial port…";
  renderState();
  const baud = 115200;
  try {
    await port.open({ baudRate: baud });
  } catch (error) {
    throw new Error(
      `Could not open the selected serial port. Close ESP-IDF Monitor and other serial apps, then retry. ${error.message}`,
    );
  }
  serialPort = port;
  serialReader = port.readable.getReader();
  serialWriter = port.writable.getWriter();
  appendLogRow("INFO", `Serial port opened at ${baud} baud. The board may reboot when the port opens.`);
  connectionStage = "Verifying firmware…";
  renderState();
  await waitForFirmwareIdentity();
  connected = true;
  playbackActive = null;
  connectionStage = "Reading live device state…";
  renderState();
  appendLogRow("INFO", "Firmware identity verified; playback was not started.");
  await sendCommand("siren status");
  await sendCommand("siren config");
  await sendCommand("audio status");
  connectionStage = configLoaded
    ? "Connected · live values loaded"
    : "Connected · configuration incomplete";
  renderDeviceStatusMessage();
  renderState();
}

async function closeSerial() {
  const reader = serialReader;
  const writer = serialWriter;
  const port = serialPort;
  serialReader = null;
  serialWriter = null;
  serialPort = null;
  connected = false;
  playbackActive = null;
  configTransactionActive = false;
  connectionStage = "Disconnected";
  connectionMessage.textContent = "";
  const cleanupErrors = [];
  if (reader) {
    try {
      await reader.cancel();
    } catch (error) {
      cleanupErrors.push(`reader cancel: ${error.message}`);
    } finally {
      try {
        reader.releaseLock();
      } catch (error) {
        cleanupErrors.push(`reader release: ${error.message}`);
      }
    }
  }
  if (writer) {
    try {
      await writer.close();
    } catch (error) {
      cleanupErrors.push(`writer close: ${error.message}`);
    } finally {
      try {
        writer.releaseLock();
      } catch (error) {
        cleanupErrors.push(`writer release: ${error.message}`);
      }
    }
  }
  if (port) {
    try {
      await port.close();
    } catch (error) {
      cleanupErrors.push(`port close: ${error.message}`);
    }
  }
  pendingRead = null;
  clearDeviceSettingsControls(true);
  renderState();
  if (cleanupErrors.length) {
    throw new Error(`Serial cleanup was incomplete: ${cleanupErrors.join("; ")}`);
  }
}

function recordPlayback(text) {
  const clean = cleanSerialText(text);
  if (clean.includes("playback=active")) playbackActive = true;
  if (clean.includes("playback=idle")) playbackActive = false;
  renderDeviceStatusMessage();
  renderState();
}

function renderDeviceStatusMessage() {
  if (!connected) return;
  const playback = playbackActive === null
    ? "Playback status not read"
    : `Playback ${playbackActive ? "active" : "idle"}`;
  const configuration = !configLoaded
    ? "configuration incomplete"
    : configTransactionActive
      ? "settings staged"
      : "settings loaded";
  connectionMessage.textContent = `${playback} · ${configuration}`;
}

async function withBusy(action) {
  if (busy) return;
  setBusy(true);
  try {
    await action();
  } catch (error) {
    showError(error);
  } finally {
    setBusy(false);
    if (pendingPlaybackStop) {
      const command = pendingPlaybackStop;
      pendingPlaybackStop = null;
      if (playbackActive === true || (command === "siren stop" && playbackActive === null)) {
        await withBusy(() => sendCommand(command));
      } else if (playbackActive === false) {
        appendLogRow("INFO", `Queued '${command}' was not sent because firmware reported playback idle.`);
      } else {
        appendLogRow("INFO", `Queued '${command}' was not sent because active playback was not confirmed.`);
      }
    }
  }
}

function requestPlaybackStop(command) {
  if (!connected || (command === "siren cancel" && playbackActive !== true) ||
      (command === "siren stop" && playbackActive === false)) return;
  if (busy) {
    if (command === "siren stop" || pendingPlaybackStop === null) {
      pendingPlaybackStop = command;
    }
    appendLogRow(
      "INFO",
      `${pendingPlaybackStop === "siren stop" ? "Brake" : "Cancel"} queued until the current serial command completes.`,
    );
    return;
  }
  void withBusy(() => sendCommand(command));
}

async function sendCommand(command, confirmations = {}) {
  if (/^siren(?: config)? reset$/.test(command) && confirmations.confirmReset !== true) {
    throw new Error("Reset requires explicit confirmation.");
  }
  if (/^siren loop(?:\s+0+)?$/.test(command) && confirmations.confirmNoTimeout !== true) {
    throw new Error("Starting playback without a safety timeout requires explicit confirmation.");
  }
  const output = await exchange(command, command === "siren benchmark" ? 20000 : 6000);
  if ((command.startsWith("siren loop ") || /^siren [1-9]\d{0,3}$/.test(command)) &&
      playbackActive === null) {
    await exchange("siren status");
  }
  if (["siren stop", "siren brake", "siren 0"].includes(command)) {
    await waitForIdle();
  }
  if (command === "siren config") {
    applyDeviceConfig(output);
  }
  renderState();
  return output;
}

async function waitForIdle(timeoutMs = 7000) {
  const deadline = Date.now() + timeoutMs;
  while (Date.now() < deadline) {
    const status = await exchange("siren status", Math.min(2000, deadline - Date.now()));
    if (status.includes("playback=idle")) return status;
    await new Promise((resolve) => window.setTimeout(resolve, 100));
  }
  throw new Error("Playback stop was not confirmed; the device did not report playback=idle.");
}

function renderSettingsControls() {
  const fieldsByKey = new Map([
    ...SELECTS.map((field) => [field.key, { field, kind: "select" }]),
    ...NUMBERS.map((field) => [field.key, { field, kind: "number" }]),
    ...BOOLEANS.map((field) => [field.key, { field, kind: "boolean" }]),
  ]);
  const container = document.getElementById("settings-grid");
  container.replaceChildren();
  for (const group of SETTING_GROUPS) {
    const section = document.createElement("section");
    section.className = "settings-group";
    section.dataset.settingGroup = group.id;
    section.setAttribute("aria-labelledby", `settings-heading-${group.id}`);
    const heading = document.createElement("h2");
    heading.className = "settings-group-title";
    heading.id = `settings-heading-${group.id}`;
    heading.textContent = group.title;
    const fields = document.createElement("div");
    fields.className = "settings-fields";
    for (const key of group.keys) {
      const { field, kind } = fieldsByKey.get(key);
      const control = document.createElement(kind === "select" ? "select" : "input");
      control.className = "setting-control";
      control.dataset.settingKey = key;
      if (kind === "select") {
        for (const value of field.options) {
          const option = document.createElement("option");
          option.value = value;
          option.textContent = value;
          control.append(option);
        }
      } else if (kind === "boolean") {
        control.type = "checkbox";
      } else {
        control.type = "number";
        control.min = field.minimum;
        control.max = field.maximum;
        control.step = field.step;
        control.inputMode = "decimal";
        control.placeholder = "—";
      }
      control.disabled = true;
      control.addEventListener("change", onSettingChange);
      if (kind === "select") {
        const unknown = document.createElement("option");
        unknown.value = "";
        unknown.textContent = "—";
        unknown.disabled = true;
        unknown.selected = true;
        control.prepend(unknown);
      }
      const label = document.createElement("label");
      if (kind === "boolean") {
        label.className = "toggle-setting";
        const text = document.createElement("span");
        text.textContent = field.label;
        label.append(text, control);
      } else {
        label.className = "field";
        const text = document.createElement("span");
        text.textContent = field.label;
        label.append(text, control);
      }
      fields.append(label);
    }
    section.append(heading, fields);
    container.append(section);
  }
  clearDeviceSettingsControls(true);
}

function updateSettingControls(values) {
  currentSettings = { ...currentSettings, ...values };
  for (const control of document.querySelectorAll("[data-setting-key]")) {
    const value = values[control.dataset.settingKey];
    if (value === undefined) continue;
    if (control.type === "checkbox") {
      control.checked = value === "1";
      control.indeterminate = false;
    } else {
      control.value = value;
    }
  }
}

function clearDeviceSettingsControls(resetSettings = false) {
  if (resetSettings) currentSettings = {};
  configLoaded = false;
  deviceVolume = null;
  renderDeviceVolume();
  for (const control of document.querySelectorAll("[data-setting-key]")) {
    if (control.type === "checkbox") {
      control.checked = false;
      control.indeterminate = true;
    } else {
      control.value = "";
    }
  }
}

function applyDeviceConfig(output) {
  const parsed = parseDeviceConfig(output);
  clearDeviceSettingsControls();
  updateSettingControls(parsed);
  const parsedVolume = Number(parsed.volume);
  deviceVolume = Number.isInteger(parsedVolume) && parsedVolume >= 0 && parsedVolume <= 100
    ? parsedVolume
    : null;
  renderDeviceVolume();
  const missing = Object.keys(DEFAULTS).filter((key) => !(key in parsed));
  configLoaded = missing.length === 0;
  renderDeviceStatusMessage();
  if (connected) {
    connectionStage = configLoaded
      ? "Connected · live values loaded"
      : "Connected · configuration incomplete";
    renderState();
  }
  if (!configLoaded) {
    appendLogRow("ERROR", `Device configuration was incomplete: ${missing.join(", ")}.`);
  }
}

function renderDeviceVolume() {
  const hasDeviceVolume = deviceVolume !== null;
  headerVolume.dataset.known = String(hasDeviceVolume);
  volumeSlider.hidden = !hasDeviceVolume;
  volumeValue.classList.toggle("unknown", !hasDeviceVolume);
  volumeValue.textContent = hasDeviceVolume ? `${deviceVolume}%` : "—%";
  volumeSlider.setAttribute(
    "aria-valuetext",
    hasDeviceVolume ? `${deviceVolume} percent, read from device` : "Not read from device",
  );
  if (hasDeviceVolume) volumeSlider.value = String(deviceVolume);
  volumeSlider.disabled = !connected || busy || !hasDeviceVolume;
}

function parseDeviceConfig(text) {
  const values = {};
  for (const rawLine of text.split(/\r?\n/)) {
    const section = rawLine.split(":", 1)[0].trim();
    const mapping = section === "ports" ? PORT_KEYS : section === "mix" ? MIX_KEYS : CONFIG_KEYS;
    const line = section === "ports" || section === "mix"
      ? rawLine.slice(rawLine.indexOf(":") + 1)
      : rawLine;
    for (const match of line.matchAll(/([a-z_]+)=([^\s]+)/g)) {
      const [, key, rawValue] = match;
      if (!mapping[key]) continue;
      let value = rawValue;
      if (key === "rise" || key === "fall") value = value.replace(/s$/, "");
      if (key === "volume") value = value.split("/", 1)[0];
      values[mapping[key]] = value;
    }
  }
  for (const key of ["radio", "thermal", "jitter"]) {
    if (key in values) values[key] = values[key] === "on" ? "1" : "0";
  }
  return values;
}

function settingCommand(key, value) {
  return SELECT_COMMANDS[key]
    ? `${SELECT_COMMANDS[key]} ${value}`
    : `siren set ${key} ${value}`;
}

function onSettingChange(event) {
  const control = event.currentTarget;
  const key = control.dataset.settingKey;
  const value = control.type === "checkbox" ? (control.checked ? "1" : "0") : control.value;
  const previous = currentSettings[key];
  if (!control.checkValidity() || value === "") {
    updateSettingControls({ [key]: previous });
    showError(`${key} is outside its valid range.`);
    return;
  }
  const start = Number(key === "sweep_start_hz" ? value : currentSettings.sweep_start_hz);
  const end = Number(key === "sweep_end_hz" ? value : currentSettings.sweep_end_hz);
  if (start >= end) {
    updateSettingControls({ [key]: previous });
    showError("Sweep end must be greater than sweep start.");
    return;
  }
  withBusy(async () => {
    let commandAccepted = false;
    try {
      await sendCommand(settingCommand(key, value));
      commandAccepted = true;
      currentSettings[key] = value;
      await sendCommand("siren config");
    } catch (error) {
      if (connected) {
        updateSettingControls({ [key]: commandAccepted ? value : previous });
      }
      throw error;
    }
  });
}

function validatePresetName(name) {
  const normalized = name.trim();
  if (!normalized || normalized.length > 48 || !/^[A-Za-z0-9][A-Za-z0-9 _().-]*$/.test(normalized)) {
    throw new Error("Preset name must be 1–48 characters and use letters, numbers, spaces, and _ ( ) . -.");
  }
  return normalized;
}

function validateScene(values) {
  if (!values || typeof values !== "object" || Array.isArray(values)) {
    throw new Error("Preset configuration must be an object.");
  }
  const keys = Object.keys(DEFAULTS);
  if (Object.keys(values).length !== keys.length || keys.some((key) => !(key in values))) {
    throw new Error("Preset configuration has missing or unknown fields.");
  }
  const scene = Object.fromEntries(keys.map((key) => [key, String(values[key])]));
  for (const { key, options } of SELECTS) {
    if (!options.includes(scene[key])) throw new Error(`Unknown ${key} value.`);
  }
  for (const { key } of BOOLEANS) {
    if (!["0", "1"].includes(scene[key])) throw new Error(`${key} must be 0 or 1.`);
  }
  const volume = Number(scene.volume);
  if (!Number.isInteger(volume) || volume < 0 || volume > 100) {
    throw new Error("Volume must be an integer from 0 to 100.");
  }
  const numbers = {};
  for (const field of NUMBERS) {
    const number = Number(scene[field.key]);
    if (!Number.isFinite(number) || number < field.minimum || number > field.maximum) {
      throw new Error(`${field.label} must be between ${field.minimum} and ${field.maximum}.`);
    }
    numbers[field.key] = number;
  }
  if (numbers.sweep_end_hz <= numbers.sweep_start_hz) {
    throw new Error("Sweep end must be greater than sweep start.");
  }
  if (scene.source === "chopper") {
    for (const index of CHOPPER_ACTIVE[scene.chopper]) {
      if (numbers[PORT_COUNT_KEYS[index]] > 0 && numbers[PORT_MIX_KEYS[index]] > 0 &&
          numbers.peak_rpm * numbers[PORT_COUNT_KEYS[index]] / 60 >= 24000) {
        throw new Error("Chopper preset exceeds Nyquist; reduce RPM or port count.");
      }
    }
  }
  return scene;
}

function validatePresetCatalog(payload) {
  if (!payload || ![1, 2, 3].includes(payload.schema_version) ||
      !payload.presets || typeof payload.presets !== "object" || Array.isArray(payload.presets)) {
    throw new Error("Preset file must contain schema_version 1, 2, or 3 and a presets object.");
  }
  const entries = Object.entries(payload.presets);
  if (entries.length > 40) throw new Error("Preset file may contain at most 40 user presets.");
  const presets = {};
  const seen = new Set();
  for (const [rawName, rawScene] of entries) {
    const name = validatePresetName(rawName);
    const folded = name.toLowerCase();
    if (Object.keys(BUILTINS).some((builtin) => builtin.toLowerCase() === folded)) {
      throw new Error(`'${name}' is a built-in preset and cannot be overwritten.`);
    }
    if (seen.has(folded)) throw new Error(`Preset name '${name}' is duplicated after normalization.`);
    seen.add(folded);
    const scene = payload.schema_version === 1
      ? { ...DEFAULTS, ...rawScene }
      : payload.schema_version === 2
        ? { ...rawScene, distance_mix: DEFAULTS.distance_mix }
        : rawScene;
    presets[name] = validateScene(scene);
  }
  return { schema_version: 3, presets };
}

function saveLocalPresetCatalog(catalog) {
  try {
    localStorage.setItem(PRESET_STORAGE_KEY, JSON.stringify(catalog));
  } catch (error) {
    throw new Error(`Could not save browser presets: ${error.message}`);
  }
  presetCatalog = catalog;
}

function loadLocalPresets() {
  try {
    const encoded = localStorage.getItem(PRESET_STORAGE_KEY);
    if (encoded) {
      presetCatalog = validatePresetCatalog(JSON.parse(encoded));
    } else {
      const legacyEncoded = localStorage.getItem(LEGACY_PRESET_STORAGE_KEY);
      if (legacyEncoded) {
        presetCatalog = validatePresetCatalog(JSON.parse(legacyEncoded));
        try {
          localStorage.setItem(PRESET_STORAGE_KEY, JSON.stringify(presetCatalog));
        } catch (error) {
          showError(`Legacy presets loaded but could not be migrated: ${error.message}`);
        }
      }
    }
  } catch (error) {
    showError(`Saved presets could not be loaded: ${error.message}`);
    presetCatalog = { schema_version: 3, presets: {} };
  }
  renderPresetOptions();
}

function renderPresetOptions(selectedName = "") {
  const select = document.getElementById("preset-select");
  select.replaceChildren();
  const placeholder = document.createElement("option");
  placeholder.value = "";
  placeholder.textContent = "Choose a preset…";
  placeholder.selected = true;
  select.append(placeholder);
  const builtins = document.createElement("optgroup");
  builtins.label = "Built-in";
  for (const name of Object.keys(BUILTINS)) {
    const option = document.createElement("option");
    option.value = name;
    option.textContent = name;
    builtins.append(option);
  }
  select.append(builtins);
  const saved = document.createElement("optgroup");
  saved.label = "Saved in this browser";
  for (const name of Object.keys(presetCatalog.presets).sort((a, b) => a.localeCompare(b))) {
    const option = document.createElement("option");
    option.value = name;
    option.textContent = name;
    saved.append(option);
  }
  if (saved.children.length) select.append(saved);
  if ([...select.options].some((option) => option.value === selectedName)) select.value = selectedName;
  else select.value = "";
  lastPresetSelection = select.value;
  updatePresetButtons();
}

function updatePresetButtons() {
  const select = document.getElementById("preset-select");
  const selected = select.value;
  select.disabled = busy;
  document.getElementById("save-preset").disabled = !connected || busy || !configLoaded;
  document.getElementById("delete-preset").disabled =
    busy || !selected || Object.hasOwn(BUILTINS, selected);
}

function selectedPreset() {
  const name = document.getElementById("preset-select").value;
  const scene = Object.hasOwn(BUILTINS, name) ? BUILTINS[name] : presetCatalog.presets[name];
  if (!scene) throw new Error("Select a valid preset.");
  return { name, scene: validateScene(scene) };
}

function handlePresetSelectionChange() {
  const select = document.getElementById("preset-select");
  const selectedName = select.value;
  updatePresetButtons();
  if (!selectedName) {
    lastPresetSelection = "";
    updatePresetButtons();
    return;
  }
  if (!connected) {
    lastPresetSelection = selectedName;
    connectionMessage.textContent = "Connect to apply the selected preset.";
    return;
  }
  let preset;
  try {
    preset = selectedPreset();
  } catch (error) {
    select.value = lastPresetSelection;
    updatePresetButtons();
    showError(error);
    return;
  }
  if (!window.confirm(`Apply "${preset.name}" to the device and save it to NVS?`)) {
    select.value = lastPresetSelection;
    updatePresetButtons();
    return;
  }
  lastPresetSelection = selectedName;
  void withBusy(() => applyScene(preset.scene));
}

function buildSceneCommands(scene) {
  const commands = [
    "siren config begin",
    `siren preset ${scene.pattern}`,
    `siren room ${scene.room}`,
    `siren model ${scene.source}`,
    `siren chopper ${scene.chopper}`,
    "siren set sweep_end_hz 900",
    `siren set sweep_start_hz ${scene.sweep_start_hz}`,
    `siren set sweep_end_hz ${scene.sweep_end_hz}`,
  ];
  for (const field of NUMBERS) {
    if (!["sweep_start_hz", "sweep_end_hz"].includes(field.key)) {
      commands.push(`siren set ${field.key} ${scene[field.key]}`);
    }
  }
  for (const { key } of BOOLEANS) commands.push(`siren set ${key} ${scene[key]}`);
  commands.push(`audio volume ${scene.volume}`, "siren config commit", "siren config");
  return commands;
}

async function applyScene(scene) {
  const commands = buildSceneCommands(validateScene(scene));
  let transactionOpen = false;
  let commitAttempted = false;
  let commitConfirmed = false;
  try {
    for (const command of commands) {
      if (command === "siren config commit") commitAttempted = true;
      const output = await exchange(command);
      if (command === "siren config commit") commitConfirmed = true;
      if (command === "siren config begin") transactionOpen = true;
      if (["siren config commit", "siren config abort"].includes(command)) transactionOpen = false;
      if (command === "siren config") {
        applyDeviceConfig(output);
      }
    }
  } catch (error) {
    if (transactionOpen && !commitAttempted && connected) {
      try {
        await exchange("siren config abort");
      } catch (abortError) {
        appendLogRow("ERROR", `Could not confirm settings rollback: ${abortError.message}`);
      }
    }
    if (commitAttempted && !commitConfirmed) {
      throw new Error(`Settings commit could not be confirmed; read device configuration before retrying. ${error.message}`);
    }
    if (commitConfirmed) {
      throw new Error(`Preset was committed, but the post-commit device read failed. ${error.message}`);
    }
    throw new Error(`Preset was not fully applied; staged changes were aborted. ${error.message}`);
  }
  configLoaded = true;
}

function triggerPresetDownload() {
  if (!Object.keys(presetCatalog.presets).length) {
    showError("There are no saved user presets to export.");
    return;
  }
  const data = new Blob([`${JSON.stringify(presetCatalog, null, 2)}\n`], {
    type: "application/json",
  });
  const url = URL.createObjectURL(data);
  const link = document.createElement("a");
  link.href = url;
  link.download = "siren-presets.json";
  link.click();
  window.setTimeout(() => URL.revokeObjectURL(url), 1000);
}

async function importPresetFile(file) {
  if (file.size > 65536) throw new Error("Preset files must be 64 KiB or smaller.");
  const catalog = validatePresetCatalog(JSON.parse(await file.text()));
  saveLocalPresetCatalog(catalog);
  renderPresetOptions();
}

async function disconnectDevice() {
  if (!connected) return;
  if (configTransactionActive) await exchange("siren config abort");
  if (playbackActive) {
    await exchange("siren stop");
    await waitForIdle();
  }
  await closeSerial();
  connectionMessage.textContent = "";
  appendLogRow("INFO", "Serial port disconnected.");
}

async function connectFromHeader() {
  connectionStage = "Choose a USB serial device…";
  connectionMessage.textContent = "";
  renderState();
  try {
    await connectDevice();
  } catch (error) {
    if (serialPort) {
      try {
        await closeSerial();
      } catch (closeError) {
        appendLogRow("ERROR", `Could not close the failed serial connection: ${closeError.message}`);
      }
    }
    if (error.name === "NotFoundError") {
      connectionStage = "Disconnected";
      connectionMessage.textContent = "No device was selected.";
      appendLogRow("INFO", "Device selection canceled.");
      renderState();
      return;
    }
    connectionStage = "Connection failed";
    connectionMessage.textContent = error.message;
    renderState();
    throw error;
  }
}

document.getElementById("playback-form").addEventListener("submit", (event) => {
  event.preventDefault();
  if (playbackActive === true) {
    requestPlaybackStop("siren stop");
    return;
  }
  const timeout = Number(document.getElementById("safety-timeout").value);
  if (!Number.isInteger(timeout) || timeout < 0 || timeout > 3600) {
    showError("Safety timeout must be a whole number from 0 to 3600 seconds.");
    return;
  }
  if (timeout === 0 &&
      !window.confirm("Start without a firmware safety timeout? You must stop playback manually.")) return;
  withBusy(() => sendCommand(`siren loop ${timeout}`, { confirmNoTimeout: timeout === 0 }));
});

document.getElementById("cancel-button").addEventListener("click", () =>
  requestPlaybackStop("siren cancel"),
);

volumeSlider.addEventListener("input", () => {
  volumeValue.textContent = `${volumeSlider.value}%`;
  volumeSlider.setAttribute("aria-valuetext", `${volumeSlider.value} percent, pending confirmation`);
});
volumeSlider.addEventListener("change", () => {
  const requestedVolume = Number(volumeSlider.value);
  if (!connected || deviceVolume === null || busy || requestedVolume === deviceVolume) {
    renderDeviceVolume();
    return;
  }
  void withBusy(async () => {
    volumeSlider.value = String(requestedVolume);
    volumeValue.textContent = `${requestedVolume}%`;
    try {
      await sendCommand(`audio volume ${requestedVolume}`);
      await sendCommand("siren config");
      await sendCommand("audio status");
      if (deviceVolume !== requestedVolume) {
        throw new Error("Firmware did not confirm the requested speaker volume.");
      }
    } catch (error) {
      renderDeviceVolume();
      throw error;
    }
  });
});

document.getElementById("refresh-config").addEventListener("click", () =>
  withBusy(() => sendCommand("siren config")),
);
document.getElementById("run-benchmark").addEventListener("click", () =>
  withBusy(() => sendCommand("siren benchmark")),
);
document.getElementById("preset-select").addEventListener("change", handlePresetSelectionChange);

document.getElementById("save-preset").addEventListener("click", () => {
  if (!configLoaded || !connected) {
    showError("Read a verified device configuration before saving a preset.");
    return;
  }
  const dialog = document.getElementById("save-preset-dialog");
  document.getElementById("preset-name").value = "";
  dialog.showModal();
  document.getElementById("preset-name").focus();
});
document.getElementById("cancel-save-preset").addEventListener("click", () =>
  document.getElementById("save-preset-dialog").close(),
);
document.getElementById("save-preset-form").addEventListener("submit", (event) => {
  event.preventDefault();
  try {
    const name = validatePresetName(document.getElementById("preset-name").value);
    const existing = Object.keys(presetCatalog.presets).find((key) => key.toLowerCase() === name.toLowerCase());
    if (existing && !window.confirm(`Replace the saved preset "${existing}"?`)) return;
    const presets = { ...presetCatalog.presets };
    if (existing) delete presets[existing];
    presets[name] = validateScene(currentSettings);
    saveLocalPresetCatalog(validatePresetCatalog({ schema_version: 3, presets }));
    renderPresetOptions(name);
    document.getElementById("save-preset-dialog").close();
  } catch (error) {
    showError(error);
  }
});

document.getElementById("export-presets").addEventListener("click", triggerPresetDownload);
document.getElementById("import-presets").addEventListener("click", () =>
  document.getElementById("preset-file").click(),
);
document.getElementById("preset-file").addEventListener("change", async (event) => {
  const [file] = event.currentTarget.files;
  event.currentTarget.value = "";
  if (!file) return;
  try {
    await importPresetFile(file);
  } catch (error) {
    showError(`Preset import failed: ${error.message}`);
  }
});
document.getElementById("delete-preset").addEventListener("click", () => {
  const name = document.getElementById("preset-select").value;
  if (!Object.hasOwn(presetCatalog.presets, name)) {
    showError("Built-in presets cannot be deleted.");
    return;
  }
  if (!window.confirm(`Delete the saved preset "${name}" from this browser?`)) return;
  const presets = { ...presetCatalog.presets };
  delete presets[name];
  try {
    saveLocalPresetCatalog({ schema_version: 3, presets });
    renderPresetOptions();
  } catch (error) {
    showError(error);
  }
});

document.getElementById("command-form").addEventListener("submit", (event) => {
  event.preventDefault();
  const command = commandInput.value.trim();
  if (!command) return;
  const isReset = /^siren(?: config)? reset$/.test(command);
  const isUnboundedPlayback = /^siren loop(?:\s+0+)?$/.test(command);
  if (isReset &&
      !window.confirm("Replace saved NVS settings with factory defaults? Playback will not start.")) return;
  if (isUnboundedPlayback &&
      !window.confirm("Start with no firmware safety timeout? You must stop playback manually.")) return;
  withBusy(async () => {
    await sendCommand(command, {
      confirmReset: isReset,
      confirmNoTimeout: isUnboundedPlayback,
    });
    if (command === "siren config begin") {
      connectionMessage.textContent = "Settings staged in RAM; not committed.";
    }
    if (isReset || ["siren config load", "siren config commit", "siren config abort"].includes(command) ||
        /^(?:siren (?:preset|room|model|chopper|set)|audio volume)\b/.test(command)) {
      await sendCommand("siren config");
    }
    if (command.startsWith("audio volume ")) await sendCommand("audio status");
    commandInput.value = "";
  });
});

navigator.serial?.addEventListener("disconnect", (event) => {
  if (event.target !== serialPort) return;
  void closeSerial().then(() => {
    appendLogRow("ERROR", "USB serial device disconnected.");
  }).catch((error) => {
    appendLogRow("ERROR", `USB device disconnected; serial cleanup failed: ${error.message}`);
  });
});
window.addEventListener("pagehide", () => {
  if (serialPort) void closeSerial().catch((error) => appendLogRow("ERROR", error.message));
});

// Topbar & Terminal Drawer Handlers
document.getElementById("btnConnect")?.addEventListener("click", () => {
  if (connected) {
    void withBusy(disconnectDevice);
  } else {
    void withBusy(connectFromHeader);
  }
});
const helpDialog = document.getElementById("help-dialog");
document.getElementById("btnHelp")?.addEventListener("click", () => helpDialog.showModal());
document.getElementById("close-help-dialog")?.addEventListener("click", () => helpDialog.close());
helpDialog.addEventListener("click", (event) => {
  if (event.target === helpDialog) helpDialog.close();
});
document.getElementById("btnToggleTerm")?.addEventListener("click", () => {
  const drawer = document.getElementById("termDrawer");
  const isOpen = drawer?.classList.toggle("open") === true;
  if (drawer) {
    drawer.inert = !isOpen;
    drawer.setAttribute("aria-hidden", String(!isOpen));
  }
  document.getElementById("btnToggleTerm")?.setAttribute("aria-expanded", String(isOpen));
});
document.getElementById("drawerCloseBtn")?.addEventListener("click", () => {
  const drawer = document.getElementById("termDrawer");
  drawer?.classList.remove("open");
  if (drawer) {
    drawer.inert = true;
    drawer.setAttribute("aria-hidden", "true");
  }
  document.getElementById("btnToggleTerm")?.setAttribute("aria-expanded", "false");
});

renderSettingsControls();
loadLocalPresets();
if (!("serial" in navigator)) {
  connectionStage = "Web Serial unavailable";
  connectionMessage.textContent = "Open this file in Chrome, Edge, or the VS Code Chromium browser.";
  connectionBadge.textContent = "Web Serial unavailable";
  connectionBadge.className = "badge badge-error";
  appendLogRow("ERROR", "Use a Chromium-based browser with Web Serial support, such as Chrome, Edge, or the VS Code browser.");
}
setBusy(false);
window.setInterval(async () => {
  if (!connected || !playbackActive || busy) return;
  try {
    await withBusy(() => sendCommand("siren status"));
  } catch (error) {
    showError(error);
  }
}, 1800);


