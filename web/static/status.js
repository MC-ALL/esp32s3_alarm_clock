const statusPage = (() => {
  const { request, setStatus, formatTimestamp, bindRetry, runPageLoader, isDeviceConnected } = window.AppCommon;
  const els = {
    refresh: document.querySelector("#refresh-status"),
    online: document.querySelector("#device-online"),
    temp: document.querySelector("#device-temp"),
    humi: document.querySelector("#device-humi"),
    lux: document.querySelector("#device-lux"),
    presence: document.querySelector("#device-presence"),
    updatedAt: document.querySelector("#device-updated-at"),
    unavailableNote: document.querySelector("#status-unavailable-note"),
  };

  function render(status) {
    const connected = isDeviceConnected(status);
    if (!connected) {
      els.online.textContent = "未连接";
      els.temp.textContent = "--";
      els.humi.textContent = "--";
      els.lux.textContent = "--";
      els.presence.textContent = "--";
      els.updatedAt.textContent = "--";
      if (els.unavailableNote) {
        els.unavailableNote.hidden = false;
      }
      return;
    }

    els.online.textContent = "在线";
    els.temp.textContent = status && status.temperature_c != null ? `${status.temperature_c.toFixed(1)}C` : "--";
    els.humi.textContent = status && status.humidity_percent != null ? `${status.humidity_percent.toFixed(0)}%` : "--";
    els.lux.textContent = status && status.lux != null ? `${status.lux.toFixed(0)}` : "--";
    els.presence.textContent = status && status.presence_detected != null ? (status.presence_detected ? "有人" : "无人") : "--";
    els.updatedAt.textContent = status ? formatTimestamp(status.updated_at) : "--";
    if (els.unavailableNote) {
      els.unavailableNote.hidden = true;
    }
  }

  async function load() {
    const data = await request("/api/device/status");
    render(data);
  }

  async function refreshNow() {
    setStatus("saving", "加载中...");
    try {
      await load();
      setStatus("saved", "已保存");
    } catch (error) {
      window.AppCommon.showLoadError(error);
    }
  }

  bindRetry(() => runPageLoader(load));
  els.refresh?.addEventListener("click", refreshNow);
  runPageLoader(load);
})();
