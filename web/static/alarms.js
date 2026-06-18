const alarmsPage = (() => {
  const { request, setStatus, buildEmptyState, bindRetry, runPageLoader, uiText } = window.AppCommon;
  const state = {
    alarms: [],
    configVersion: null,
  };
  const els = {
    alarmsList: document.querySelector("#alarms-list"),
    addAlarm: document.querySelector("#add-alarm"),
    saveAlarms: document.querySelector("#save-alarms"),
    alarmTemplate: document.querySelector("#alarm-item-template"),
  };

  async function loadConfig() {
    const data = await request("/api/device/config");
    state.configVersion = data.config_version;
    state.alarms = data.alarms || [];
    render();
  }

  function render() {
    els.alarmsList.innerHTML = "";
    if (!state.alarms.length) {
      els.alarmsList.appendChild(buildEmptyState("还没有闹钟", "先新增一个提醒时间吧。"));
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
        render();
      });

      els.alarmsList.appendChild(fragment);
    });
  }

  els.addAlarm?.addEventListener("click", () => {
    state.alarms.push({ hour: 7, minute: 30, repeat: true, enabled: true, voice: true });
    render();
  });

  els.saveAlarms?.addEventListener("click", async () => {
    setStatus("saving", uiText.saving);
    try {
      const response = await request("/api/device/alarms", {
        method: "PUT",
        body: JSON.stringify({ config_version: state.configVersion, alarms: state.alarms.map((alarm) => ({ ...alarm })) }),
      });
      state.configVersion = response.config_version;
      state.alarms = response.alarms || [];
      render();
      setStatus("saved", uiText.saved);
    } catch (error) {
      await loadConfig();
      window.AppCommon.showLoadError(error);
    }
  });

  bindRetry(() => runPageLoader(loadConfig));
  runPageLoader(loadConfig);
})();
