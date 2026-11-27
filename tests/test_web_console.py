import re
import unittest
from html.parser import HTMLParser
from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parents[1]
CONSOLE_PATH = PROJECT_ROOT / "tools-web" / "index.html"
TOOLS_WEB = CONSOLE_PATH.parent


class ResourceParser(HTMLParser):
    def __init__(self):
        super().__init__()
        self.inline_scripts = 0
        self.inline_styles = 0
        self.external_assets = []
        self.element_ids = set()
        self.id_counts = {}
        self.attributes_by_id = {}

    def handle_starttag(self, tag, attrs):
        attributes = dict(attrs)
        if "id" in attributes:
            self.element_ids.add(attributes["id"])
            self.id_counts[attributes["id"]] = self.id_counts.get(attributes["id"], 0) + 1
            self.attributes_by_id[attributes["id"]] = attributes
        if tag == "script":
            if attributes.get("src"):
                self.external_assets.append(attributes["src"])
            else:
                self.inline_scripts += 1
        elif tag == "style":
            self.inline_styles += 1
        elif tag == "link" and "stylesheet" in attributes.get("rel", "").split():
            self.external_assets.append(attributes.get("href", "stylesheet"))


class StandaloneConsoleTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.html = CONSOLE_PATH.read_text(encoding="utf-8")
        cls.css = (TOOLS_WEB / "styles.css").read_text(encoding="utf-8")
        cls.app_js = (TOOLS_WEB / "js" / "app.js").read_text(encoding="utf-8")
        cls.console_js = (TOOLS_WEB / "js" / "console.js").read_text(encoding="utf-8")
        cls.parser = ResourceParser()
        cls.parser.feed(cls.html)

    def test_console_uses_modular_assets_and_no_inline_code(self):
        self.assertEqual(self.parser.inline_scripts, 0)
        self.assertEqual(self.parser.inline_styles, 0)
        self.assertIn("styles.css", self.parser.external_assets)
        self.assertIn("js/console.js", self.parser.external_assets)
        self.assertIn("js/app.js", self.parser.external_assets)
        self.assertNotRegex(self.html, re.compile(r"__API_TOKEN__"))

    def test_console_styles_respect_content_security_policy(self):
        self.assertNotRegex(self.html, re.compile(r"\sstyle\s*="))
        self.assertNotIn(".style.", self.app_js + self.console_js)
        self.assertNotIn("siren-scope", self.html + self.app_js + self.css)
        self.assertIn(".clipboard-fallback", self.css)

    def test_modal_dialogs_are_centered(self):
        modal_styles = self.css.split(".modal {", 1)[1].split("}", 1)[0]
        self.assertIn("margin: auto;", modal_styles)

    def test_workspace_matches_studio_layout_with_one_serial_console(self):
        self.assertIn('class="studio-workspace"', self.html)
        self.assertIn('class="status-bar"', self.html)
        for helper in (
            "Awaiting device status",
            "Device values have not been read",
            "Values are read from firmware",
            "Cancel winds down over",
            "Ready. Connect to select",
        ):
            self.assertNotIn(helper, self.html)
        self.assertNotIn("Connect to read", self.app_js)
        self.assertNotIn("playback-state", self.parser.element_ids)
        self.assertNotIn("device-read-state", self.parser.element_ids)
        self.assertTrue(all(count == 1 for count in self.parser.id_counts.values()))
        self.assertEqual(self.html.count('id="event-log"'), 1)
        for element_id in ("drawerCopyBtn", "drawerDownloadBtn", "drawerClearBtn", "drawerCloseBtn"):
            self.assertIn(element_id, self.parser.element_ids)
        self.assertNotIn("drawerTermContent", self.html + self.app_js + self.console_js)
        self.assertNotIn('id="clear-log"', self.html)

    def test_settings_are_grouped_on_one_page_and_help_is_available(self):
        self.assertNotIn('role="tablist"', self.html)
        self.assertNotIn("settings-tab", self.html + self.app_js)
        self.assertNotIn("settings-tabs", self.app_js)
        self.assertNotIn("SYNTHESIS SETTINGS", self.html)
        self.assertNotIn("Sound configuration</h2>", self.html)
        for title in ("Signal", "Room & effects", "Chopper & RPM"):
            self.assertIn(f'title: "{title}"', self.app_js)
        self.assertIn("section.dataset.settingGroup = group.id;", self.app_js)
        self.assertIn("heading.textContent = group.title;", self.app_js)
        self.assertNotIn("section.hidden", self.app_js)
        self.assertIn("btnHelp", self.parser.element_ids)
        self.assertIn("help-dialog", self.parser.element_ids)
        self.assertIn("helpDialog.showModal()", self.app_js)
        self.assertIn("header-volume-track", self.html)

    def test_toolbar_holds_playback_and_presets_above_full_width_settings(self):
        toolbar = self.html.index('class="control-toolbar"')
        main = self.html.index('<main class="studio-workspace"')
        for heading in (
            ">PLAYBACK</p>",
            "<h2>Signal control</h2>",
            ">LOCAL PRESETS</p>",
            'id="preset-toolbar-title"',
            "Built-in and saved presets",
            'class="card-index"',
        ):
            self.assertNotIn(heading, self.html)
        self.assertIn('aria-label="Playback and preset controls"', self.html)
        self.assertIn('aria-label="Safety timer"', self.html)
        self.assertIn('aria-label="Sound preset"', self.html)
        self.assertIn('for="safety-timeout">Safety timer</label>', self.html)
        self.assertEqual(self.parser.attributes_by_id["safety-timeout"].get("type"), "number")
        self.assertIn("width: max-content;", self.css)
        for element_id in (
            "playback-button",
            "cancel-button",
            "run-benchmark",
            "safety-timeout",
            "preset-select",
            "save-preset",
            "delete-preset",
            "export-presets",
            "import-presets",
        ):
            index = self.html.index(f'id="{element_id}"')
            self.assertGreater(index, toolbar)
            self.assertLess(index, main)
        playback_form_end = self.html.index("</form>", self.html.index('id="playback-form"'))
        self.assertLess(self.html.index('id="run-benchmark"'), self.html.index('id="preset-select"'))
        self.assertLess(self.html.index('id="preset-select"'), playback_form_end)
        self.assertIn('class="toolbar-section safety-timeout-toolbar"', self.html)
        self.assertIn('id="refresh-config"', self.html[main:])
        self.assertNotIn("display-stage", self.html + self.css)
        self.assertNotIn("control-panel", self.html + self.css)

    def test_topbar_connect_and_console_actions_are_available_disconnected(self):
        self.assertIn('"btnConnect", "btnToggleTerm"', self.app_js)
        self.assertIn('"btnHelp"', self.app_js)
        self.assertIn('"close-help-dialog"', self.app_js)
        self.assertIn('btnConnect.setAttribute("aria-label"', self.app_js)
        self.assertIn('btnConnect.setAttribute("aria-pressed"', self.app_js)

    def test_console_has_standardized_clear_copy_download_actions(self):
        for element_id in ("drawerClearBtn", "drawerCopyBtn", "drawerDownloadBtn"):
            self.assertIn(element_id, self.parser.element_ids)
        self.assertIn("copyConsole", self.console_js)
        self.assertIn("downloadConsole", self.console_js)
        self.assertIn("clearConsole", self.console_js)
        self.assertIn("drawerCopyBtn", self.console_js)
        self.assertIn("drawerDownloadBtn", self.console_js)

    def test_console_send_action_matches_icon_button_pattern(self):
        attributes = self.parser.attributes_by_id["send-command"]
        self.assertIn("icon-btn", attributes.get("class", ""))
        self.assertEqual(attributes.get("type"), "submit")
        self.assertEqual(attributes.get("data-tooltip"), "Send command")
        self.assertEqual(attributes.get("aria-label"), "Send command")
        self.assertIn('id="send-command"', self.html)
        self.assertNotIn(">Send</button>", self.html)

    def test_command_input_has_no_visible_prefix_label(self):
        attributes = self.parser.attributes_by_id["command-input"]
        self.assertEqual(attributes.get("aria-label"), "Serial command")
        self.assertNotIn(">COMMAND</label>", self.html)
        self.assertIn("grid-template-columns: minmax(0, 1fr) auto;", self.css)

    def test_console_uses_browser_serial_without_a_port_picker(self):
        self.assertIn("navigator.serial", self.app_js)
        self.assertIn("TOOL_ID:esp-idf-siren-synth:console_siren_dev:3", self.app_js)
        self.assertIn("WAVESHARE ESP32-P4 WIFI6", self.html)
        self.assertIn("connect-src 'none'", self.html)
        self.assertIn("btnConnect", self.parser.element_ids)
        self.assertNotIn("connect-button", self.parser.element_ids)
        self.assertNotIn("disconnect-button", self.parser.element_ids)
        self.assertNotIn("port-select", self.parser.element_ids)

    def test_device_values_are_read_instead_of_assumed_and_theme_is_single(self):
        volume_slider = self.parser.attributes_by_id["header-volume-slider"]
        self.assertNotIn("value", volume_slider)
        self.assertIn("hidden", volume_slider)
        self.assertIn("disabled", volume_slider)
        self.assertEqual(self.parser.attributes_by_id["header-volume"].get("data-known"), "false")
        self.assertIn("header-volume-value", self.parser.element_ids)
        self.assertIn("header-volume-icon-shell", self.html)
        self.assertNotIn("volume-form", self.parser.element_ids)
        self.assertNotIn("volume-button", self.parser.element_ids)
        self.assertIn("await sendCommand(\"siren config\")", self.app_js)
        self.assertIn("parseDeviceConfig(output)", self.app_js)
        self.assertIn("Firmware did not confirm the requested speaker volume.", self.app_js)
        self.assertNotIn("theme-toggle", self.html)
        self.assertNotIn("data-theme=\"light\"", self.html)

    def test_header_volume_has_no_card_surface(self):
        volume_styles = self.css.split(".header-volume {", 1)[1].split("}", 1)[0]
        self.assertNotIn("border:", volume_styles)
        self.assertNotIn("background:", volume_styles)

    def test_playback_state_starts_unknown_and_comes_from_firmware(self):
        self.assertIn("let playbackActive = null;", self.app_js)
        self.assertIn('function renderPresetOptions(selectedName = "")', self.app_js)
        self.assertIn("function recordPlayback(text)", self.app_js)
        self.assertIn('clean.includes("playback=active")', self.app_js)
        self.assertIn('clean.includes("playback=idle")', self.app_js)
        self.assertIn('control.placeholder = "—"', self.app_js)
        send_command = self.app_js[
            self.app_js.index("async function sendCommand"):
            self.app_js.index("async function waitForIdle")
        ]
        self.assertNotIn("playbackActive = true;", send_command)

    def test_serial_handshake_probes_identity_and_releases_both_stream_locks(self):
        self.assertRegex(
            self.app_js,
            re.compile(r"async function connectDevice\(\)[\s\S]*?await waitForFirmwareIdentity\(\);"),
        )
        self.assertIn('await writeSerial("TOOL_ID\\n");', self.app_js)
        self.assertIn("await writeSerial(`${command}\\n`);", self.app_js)
        self.assertIn("pendingRead = serialReader.read()", self.app_js)
        self.assertIn("reader.releaseLock()", self.app_js)
        self.assertIn("writer.releaseLock()", self.app_js)
        self.assertIn("if (!output || output === command)", self.app_js)

    def test_identity_handshake_accepts_a_prompt_with_trailing_console_output(self):
        self.assertIn("normalized.indexOf(\"siren>\", identityEnd) >= 0", self.app_js)
        self.assertIn("probeCount < 3 && !identitySeen && Date.now() >= nextProbeAt", self.app_js)

    def test_device_snapshot_is_removed_and_benchmark_remains_available(self):
        self.assertNotIn("DEVICE SNAPSHOT", self.html)
        self.assertNotIn("Firmware status", self.html)
        self.assertNotIn('id="device-config"', self.html)
        self.assertNotIn('id="read-status"', self.html)
        self.assertNotIn('id="read-config"', self.html)
        self.assertIn('id="run-benchmark"', self.html)
        self.assertIn('data-tooltip="Run DSP benchmark"', self.html)
        self.assertNotIn("deviceSnapshot", self.app_js)

    def test_playback_uses_two_icon_buttons_with_stateful_brake(self):
        self.assertIn('id="playback-button"', self.html)
        self.assertIn('data-tooltip="Start current pattern"', self.html)
        self.assertIn("id=\"cancel-button\"", self.html)
        self.assertIn('data-tooltip="Cancel · wind down"', self.html)
        self.assertNotIn('id="start-button"', self.html)
        self.assertNotIn('id="stop-button"', self.html)
        self.assertIn(".playback-action svg[hidden]", self.css)
        self.assertIn("playbackButton.dataset.tooltip = playbackLabel;", self.app_js)
        self.assertIn('const playbackLabel = playbackIsActive ? "Brake now" : "Start current pattern";', self.app_js)
        self.assertIn('if (playbackActive === true) {\n    requestPlaybackStop("siren stop");', self.app_js)
        self.assertIn('requestPlaybackStop("siren cancel")', self.app_js)

    def test_playback_stops_queue_while_serial_command_is_busy(self):
        self.assertIn("let pendingPlaybackStop = null;", self.app_js)
        self.assertIn('requestPlaybackStop("siren cancel")', self.app_js)
        self.assertIn('requestPlaybackStop("siren stop")', self.app_js)
        self.assertIn(
            "playbackButton.disabled = !connected || (busy && !playbackIsActive);",
            self.app_js,
        )
        self.assertIn("Queued '${command}' was not sent because firmware reported playback idle.", self.app_js)

    def test_preset_selection_applies_and_actions_are_tooltipped_icon_buttons(self):
        self.assertNotIn('id="apply-preset"', self.html)
        self.assertNotIn("Apply preset", self.html)
        self.assertIn('id="preset-select"', self.html)
        self.assertIn('addEventListener("change", handlePresetSelectionChange)', self.app_js)
        self.assertIn('void withBusy(() => applyScene(preset.scene));', self.app_js)
        self.assertIn('Connect to apply the selected preset.', self.app_js)
        self.assertIn("select.disabled = busy;", self.app_js)
        self.assertIn('placeholder.value = "";', self.app_js)
        self.assertNotIn("placeholder.disabled = true;", self.app_js)
        for element_id in ("save-preset", "delete-preset", "export-presets", "import-presets"):
            attributes = self.parser.attributes_by_id[element_id]
            self.assertIn("icon-btn", attributes.get("class", ""))
            self.assertIn("data-tooltip", attributes)
            self.assertIn("aria-label", attributes)
        self.assertLess(self.html.index('id="preset-select"'), self.html.index('id="save-preset"'))


if __name__ == "__main__":
    unittest.main()
