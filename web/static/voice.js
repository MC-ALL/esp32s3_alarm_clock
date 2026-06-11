const voicePage = (() => {
  const { request, setStatus, bindRetry, runPageLoader, uiText } = window.AppCommon;
  const state = {
    configVersion: null,
    voiceSettings: null,
  };
  const els = {
    save: document.querySelector("#save-voice-settings"),
    voiceTodo: document.querySelector("#voice-todo"),
    voiceAlarm: document.querySelector("#voice-alarm"),
    voiceEnv: document.querySelector("#voice-env"),
    voiceEnvAlert: document.querySelector("#voice-env-alert"),
    voiceHourChime: document.querySelector("#voice-hour-chime"),
  };

  function render() {
    if (!state.voiceSettings) {
      return;
    }
    els.voiceTodo.checked = Boolean(state.voiceSettings.todo_voice_on);
    els.voiceAlarm.checked = Boolean(state.voiceSettings.alarm_voice_on);
    els.voiceEnv.checked = Boolean(state.voiceSettings.env_voice_on);
    els.voiceEnvAlert.checked = Boolean(state.voiceSettings.env_alert_on);
    els.voiceHourChime.checked = Boolean(state.voiceSettings.home_hour_chime_on);
  }

  function currentPayload() {
    return {
      todo_voice_on: els.voiceTodo.checked,
      alarm_voice_on: els.voiceAlarm.checked,
      env_voice_on: els.voiceEnv.checked,
      env_alert_on: els.voiceEnvAlert.checked,
      home_hour_chime_on: els.voiceHourChime.checked,
    };
  }

  async function loadConfig() {
    const data = await request("/api/device/config");
    state.configVersion = data.config_version;
    state.voiceSettings = data.voice_settings || null;
    render();
  }

  els.save?.addEventListener("click", async () => {
    setStatus("saving", uiText.saving);
    try {
      const response = await request("/api/device/voice-settings", {
        method: "PUT",
        body: JSON.stringify({ config_version: state.configVersion, voice_settings: currentPayload() }),
      });
      state.configVersion = response.config_version;
      state.voiceSettings = response.voice_settings || currentPayload();
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
