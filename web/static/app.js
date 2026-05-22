const state = {
  items: [],
  completedExpanded: false,
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
  pendingEmptyTitle: "当前还没有内容",
  pendingEmptyNote: "先添加一条待办，时钟就能在下一次拉取时看到它。",
  completedEmptyTitle: "暂时没有已完成项目",
  completedEmptyNote: "完成后的事项会被收进这里，默认保持折叠。",
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
  completedToggle: document.querySelector("#completed-toggle"),
  completedWrapper: document.querySelector("#completed-wrapper"),
  template: document.querySelector("#todo-item-template"),
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

function pendingItems() {
  return state.items.filter((item) => !item.done);
}

function completedItems() {
  return state.items.filter((item) => item.done);
}

function updateCounts() {
  els.pendingCount.textContent = String(pendingItems().length);
  els.completedCount.textContent = String(completedItems().length);
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

function render() {
  const pending = pendingItems();
  const completed = completedItems();

  els.pendingList.innerHTML = "";
  els.completedList.innerHTML = "";

  if (!pending.length) {
    els.pendingList.appendChild(buildEmptyState(uiText.pendingEmptyTitle, uiText.pendingEmptyNote));
  } else {
    pending.forEach((item, index, list) => {
      els.pendingList.appendChild(buildTodoItem(item, index, list.length, false));
    });
  }

  if (!completed.length) {
    els.completedList.appendChild(buildEmptyState(uiText.completedEmptyTitle, uiText.completedEmptyNote));
  } else {
    completed.forEach((item) => {
      els.completedList.appendChild(buildTodoItem(item, -1, -1, true));
    });
  }

  updateCounts();
  syncCompletedPanel();
}

function syncCompletedPanel() {
  els.completedToggle.setAttribute("aria-expanded", String(state.completedExpanded));
  els.completedWrapper.classList.toggle("collapsed", !state.completedExpanded);
}

function buildTodoItem(item, index, total, isCompleted) {
  const fragment = els.template.content.cloneNode(true);
  const root = fragment.querySelector(".todo-item");
  const checkbox = fragment.querySelector(".todo-check");
  const textInput = fragment.querySelector(".todo-text");
  const moveUp = fragment.querySelector(".move-up");
  const moveDown = fragment.querySelector(".move-down");
  const deleteBtn = fragment.querySelector(".delete");

  root.dataset.id = item.id;
  if (item.done) {
    root.classList.add("done");
  }

  checkbox.checked = item.done;
  textInput.value = item.text;
  textInput.dataset.previousValue = item.text;

  if (isCompleted) {
    moveUp.disabled = true;
    moveDown.disabled = true;
  } else {
    moveUp.disabled = index <= 0;
    moveDown.disabled = index >= total - 1;
  }

  checkbox.addEventListener("change", async () => {
    const previous = item.done;
    item.done = checkbox.checked;
    render();
    setStatus("saving", uiText.saving);

    try {
      const updated = await request(`/api/todos/${item.id}`, {
        method: "PUT",
        body: JSON.stringify({ done: item.done }),
      });
      mergeItem(updated);
      setStatus("saved", uiText.saved);
    } catch (error) {
      item.done = previous;
      render();
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
      mergeItem(updated);
      setStatus("saved", uiText.saved);
    } catch (error) {
      textInput.value = previousValue;
      mergeItem({ ...item, text: previousValue });
      setStatus("error", uiText.saveFailed);
    }
  });

  moveUp.addEventListener("click", () => reorderPending(item.id, -1));
  moveDown.addEventListener("click", () => reorderPending(item.id, 1));

  deleteBtn.addEventListener("click", async () => {
    const snapshot = [...state.items];
    state.items = state.items.filter((entry) => entry.id !== item.id);
    render();
    setStatus("saving", uiText.saving);

    try {
      await request(`/api/todos/${item.id}`, { method: "DELETE" });
      setStatus("saved", uiText.saved);
    } catch (error) {
      state.items = snapshot;
      render();
      setStatus("error", uiText.deleteFailed);
    }
  });

  return fragment;
}

function mergeItem(updated) {
  state.items = state.items.map((item) => (item.id === updated.id ? updated : item));
  render();
}

async function reorderPending(todoId, direction) {
  const snapshot = [...state.items];
  const pending = pendingItems();
  const completed = completedItems();
  const index = pending.findIndex((item) => item.id === todoId);
  const targetIndex = index + direction;

  if (index < 0 || targetIndex < 0 || targetIndex >= pending.length) {
    return;
  }

  [pending[index], pending[targetIndex]] = [pending[targetIndex], pending[index]];
  state.items = [...pending, ...completed];
  render();
  setStatus("saving", uiText.saving);

  try {
    await request("/api/todos/reorder", {
      method: "PUT",
      body: JSON.stringify({ ids: state.items.map((item) => item.id) }),
    });
    setStatus("saved", uiText.saved);
  } catch (error) {
    state.items = snapshot;
    render();
    setStatus("error", uiText.reorderFailed);
  }
}

async function loadTodos() {
  els.loadError.hidden = true;
  els.loadErrorDetail.hidden = true;
  els.loadErrorDetail.textContent = "";
  els.loadError.style.display = "none";
  els.loadErrorDetail.style.display = "none";
  setStatus("saving", uiText.loading);

  try {
    const data = await request("/api/todos", { method: "GET" });
    state.items = data.items || [];
    render();
    els.loadError.hidden = true;
    els.loadErrorDetail.hidden = true;
    els.loadErrorDetail.textContent = "";
    els.loadError.style.display = "none";
    els.loadErrorDetail.style.display = "none";
    setStatus("saved", uiText.ready);
  } catch (error) {
    console.error("loadTodos failed", error);
    els.loadError.hidden = false;
    els.loadErrorDetail.hidden = false;
    els.loadError.style.display = "";
    els.loadErrorDetail.style.display = "";
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
    const item = await request("/api/todos", {
      method: "POST",
      body: JSON.stringify({ text }),
    });
    state.items.push(item);
    els.todoInput.value = "";
    render();
    setStatus("saved", uiText.saved);
  } catch (error) {
    els.formError.hidden = false;
    els.formError.textContent = uiText.createFailed;
    setStatus("error", uiText.createFailed);
  }
});

els.completedToggle.addEventListener("click", () => {
  state.completedExpanded = !state.completedExpanded;
  syncCompletedPanel();
});

els.retryLoad.addEventListener("click", () => {
  loadTodos();
});

loadTodos();
