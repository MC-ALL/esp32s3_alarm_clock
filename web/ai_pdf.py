from __future__ import annotations

import html
from io import BytesIO
from typing import Any

from reportlab.lib.enums import TA_CENTER, TA_LEFT
from reportlab.lib.pagesizes import A4
from reportlab.lib.styles import ParagraphStyle, getSampleStyleSheet
from reportlab.lib.units import mm
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.cidfonts import UnicodeCIDFont
from reportlab.platypus import ListFlowable, ListItem, Paragraph, SimpleDocTemplate, Spacer

PDF_FONT_NAME = "STSong-Light"


def _register_font() -> None:
    try:
        pdfmetrics.getFont(PDF_FONT_NAME)
    except KeyError:
        pdfmetrics.registerFont(UnicodeCIDFont(PDF_FONT_NAME))


def _escape_text(value: Any) -> str:
    return html.escape(str(value or ""))


def _risk_label(value: str) -> str:
    mapping = {
        "low": "低",
        "medium": "中",
        "high": "高",
    }
    return mapping.get(value, value or "--")


def build_health_report_pdf(report: dict[str, Any]) -> bytes:
    report_fields = report.get("report_fields") if isinstance(report, dict) else None
    if not isinstance(report_fields, dict) or not report_fields:
        raise ValueError("report_fields is required")

    _register_font()

    buffer = BytesIO()
    document = SimpleDocTemplate(
        buffer,
        pagesize=A4,
        leftMargin=18 * mm,
        rightMargin=18 * mm,
        topMargin=16 * mm,
        bottomMargin=16 * mm,
        title=str(report_fields.get("report_title", "办公健康分析报告")),
    )

    sample_styles = getSampleStyleSheet()
    title_style = ParagraphStyle(
        "ReportTitleCN",
        parent=sample_styles["Title"],
        fontName=PDF_FONT_NAME,
        fontSize=20,
        leading=26,
        alignment=TA_CENTER,
        spaceAfter=12,
    )
    meta_style = ParagraphStyle(
        "ReportMetaCN",
        parent=sample_styles["BodyText"],
        fontName=PDF_FONT_NAME,
        fontSize=10.5,
        leading=15,
        alignment=TA_CENTER,
        textColor="#555555",
        spaceAfter=12,
    )
    section_title_style = ParagraphStyle(
        "ReportSectionTitleCN",
        parent=sample_styles["Heading2"],
        fontName=PDF_FONT_NAME,
        fontSize=13,
        leading=18,
        alignment=TA_LEFT,
        spaceBefore=10,
        spaceAfter=6,
    )
    body_style = ParagraphStyle(
        "ReportBodyCN",
        parent=sample_styles["BodyText"],
        fontName=PDF_FONT_NAME,
        fontSize=11,
        leading=17,
        alignment=TA_LEFT,
        spaceAfter=4,
    )

    generated_at = str(report.get("generated_at", "") or "--")
    risk_level = _risk_label(str(report_fields.get("risk_level", "") or ""))

    story = [
        Paragraph(_escape_text(report_fields.get("report_title", "办公健康分析报告")), title_style),
        Paragraph(
            _escape_text("生成时间：%s　　风险等级：%s" % (generated_at, risk_level)),
            meta_style,
        ),
    ]

    sections = [
        ("总体结论", report_fields.get("overall_summary", "")),
        ("环境状态分析", report_fields.get("environment_analysis", "")),
        ("连续在位分析", report_fields.get("sedentary_analysis", "")),
        ("待办与作息建议", report_fields.get("todo_and_routine_advice", "")),
    ]
    for title, body in sections:
        story.append(Paragraph(_escape_text(title), section_title_style))
        story.append(Paragraph(_escape_text(body), body_style))

    key_findings = report_fields.get("key_findings") or []
    if key_findings:
        story.append(Paragraph("关键发现", section_title_style))
        story.append(
            ListFlowable(
                [
                    ListItem(Paragraph(_escape_text(item), body_style), leftIndent=8)
                    for item in key_findings
                    if str(item).strip()
                ],
                bulletType="bullet",
                leftIndent=12,
            )
        )

    improvement_actions = report_fields.get("improvement_actions") or []
    if improvement_actions:
        story.append(Spacer(1, 6))
        story.append(Paragraph("改善建议清单", section_title_style))
        story.append(
            ListFlowable(
                [
                    ListItem(Paragraph(_escape_text(item), body_style), leftIndent=8)
                    for item in improvement_actions
                    if str(item).strip()
                ],
                bulletType="bullet",
                leftIndent=12,
            )
        )

    document.build(story)
    return buffer.getvalue()
