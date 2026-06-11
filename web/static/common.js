window.AppCommon = (() => {
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
    return value || "--";
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
    clearLoadError,
    showLoadError,
    bindRetry,
    runPageLoader,
  };
})();
