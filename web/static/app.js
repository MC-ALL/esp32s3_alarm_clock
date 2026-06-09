const state = {
  activeTodos: [],
  completedTodos: [],
  alarms: [],
  voiceSettings: null,
  deviceStatus: null,
  events: [],
  configVersion: null,
  saveTimer: null,
};

const uiText = {
  idle: "空闲",
  loading: "加载中...",
  saving: "保存中...",
  saved: "已保存",
  ready: "就绪",
  saveFailed: "保存失败",
  loadFailed: "加载失败",
  createFailed: "创建失败",
  deleteFailed: "删除失败",
  reorderFailed: "排序失败",
  textRequired: "内容不能为空",
  pendingEmptyTitle: "当前还没有未完成事项",
  pendingEmptyNote: "先添加一条待办，设备端下次拉取就会看到。",
  completedEmptyTitle: "暂无已完成记录",
  completedEmptyNote: "完成后的事项只在 Web 端查看，设备端不会再拉取。",
  eventsEmptyTitle: "暂无事件记录",
  eventsEmptyNote: "设备上报语音或提醒事件后，这里会出现记录。",
  alarmsEmptyTitle: "暂无闹钟",
  alarmsEmptyNote: "可以新增闹钟并保存到 Web 真源。",
};

const els = {
  createForm: document.querySelector("#create-form"),
  todoInput: document.querySelector("#todo-input"),
  formError: document.querySelector("#form-error"),
  saveStatus: document.querySelector("#save-status"),
  loadError: document.querySelector("#load-error"),
  loadErrorDetail: document.querySelector("#load-error-detail"),
  retryLoad: document.querySelector("#retry-load"),
  pendingList: document.querySelector("#pending-list"),
  completedList: document.querySelector("#completed-list"),
  pendingCount: document.querySelector("#pending-count"),
  completedCount: document.querySelector("#completed-count"),
  alarmsList: document.querySelector("#alarms-list"),
  addAlarm: document.querySelector("#add-alarm"),
  saveAlarms: document.querySelector("#save-alarms"),
  saveVoiceSettings: document.querySelector("#save-voice-settings"),
  refreshStatus: document.querySelector("#refresh-status"),
  refreshEvents: document.querySelector("#refresh-events"),
  eventTypeFilter: document.querySelector("#event-type-filter"),
  eventsList: document.querySelector("#events-list"),
  deviceOnline: document.querySelector("#device-online"),
  deviceTemp: document.querySelector("#device-temp"),
  deviceHumi: document.querySelector("#device-humi"),
  deviceLux: document.querySelector("#device-lux"),
  devicePresence: document.querySelector("#device-presence"),
  deviceUpdatedAt: document.querySelector("#device-updated-at"),
  voiceTodo: document.querySelector("#voice-todo"),
  voiceAlarm: document.querySelector("#voice-alarm"),
  voiceEnv: document.querySelector("#voice-env"),
  voiceEnvAlert: document.querySelector("#voice-env-alert"),
  voiceHourChime: document.querySelector("#voice-hour-chime"),
  todoTemplate: document.querySelector("#todo-item-template"),
  completedTemplate: document.querySelector("#completed-item-template"),
  alarmTemplate: document.querySelector("#alarm-item-template"),
  eventTemplate: document.querySelector("#event-item-template"),
};

