import re
import unittest
from html.parser import HTMLParser
from pathlib import Path


CONSOLE_PATH = Path(__file__).resolve().parents[1] / "tools-web" / "siren_console.html"
if not CONSOLE_PATH.exists():
    CONSOLE_PATH = Path(__file__).resolve().parents[1] / "tools" / "siren_console.html"


class ResourceParser(HTMLParser):
    def __init__(self):
        super().__init__()
        self.inline_scripts = 0
        self.inline_styles = 0
        self.external_assets = []
        self.element_ids = set()
        self.attributes_by_id = {}

    def handle_starttag(self, tag, attrs):
        attributes = dict(attrs)
        if "id" in attributes:
            self.element_ids.add(attributes["id"])
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
        cls.parser = ResourceParser()
        cls.parser.feed(cls.html)

    def test_console_has_no_external_script_or_stylesheet(self):
        self.assertEqual(self.parser.inline_scripts, 1)
        self.assertEqual(self.parser.inline_styles, 1)
        self.assertEqual(self.parser.external_assets, [])
        self.assertNotRegex(self.html, re.compile(r"__API_TOKEN__|/app\.js|/styles\.css"))

    def test_console_uses_browser_serial_without_a_port_picker(self):
        self.assertIn("navigator.serial", self.html)
        self.assertIn("TOOL_ID:esp-idf-siren-synth:console_siren_dev:3", self.html)
        self.assertIn("WAVESHARE ESP32-P4 WIFI6", self.html)
        self.assertIn("connect-src 'none'", self.html)
        self.assertIn("connect-button", self.parser.element_ids)
        self.assertNotIn("port-select", self.parser.element_ids)

    def test_device_values_are_read_instead_of_assumed_and_theme_is_single(self):
        volume = self.parser.attributes_by_id["volume"]
        self.assertNotIn("value", volume)
        self.assertEqual(volume.get("placeholder"), "Connect to read")
        self.assertIn("await sendCommand(\"siren config\")", self.html)
        self.assertIn("parseDeviceConfig(output)", self.html)
        self.assertNotIn("theme-toggle", self.html)
        self.assertNotIn("data-theme=\"light\"", self.html)

    def test_serial_handshake_probes_identity_and_releases_both_stream_locks(self):
        self.assertRegex(
            self.html,
            re.compile(
                r"async function connectDevice\(\)[\s\S]*?await waitForFirmwareIdentity\(\);"
            ),
        )
        self.assertIn('await writeSerial("TOOL_ID\\n");', self.html)
        self.assertIn("await writeSerial(`${command}\\n`);", self.html)
        self.assertIn("pendingRead = serialReader.read()", self.html)
        self.assertIn("reader.releaseLock()", self.html)
        self.assertIn("writer.releaseLock()", self.html)
        self.assertIn("if (!output || output === command)", self.html)

    def test_identity_handshake_accepts_a_prompt_with_trailing_console_output(self):
        self.assertIn("normalized.indexOf(\"siren>\", identityEnd) >= 0", self.html)
        self.assertIn("probeCount < 3 && !identitySeen && Date.now() >= nextProbeAt", self.html)

    def test_playback_has_distinct_wind_down_and_brake_controls(self):
        self.assertIn("id=\"cancel-button\"", self.html)
        self.assertIn("Cancel · wind down", self.html)
        self.assertIn("id=\"stop-button\"", self.html)
        self.assertIn(">Brake now</button>", self.html)
        self.assertIn('requestPlaybackStop("siren cancel")', self.html)
        self.assertIn('requestPlaybackStop("siren stop")', self.html)

    def test_playback_stops_queue_while_serial_command_is_busy(self):
        self.assertIn("let pendingPlaybackStop = null;", self.html)
        self.assertIn('requestPlaybackStop("siren cancel")', self.html)
        self.assertIn('requestPlaybackStop("siren stop")', self.html)
        self.assertIn(
            'document.getElementById("stop-button").disabled = !connected || !playbackActive;',
            self.html,
        )
        self.assertIn("Queued '${command}' was not sent because playback is already idle.", self.html)


if __name__ == "__main__":
    unittest.main()
