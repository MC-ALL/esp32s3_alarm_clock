const modelPage = (() => {
  const { request, bindRetry, runPageLoader, setStatus, uiText } = window.AppCommon;
  const state = {
    messages: [],
    latestReport: null,
    sending: false,
    generatingReport: false,
    clearingMemory: false,
  };

  const els = {
    chatEmpty: document.querySelector("#model-chat-empty"),
    chatList: document.querySelector("#model-chat-list"),
    chatForm: document.querySelector("#model-chat-form"),
    messageInput: document.querySelector("#model-message"),
    sendButton: document.querySelector("#send-model-message"),
    clearMemoryButton: document.querySelector("#clear-model-memory"),
    inlineError: document.querySelector("#model-inline-error"),
    generateReportButton: document.querySelector("#generate-model-report"),
    downloadReportButton: document.querySelector("#download-model-report"),
    reportEmpty: document.querySelector("#model-report-empty"),
    reportCard: document.querySelector("#model-report-card"),
    reportTitle: document.querySelector("#report-title"),
    reportRiskLevel: document.querySelector("#report-risk-level"),
    reportGeneratedAt: document.querySelector("#report-generated-at"),
    reportOverallSummary: document.querySelector("#report-overall-summary"),
    reportEnvironmentAnalysis: document.querySelector("#report-environment-analysis"),
    reportSedentaryAnalysis: document.querySelector("#report-sedentary-analysis"),
    reportTodoAdvice: document.querySelector("#report-todo-advice"),
    reportKeyFindings: document.querySelector("#report-key-findings"),
    reportImprovementActions: document.querySelector("#report-improvement-actions"),
  };

  function setInlineError(message) {
    if (!els.inlineError) {
      return;
    }
    const text = String(message || "").trim();
    els.inlineError.hidden = !text;
    els.inlineError.textContent = text;
  }

  function escapeHtml(value) {
    return String(value || "")
      .replaceAll("&", "&amp;")
      .replaceAll("<", "&lt;")
      .replaceAll(">", "&gt;")
      .replaceAll('"', "&quot;");
  }

  function riskLabel(value) {
    const mapping = {
      low: "低",
      medium: "中",
      high: "高",
    };
    return mapping[value] || value || "--";
  }

  function renderChat() {
    if (!els.chatList || !els.chatEmpty) {
      return;
    }

    if (!state.messages.length) {
      els.chatEmpty.hidden = false;
      els.chatList.hidden = true;
      els.chatList.innerHTML = "";
      return;
    }

    els.chatEmpty.hidden = true;
    els.chatList.hidden = false;
    els.chatList.innerHTML = state.messages
      .map(
        (message) => `
          <article class="chat-bubble ${message.role === "user" ? "chat-bubble-user" : "chat-bubble-ai"}">
            <div class="chat-bubble-head">
              <span>${message.role === "user" ? "你" : "助手"}</span>
            </div>
            <p>${escapeHtml(message.text)}</p>
          </article>
        `,
      )
      .join("");
    els.chatList.scrollTop = els.chatList.scrollHeight;
  }

  function renderBulletList(container, items) {
    if (!container) {
      return;
    }
    const normalized = Array.isArray(items) ? items.filter((item) => String(item || "").trim()) : [];
    container.innerHTML = normalized.map((item) => `<li>${escapeHtml(item)}</li>`).join("");
  }

  function renderReport() {
    if (!els.reportCard || !els.reportEmpty || !els.downloadReportButton) {
      return;
    }

    if (!state.latestReport || !state.latestReport.report_fields) {
      els.reportEmpty.hidden = false;
      els.reportCard.hidden = true;
      els.downloadReportButton.disabled = true;
      return;
    }

    const report = state.latestReport.report_fields;
    els.reportEmpty.hidden = true;
    els.reportCard.hidden = false;
    els.downloadReportButton.disabled = false;
    els.reportTitle.textContent = report.report_title || "办公健康分析报告";
    els.reportRiskLevel.textContent = riskLabel(report.risk_level);
    els.reportGeneratedAt.textContent = state.latestReport.generated_at || "--";
    els.reportOverallSummary.textContent = report.overall_summary || "--";
    els.reportEnvironmentAnalysis.textContent = report.environment_analysis || "--";
    els.reportSedentaryAnalysis.textContent = report.sedentary_analysis || "--";
    els.reportTodoAdvice.textContent = report.todo_and_routine_advice || "--";
    renderBulletList(els.reportKeyFindings, report.key_findings);
    renderBulletList(els.reportImprovementActions, report.improvement_actions);
  }

  function updateControls() {
    const busy = state.sending || state.generatingReport || state.clearingMemory;
    if (els.sendButton) {
      els.sendButton.disabled = busy;
    }
    if (els.messageInput) {
      els.messageInput.disabled = busy;
    }
    if (els.generateReportButton) {
      els.generateReportButton.disabled = busy;
    }
    if (els.clearMemoryButton) {
      els.clearMemoryButton.disabled = busy;
    }
    if (els.downloadReportButton) {
      els.downloadReportButton.disabled = busy || !state.latestReport;
    }
  }

  async function loadLatestReport() {
    try {
      state.latestReport = await request("/api/model/report");
    } catch (error) {
      const detail = String(error && error.message ? error.message : error);
      if (detail === "health report not found") {
        state.latestReport = null;
        return;
      }
      throw error;
    }
  }

  async function bootstrap() {
    state.messages = [];
    renderChat();
    await loadLatestReport();
    renderReport();
  }

  async function submitChat(event) {
    event.preventDefault();
    if (state.sending || state.generatingReport || state.clearingMemory) {
      return;
    }

    const message = String(els.messageInput?.value || "").trim();
    if (!message) {
      setInlineError(uiText.textRequired);
      return;
    }

    setInlineError("");
    state.sending = true;
    updateControls();
    setStatus("saving", "发送中...");
    state.messages.push({ role: "user", text: message });
    renderChat();

    try {
      const data = await request("/api/model/chat", {
        method: "POST",
        body: JSON.stringify({ message }),
      });
      state.messages.push({ role: "assistant", text: data.answer_text || "本轮没有返回内容。" });
      if (els.messageInput) {
        els.messageInput.value = "";
      }
      renderChat();
      setStatus("saved", "就绪");
    } catch (error) {
      state.messages.pop();
      renderChat();
      setInlineError(error.message || "模型服务暂时不可用，请稍后重试");
      setStatus("error", "发送失败");
    } finally {
      state.sending = false;
      updateControls();
    }
  }

  async function clearMemory() {
    if (state.sending || state.generatingReport || state.clearingMemory) {
      return;
    }
    const confirmed = window.confirm("确认清空本地对话缓存吗？这会删除后端保存的隐藏概括记录。");
    if (!confirmed) {
      return;
    }

    setInlineError("");
    state.clearingMemory = true;
    updateControls();
    setStatus("saving", "清空中...");
    try {
      await request("/api/model/memory", {
        method: "DELETE",
      });
      state.messages = [];
      renderChat();
      setStatus("saved", "缓存已清空");
    } catch (error) {
      setInlineError(error.message || "清空对话缓存失败");
      setStatus("error", "清空失败");
    } finally {
      state.clearingMemory = false;
      updateControls();
    }
  }

  async function generateReport() {
    if (state.sending || state.generatingReport || state.clearingMemory) {
      return;
    }

    setInlineError("");
    state.generatingReport = true;
    updateControls();
    setStatus("saving", "生成报告中...");
    const hadPreviousReport = Boolean(state.latestReport);
    try {
      const data = await request("/api/model/report/generate", {
        method: "POST",
        body: JSON.stringify({}),
      });
      state.latestReport = data;
      renderReport();
      setStatus("saved", "报告已更新");
    } catch (error) {
      setInlineError(
        error.message ||
          (hadPreviousReport ? "本次报告生成失败，已保留上一次报告" : "本次报告生成失败，请稍后重试"),
      );
      setStatus("error", "报告失败");
    } finally {
      state.generatingReport = false;
      updateControls();
    }
  }

  async function downloadReportPdf() {
    if (!state.latestReport || state.sending || state.generatingReport || state.clearingMemory) {
      return;
    }

    setInlineError("");
    setStatus("saving", "导出 PDF...");
    try {
      const response = await fetch("/api/model/report/pdf");
      if (!response.ok) {
        let detail = "PDF 导出失败";
        try {
          const data = await response.json();
          detail = data.detail || detail;
        } catch {
          detail = response.statusText || detail;
        }
        throw new Error(detail);
      }

      const blob = await response.blob();
      const objectUrl = window.URL.createObjectURL(blob);
      const anchor = document.createElement("a");
      anchor.href = objectUrl;
      anchor.download = "smart-clock-health-report.pdf";
      document.body.appendChild(anchor);
      anchor.click();
      anchor.remove();
      window.URL.revokeObjectURL(objectUrl);
      setStatus("saved", "PDF 已导出");
    } catch (error) {
      setInlineError(error.message || "PDF 导出失败");
      setStatus("error", "导出失败");
    }
  }

  bindRetry(() => runPageLoader(bootstrap));
  els.chatForm?.addEventListener("submit", submitChat);
  els.clearMemoryButton?.addEventListener("click", clearMemory);
  els.generateReportButton?.addEventListener("click", generateReport);
  els.downloadReportButton?.addEventListener("click", downloadReportPdf);
  updateControls();
  runPageLoader(bootstrap);
})();
