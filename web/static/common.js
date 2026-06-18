window.AppCommon = (() => {
  const DEVICE_CONNECTION_MAX_AGE_MS = 30 * 1000;
  const uiText = {
    idle: "空闲",
    loading: "加载中...",
    saving: "保存中...",
    saved: "已保存",
    ready: "就绪",
    saveFailed: "保存失败",
    loadFailed: "加载失败",
    textRequired: "内容不能为空",
  };

  let saveTimer = null;
  const deviceConnectionState = {
    known: false,
    connected: false,
    status: null,
    checkedAtMs: 0,
  };

  function parseTimestampMs(value) {
    if (!value) {
      return null;
    }
    const parsed = Date.parse(String(value));
    return Number.isFinite(parsed) ? parsed : null;
  }

  function isDeviceConnected(status) {
    if (!status || status.online !== true) {
      return false;
    }
    const updatedAtMs = parseTimestampMs(status.updated_at);
    if (updatedAtMs == null) {
      return false;
    }
    const ageMs = Date.now() - updatedAtMs;
    return ageMs >= -5000 && ageMs <= DEVICE_CONNECTION_MAX_AGE_MS;
  }

  function renderDeviceConnectionNotice() {
    const panel = document.querySelector("#device-connection-notice");
    if (!panel) {
      return;
    }
    panel.hidden = !deviceConnectionState.known || deviceConnectionState.connected;
  }

  function setStatus(mode, text) {
    const el = document.querySelector("#save-status");
    if (!el) {
      return;
    }

    clearTimeout(saveTimer);
    el.className = `save-status ${mode}`;
    el.textContent = text;

    if (mode === "saved") {
      saveTimer = window.setTimeout(() => {
        el.className = "save-status idle";
        el.textContent = uiText.idle;
      }, 1100);
    }
  }

  async function request(url, options = {}) {
    const response = await fetch(url, {
      headers: { "Content-Type": "application/json", ...(options.headers || {}) },
      ...options,
    });

    if (!response.ok) {
      let detail = "Request failed";
      try {
        const data = await response.json();
        detail = data.detail || detail;
      } catch {
        detail = response.statusText || detail;
      }
      throw new Error(detail);
    }

    if (response.status === 204) {
      return null;
    }

    return response.json();
  }

  function buildEmptyState(title, note) {
    const wrapper = document.createElement("div");
    wrapper.className = "empty-state";

    const titleEl = document.createElement("p");
    titleEl.className = "empty-title";
    titleEl.textContent = title;

    const noteEl = document.createElement("p");
    noteEl.className = "empty-note";
    noteEl.textContent = note;

    wrapper.appendChild(titleEl);
    wrapper.appendChild(noteEl);
    return wrapper;
  }

  function formatTimestamp(value) {
    if (!value) {
      return "--";
    }

    const raw = String(value).trim();
    const match = raw.match(/^(\d{4}-\d{2}-\d{2})[T\s](\d{2}:\d{2})/);
    if (match) {
      return `${match[1]} ${match[2]}`;
    }
    return raw;
  }

  function clearLoadError() {
    const panel = document.querySelector("#load-error");
    const detail = document.querySelector("#load-error-detail");
    if (!panel || !detail) {
      return;
    }
    panel.hidden = true;
    detail.hidden = true;
    detail.textContent = "";
  }

  function showLoadError(error) {
    const panel = document.querySelector("#load-error");
    const detail = document.querySelector("#load-error-detail");
    if (panel && detail) {
      panel.hidden = false;
      detail.hidden = false;
      detail.textContent = String(error && error.message ? error.message : error);
    }
    setStatus("error", uiText.loadFailed);
  }

  async function loadDeviceConnectionState(forceRefresh = false) {
    const now = Date.now();
    if (!forceRefresh && deviceConnectionState.known && now - deviceConnectionState.checkedAtMs < 5000) {
      return { ...deviceConnectionState };
    }

    const status = await request("/api/device/status");
    deviceConnectionState.known = true;
    deviceConnectionState.status = status;
    deviceConnectionState.connected = isDeviceConnected(status);
    deviceConnectionState.checkedAtMs = now;
    renderDeviceConnectionNotice();
    return { ...deviceConnectionState };
  }

  function bindRetry(handler) {
    const button = document.querySelector("#retry-load");
    if (!button || typeof handler !== "function") {
      return;
    }
    button.addEventListener("click", handler);
  }

  async function runPageLoader(loader) {
    clearLoadError();
    setStatus("saving", uiText.loading);
    try {
      try {
        await loadDeviceConnectionState(true);
      } catch {
        renderDeviceConnectionNotice();
      }
      await loader();
      setStatus("saved", uiText.ready);
    } catch (error) {
      showLoadError(error);
    }
  }

  return {
    uiText,
    request,
    setStatus,
    buildEmptyState,
    formatTimestamp,
    isDeviceConnected,
    loadDeviceConnectionState,
    clearLoadError,
    showLoadError,
    bindRetry,
    runPageLoader,
  };
})();
