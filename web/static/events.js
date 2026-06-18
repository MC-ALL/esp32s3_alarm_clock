const eventsPage = (() => {
  const { request, buildEmptyState, formatTimestamp, bindRetry, runPageLoader, setStatus } = window.AppCommon;
  const state = {
    events: [],
  };
  const els = {
    refresh: document.querySelector("#refresh-events"),
    filter: document.querySelector("#event-type-filter"),
    list: document.querySelector("#events-list"),
    eventTemplate: document.querySelector("#event-item-template"),
  };

  function render() {
    els.list.innerHTML = "";
    if (!state.events.length) {
      els.list.appendChild(buildEmptyState("最近还没有记录", "新的提醒和操作会出现在这里。"));
      return;
    }

    state.events.forEach((event) => {
      const fragment = els.eventTemplate.content.cloneNode(true);
      fragment.querySelector(".event-type").textContent = event.event_type || "unknown";
      fragment.querySelector(".event-time").textContent = formatTimestamp(event.event_at);

      const parts = [];
      if (event.todo_id) {
        parts.push(`todo=${event.todo_id}`);
      }
      if (event.temperature_c != null) {
        parts.push(`T=${event.temperature_c}`);
      }
      if (event.humidity_percent != null) {
        parts.push(`H=${event.humidity_percent}`);
      }
      if (event.lux != null) {
        parts.push(`L=${event.lux}`);
      }
      if (event.presence_detected != null) {
        parts.push(event.presence_detected ? "有人" : "无人");
      }
      fragment.querySelector(".event-meta").textContent = parts.join(" | ") || "暂无更多信息";
      els.list.appendChild(fragment);
    });
  }

  async function loadEvents() {
    const filter = els.filter.value;
    const suffix = filter ? `?event_type=${encodeURIComponent(filter)}` : "";
    const data = await request(`/api/device/events${suffix}`);
    state.events = data.items || [];
    render();
  }

  async function refreshNow() {
    setStatus("saving", "加载中...");
    try {
      await loadEvents();
      setStatus("saved", "已保存");
    } catch (error) {
      window.AppCommon.showLoadError(error);
    }
  }

  els.filter?.addEventListener("change", refreshNow);
  els.refresh?.addEventListener("click", refreshNow);
  bindRetry(() => runPageLoader(loadEvents));
  runPageLoader(loadEvents);
})();