function setStatus(mode, text) {
  clearTimeout(state.saveTimer);
  els.saveStatus.className = `save-status ${mode}`;
  els.saveStatus.textContent = text;

  if (mode === "saved") {
    state.saveTimer = window.setTimeout(() => {
      els.saveStatus.className = "save-status idle";
      els.saveStatus.textContent = uiText.idle;
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

function updateCounts() {
  els.pendingCount.textContent = String(state.activeTodos.length);
  els.completedCount.textContent = String(state.completedTodos.length);
}

function renderStatus() {
  const status = state.deviceStatus;
  els.deviceOnline.textContent = status ? (status.online ? "在线" : "离线") : "--";
  els.deviceTemp.textContent = status && status.temperature_c != null ? `${status.temperature_c.toFixed(1)}C` : "--";
  els.deviceHumi.textContent = status && status.humidity_percent != null ? `${status.humidity_percent.toFixed(0)}%` : "--";
  els.deviceLux.textContent = status && status.lux != null ? `${status.lux.toFixed(0)}` : "--";
  els.devicePresence.textContent = status && status.presence_detected != null ? (status.presence_detected ? "有人" : "无人") : "--";
  els.deviceUpdatedAt.textContent = status ? formatTimestamp(status.updated_at) : "--";
}

function buildTodoItem(item, index, total) {
  const fragment = els.todoTemplate.content.cloneNode(true);
  const root = fragment.querySelector(".todo-item");
  const checkbox = fragment.querySelector(".todo-check");
  const textInput = fragment.querySelector(".todo-text");
  const moveUp = fragment.querySelector(".move-up");
  const moveDown = fragment.querySelector(".move-down");
  const deleteBtn = fragment.querySelector(".delete");

  root.dataset.id = item.id;
  checkbox.checked = false;
  textInput.value = item.text;
  textInput.dataset.previousValue = item.text;
  moveUp.disabled = index <= 0;
  moveDown.disabled = index >= total - 1;

  checkbox.addEventListener("change", async () => {
    const previousSnapshot = [...state.activeTodos];
    state.activeTodos = state.activeTodos.filter((entry) => entry.id !== item.id);
    renderTodos();
    setStatus("saving", uiText.saving);

    try {
      await request(`/api/todos/${item.id}/complete`, { method: "POST" });
      await loadConfig();
      await loadTodos();
      setStatus("saved", uiText.saved);
    } catch (error) {
      state.activeTodos = previousSnapshot;
      renderTodos();
      setStatus("error", uiText.saveFailed);
    }
  });

  textInput.addEventListener("focus", () => {
    textInput.dataset.previousValue = textInput.value;
  });

  textInput.addEventListener("keydown", (event) => {
    if (event.key === "Enter") {
      event.preventDefault();
      textInput.blur();
    }
  });

  textInput.addEventListener("blur", async () => {
    const nextValue = textInput.value.trim();
    const previousValue = textInput.dataset.previousValue || item.text;

    if (nextValue === previousValue) {
      textInput.value = previousValue;
      return;
    }
    if (!nextValue) {
      textInput.value = previousValue;
      setStatus("error", uiText.textRequired);
      return;
    }

    textInput.value = nextValue;
    setStatus("saving", uiText.saving);

    try {
      const updated = await request(`/api/todos/${item.id}`, {
        method: "PUT",
        body: JSON.stringify({ text: nextValue }),
      });
      mergeActiveTodo(updated);
      await loadConfig();
      setStatus("saved", uiText.saved);
    } catch (error) {
      textInput.value = previousValue;
      mergeActiveTodo({ ...item, text: previousValue });
      setStatus("error", uiText.saveFailed);
    }
  });

  moveUp.addEventListener("click", () => reorderActiveTodo(item.id, -1));
  moveDown.addEventListener("click", () => reorderActiveTodo(item.id, 1));

  deleteBtn.addEventListener("click", async () => {
    const previousSnapshot = [...state.activeTodos];
    state.activeTodos = state.activeTodos.filter((entry) => entry.id !== item.id);
    renderTodos();
    setStatus("saving", uiText.saving);

    try {
      await request(`/api/todos/${item.id}`, { method: "DELETE" });
      await loadConfig();
      setStatus("saved", uiText.saved);
    } catch (error) {
      state.activeTodos = previousSnapshot;
      renderTodos();
      setStatus("error", uiText.deleteFailed);
    }
  });

  return fragment;
}

function renderTodos() {
  els.pendingList.innerHTML = "";
  els.completedList.innerHTML = "";

  if (!state.activeTodos.length) {
    els.pendingList.appendChild(buildEmptyState(uiText.pendingEmptyTitle, uiText.pendingEmptyNote));
  } else {
    state.activeTodos.forEach((item, index, list) => {
      els.pendingList.appendChild(buildTodoItem(item, index, list.length));
    });
  }

  if (!state.completedTodos.length) {
    els.completedList.appendChild(buildEmptyState(uiText.completedEmptyTitle, uiText.completedEmptyNote));
  } else {
    state.completedTodos.forEach((item) => {
      const fragment = els.completedTemplate.content.cloneNode(true);
      fragment.querySelector(".readonly-text").textContent = item.text;
      fragment.querySelector(".readonly-meta").textContent = `完成于 ${formatTimestamp(item.completed_at || item.updated_at)}`;
      els.completedList.appendChild(fragment);
    });
  }

  updateCounts();
}

function mergeActiveTodo(updated) {
  state.activeTodos = state.activeTodos.map((item) => (item.id === updated.id ? updated : item));
  renderTodos();
}

async function reorderActiveTodo(todoId, direction) {
  const snapshot = [...state.activeTodos];
  const index = snapshot.findIndex((item) => item.id === todoId);
  const targetIndex = index + direction;
  if (index < 0 || targetIndex < 0 || targetIndex >= snapshot.length) {
    return;
  }

  [snapshot[index], snapshot[targetIndex]] = [snapshot[targetIndex], snapshot[index]];
  state.activeTodos = snapshot;
  renderTodos();
  setStatus("saving", uiText.saving);

  try {
    await request("/api/todos/reorder", {
      method: "PUT",
      body: JSON.stringify({ ids: state.activeTodos.map((item) => item.id) }),
    });
    await loadConfig();
    setStatus("saved", uiText.saved);
  } catch (error) {
    await loadTodos();
    setStatus("error", uiText.reorderFailed);
  }
}

function alarmDraftFromState() {
  return state.alarms.map((alarm) => ({ ...alarm }));
}

function renderAlarms() {
  els.alarmsList.innerHTML = "";

  if (!state.alarms.length) {
    els.alarmsList.appendChild(buildEmptyState(uiText.alarmsEmptyTitle, uiText.alarmsEmptyNote));
    return;
  }

  state.alarms.forEach((alarm, index) => {
    const fragment = els.alarmTemplate.content.cloneNode(true);
    const hourInput = fragment.querySelector(".alarm-hour");
    const minuteInput = fragment.querySelector(".alarm-minute");
    const repeatInput = fragment.querySelector(".alarm-repeat");
    const enabledInput = fragment.querySelector(".alarm-enabled");
    const voiceInput = fragment.querySelector(".alarm-voice");
    const removeButton = fragment.querySelector(".remove-alarm");

    hourInput.value = String(alarm.hour);
    minuteInput.value = String(alarm.minute);
    repeatInput.checked = Boolean(alarm.repeat);
    enabledInput.checked = Boolean(alarm.enabled);
    voiceInput.checked = Boolean(alarm.voice);

    hourInput.addEventListener("input", () => {
      state.alarms[index].hour = Number(hourInput.value || 0);
    });
    minuteInput.addEventListener("input", () => {
      state.alarms[index].minute = Number(minuteInput.value || 0);
    });
    repeatInput.addEventListener("change", () => {
      state.alarms[index].repeat = repeatInput.checked;
    });
    enabledInput.addEventListener("change", () => {
      state.alarms[index].enabled = enabledInput.checked;
    });
    voiceInput.addEventListener("change", () => {
      state.alarms[index].voice = voiceInput.checked;
    });
    removeButton.addEventListener("click", () => {
      state.alarms.splice(index, 1);
      renderAlarms();
    });

    els.alarmsList.appendChild(fragment);
  });
}

function renderVoiceSettings() {
  const settings = state.voiceSettings;
  if (!settings) {
    return;
  }
  els.voiceTodo.checked = Boolean(settings.todo_voice_on);
  els.voiceAlarm.checked = Boolean(settings.alarm_voice_on);
  els.voiceEnv.checked = Boolean(settings.env_voice_on);
  els.voiceEnvAlert.checked = Boolean(settings.env_alert_on);
  els.voiceHourChime.checked = Boolean(settings.home_hour_chime_on);
}

function currentVoiceSettingsPayload() {
  return {
    todo_voice_on: els.voiceTodo.checked,
    alarm_voice_on: els.voiceAlarm.checked,
    env_voice_on: els.voiceEnv.checked,
    env_alert_on: els.voiceEnvAlert.checked,
    home_hour_chime_on: els.voiceHourChime.checked,
  };
}

function renderEvents() {
  els.eventsList.innerHTML = "";
  if (!state.events.length) {
    els.eventsList.appendChild(buildEmptyState(uiText.eventsEmptyTitle, uiText.eventsEmptyNote));
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
    fragment.querySelector(".event-meta").textContent = parts.join(" | ") || "无附加信息";
    els.eventsList.appendChild(fragment);
  });
}

async function loadTodos() {
  const data = await request("/api/todos");
  state.activeTodos = data.items || [];
  state.completedTodos = data.completed_items || [];
  renderTodos();
}

async function loadConfig() {
  const data = await request("/api/device/config");
  state.configVersion = data.config_version;
  state.alarms = data.alarms || [];
  state.voiceSettings = data.voice_settings || null;
  renderAlarms();
  renderVoiceSettings();
}

async function loadStatus() {
  const data = await request("/api/device/status");
  state.deviceStatus = data;
  renderStatus();
}

async function loadEvents() {
  const filter = els.eventTypeFilter.value;
  const suffix = filter ? `?event_type=${encodeURIComponent(filter)}` : "";
  const data = await request(`/api/device/events${suffix}`);
  state.events = data.items || [];
  renderEvents();
}

async function loadAll() {
  els.loadError.hidden = true;
  els.loadErrorDetail.hidden = true;
  els.loadErrorDetail.textContent = "";
  setStatus("saving", uiText.loading);

  try {
    await Promise.all([loadTodos(), loadConfig(), loadStatus(), loadEvents()]);
    setStatus("saved", uiText.ready);
  } catch (error) {
    els.loadError.hidden = false;
    els.loadErrorDetail.hidden = false;
    els.loadErrorDetail.textContent = String(error && error.message ? error.message : error);
    setStatus("error", uiText.loadFailed);
  }
}

els.createForm.addEventListener("submit", async (event) => {
  event.preventDefault();
  const text = els.todoInput.value.trim();
  if (!text) {
    els.formError.hidden = false;
    els.formError.textContent = uiText.textRequired;
    return;
  }

  els.formError.hidden = true;
  setStatus("saving", uiText.saving);

  try {
    await request("/api/todos", {
      method: "POST",
      body: JSON.stringify({ text }),
    });
    els.todoInput.value = "";
    await loadTodos();
    await loadConfig();
    setStatus("saved", uiText.saved);
  } catch (error) {
    els.formError.hidden = false;
    els.formError.textContent = uiText.createFailed;
    setStatus("error", uiText.createFailed);
  }
});

els.addAlarm.addEventListener("click", () => {
  state.alarms.push({ hour: 7, minute: 30, repeat: true, enabled: true, voice: true });
  renderAlarms();
});

els.saveAlarms.addEventListener("click", async () => {
  setStatus("saving", uiText.saving);
  try {
    const response = await request("/api/device/alarms", {
      method: "PUT",
      body: JSON.stringify({ config_version: state.configVersion, alarms: alarmDraftFromState() }),
    });
    state.configVersion = response.config_version;
    state.alarms = response.alarms || [];
    renderAlarms();
    setStatus("saved", uiText.saved);
  } catch (error) {
    setStatus("error", uiText.saveFailed);
    await loadConfig();
  }
});

els.saveVoiceSettings.addEventListener("click", async () => {
  setStatus("saving", uiText.saving);
  try {
    const response = await request("/api/device/voice-settings", {
      method: "PUT",
      body: JSON.stringify({ config_version: state.configVersion, voice_settings: currentVoiceSettingsPayload() }),
    });
    state.configVersion = response.config_version;
    state.voiceSettings = response.voice_settings || currentVoiceSettingsPayload();
    renderVoiceSettings();
    setStatus("saved", uiText.saved);
  } catch (error) {
    setStatus("error", uiText.saveFailed);
    await loadConfig();
  }
});

els.refreshStatus.addEventListener("click", () => {
  loadStatus().catch((error) => setStatus("error", String(error && error.message ? error.message : error)));
});

els.refreshEvents.addEventListener("click", () => {
  loadEvents().catch((error) => setStatus("error", String(error && error.message ? error.message : error)));
});

els.eventTypeFilter.addEventListener("change", () => {
  loadEvents().catch((error) => setStatus("error", String(error && error.message ? error.message : error)));
});

els.retryLoad.addEventListener("click", () => {
  loadAll();
});

loadAll();
