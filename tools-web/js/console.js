"use strict";

(() => {
  const eventLog = () => document.getElementById("event-log");

  function consoleText() {
    const rows = [...(eventLog()?.querySelectorAll(".log-entry") || [])];
    return rows.map((row) => row.textContent.trim()).join("\n");
  }

  async function copyConsole() {
    const text = consoleText();
    if (!text) return;
    try {
      await navigator.clipboard.writeText(text);
    } catch {
      const area = document.createElement("textarea");
      area.value = text;
      area.className = "clipboard-fallback";
      document.body.append(area);
      area.select();
      document.execCommand("copy");
      area.remove();
    }
  }

  function downloadConsole() {
    const text = consoleText();
    if (!text) return;
    const stamp = new Date().toISOString().replace(/[:.]/g, "-").slice(0, 19);
    const blob = new Blob([text + "\n"], { type: "text/plain;charset=utf-8" });
    const url = URL.createObjectURL(blob);
    const link = document.createElement("a");
    link.href = url;
    link.download = `siren-console-${stamp}.log`;
    link.click();
    window.setTimeout(() => URL.revokeObjectURL(url), 1000);
  }

  function clearConsole() {
    eventLog()?.replaceChildren();
  }

  document.getElementById("drawerClearBtn")?.addEventListener("click", clearConsole);
  document.getElementById("drawerCopyBtn")?.addEventListener("click", () => void copyConsole());
  document.getElementById("drawerDownloadBtn")?.addEventListener("click", downloadConsole);
})();
