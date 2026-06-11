const todosPage = (() => {
  const { uiText, request, setStatus, buildEmptyState, formatTimestamp, bindRetry, runPageLoader } = window.AppCommon;
  const state = {
    activeTodos: [],
    completedTodos: [],
  };
  const els = {
    createForm: document.querySelector("#create-form"),
    todoInput: document.querySelector("#todo-input"),
    formError: document.querySelector("#form-error"),
    pendingList: document.querySelector("#pending-list"),
    completedList: document.querySelector("#completed-list"),
    pendingCount: document.querySelector("#pending-count"),
    completedCount: document.querySelector("#completed-count"),
    todoTemplate: document.querySelector("#todo-item-template"),
    completedTemplate: document.querySelector("#completed-item-template"),
  };

  function updateCounts() {
    els.pendingCount.textContent = String(state.activeTodos.length);
    els.completedCount.textContent = String(state.completedTodos.length);
  }

  function mergeActiveTodo(updated) {
    state.activeTodos = state.activeTodos.map((item) => (item.id === updated.id ? updated : item));
    render();
  }

  async function loadTodos() {
    const data = await request("/api/todos");
    state.activeTodos = data.items || [];
    state.completedTodos = data.completed_items || [];
    render();
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
    render();
    setStatus("saving", uiText.saving);

    try {
      await request("/api/todos/reorder", {
        method: "PUT",
        body: JSON.stringify({ ids: state.activeTodos.map((item) => item.id) }),
      });
      setStatus("saved", uiText.saved);
    } catch (error) {
      await loadTodos();
      setStatus("error", uiText.saveFailed);
    }
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
      render();
      setStatus("saving", uiText.saving);

      try {
        await request(`/api/todos/${item.id}/complete`, { method: "POST" });
        await loadTodos();
        setStatus("saved", uiText.saved);
      } catch (error) {
        state.activeTodos = previousSnapshot;
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
        mergeActiveTodo(updated);
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
      render();
      setStatus("saving", uiText.saving);

      try {
        await request(`/api/todos/${item.id}`, { method: "DELETE" });
        await loadTodos();
        setStatus("saved", uiText.saved);
      } catch (error) {
        state.activeTodos = previousSnapshot;
        render();
        setStatus("error", uiText.saveFailed);
      }
    });

    return fragment;
  }

  function render() {
    els.pendingList.innerHTML = "";
    els.completedList.innerHTML = "";

    if (!state.activeTodos.length) {
      els.pendingList.appendChild(buildEmptyState("当前还没有未完成事项", "先添加一条待办，设备端下次拉取就会看到。"));
    } else {
      state.activeTodos.forEach((item, index, list) => {
        els.pendingList.appendChild(buildTodoItem(item, index, list.length));
      });
    }

    if (!state.completedTodos.length) {
      els.completedList.appendChild(buildEmptyState("暂无已完成记录", "完成后的事项只在 Web 端查看，设备端不会再拉取。"));
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

  els.createForm?.addEventListener("submit", async (event) => {
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
      setStatus("saved", uiText.saved);
    } catch (error) {
      els.formError.hidden = false;
      els.formError.textContent = uiText.saveFailed;
      setStatus("error", uiText.saveFailed);
    }
  });

  bindRetry(() => runPageLoader(loadTodos));
  runPageLoader(loadTodos);
})();
