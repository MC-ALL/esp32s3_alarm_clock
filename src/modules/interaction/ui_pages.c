#include <ui_pages.h>

#include <environment_service.h>
#include <net_service.h>
#include <presence_service.h>
#include <sync_service.h>
#include <ui_alarm_view.h>
#include <ui_format.h>
#include <ui_layout.h>
#include <ui_style.h>
#include <ui_todo_view.h>

#include <stdio.h>

static lv_obj_t *titled_scroll_body(lv_obj_t *screen, const char *title, lv_color_t bg)
{
	ui_box(screen, 0, 0, UI_SCREEN_W, UI_CONTENT_H, bg);
	ui_label(screen, title, UI_FONT_36, ui_color_white(), 4, 4, 232, 44, LV_TEXT_ALIGN_CENTER);
	return ui_scroll_area(screen, 0, 50, UI_SCREEN_W, UI_CONTENT_H - 50, bg);
}

void ui_pages_render_home(lv_obj_t *screen, const ui_pages_state_t *state)
{
	if (state == NULL) {
		return;
	}

	char time_text[16];
	char period_text[4];
	char date_text[16];
	app_todo_snapshot_t todo = { 0 };
	(void)sync_service_get_todo_snapshot(&todo);
	ui_format_time(time_text, sizeof(time_text), state->use_24h, false);
	ui_format_time_period(period_text, sizeof(period_text), state->use_24h);
	ui_format_date(date_text, sizeof(date_text));

	if (state->home_clock_only) {
		ui_box(screen, 0, 0, UI_SCREEN_W, UI_CONTENT_H, ui_color_blue());
		ui_label(screen, time_text, UI_FONT_48, ui_color_white(), state->use_24h ? 2 : 10, 62,
			 state->use_24h ? 236 : 188, 64, LV_TEXT_ALIGN_CENTER);
		if (!state->use_24h) {
			ui_label(screen, period_text, UI_FONT_24, ui_color_white(), 190, 82, 42, 26,
				 LV_TEXT_ALIGN_LEFT);
		}
		ui_label(screen, date_text, UI_FONT_30, ui_color_white(), 2, 134, 236, 40,
			 LV_TEXT_ALIGN_CENTER);
		return;
	}

	ui_box(screen, 0, 0, UI_SCREEN_W, UI_TILE_H, ui_color_blue());
	ui_label(screen, time_text, UI_FONT_48, ui_color_white(), state->use_24h ? 2 : 10, 22,
		 state->use_24h ? 236 : 188, 62, LV_TEXT_ALIGN_CENTER);
	if (!state->use_24h) {
		ui_label(screen, period_text, UI_FONT_24, ui_color_white(), 190, 42, 42, 26, LV_TEXT_ALIGN_LEFT);
	}
	ui_label(screen, date_text, UI_FONT_24, ui_color_white(), 2, 82, 236, 32, LV_TEXT_ALIGN_CENTER);

	ui_box(screen, 0, UI_TILE_H, UI_TILE_W, UI_TILE_H, ui_color_orange());
	ui_label(screen, LV_SYMBOL_BELL, UI_FONT_32, ui_color_white(), 2, UI_TILE_H + 10, 116, 36,
		 LV_TEXT_ALIGN_CENTER);
	const ui_alarm_item_t *alarm = ui_alarm_next_enabled(state->alarms, state->alarm_count);
	char alarm_text[16];
	if (alarm != NULL) {
		ui_format_alarm_time(alarm->hour, alarm->minute, alarm_text, sizeof(alarm_text));
	} else {
		snprintf(alarm_text, sizeof(alarm_text), "--:--");
	}
	ui_label(screen, alarm_text, UI_FONT_40, ui_color_white(), 2, UI_TILE_H + 58, 116, 50,
		 LV_TEXT_ALIGN_CENTER);

	ui_box(screen, UI_TILE_W, UI_TILE_H, UI_TILE_W, UI_TILE_H, ui_color_purple());
	ui_label(screen, LV_SYMBOL_OK, UI_FONT_32, ui_color_white(), UI_TILE_W + 2, UI_TILE_H + 10, 116, 36,
		 LV_TEXT_ALIGN_CENTER);
	char count_text[8];
	snprintf(count_text, sizeof(count_text), "%u", (unsigned)ui_todo_open_count(&todo));
	ui_label(screen, count_text, UI_FONT_40, ui_color_white(), UI_TILE_W + 2, UI_TILE_H + 58, 116, 50,
		 LV_TEXT_ALIGN_CENTER);
}

void ui_pages_render_alarm(lv_obj_t *screen, const ui_pages_state_t *state)
{
	if (state == NULL) {
		return;
	}

	lv_obj_t *area = titled_scroll_body(screen, "ALARM", ui_color_orange());
	uint8_t focus = state->alarm_page_focus != NULL ? *state->alarm_page_focus : 0;
	if (state->alarm_count > 0U && focus >= state->alarm_count) {
		focus = 0;
		if (state->alarm_page_focus != NULL) {
			*state->alarm_page_focus = focus;
		}
	}

	for (uint8_t i = 0; i < state->alarm_count; i++) {
		char text[32];
		char time_text[16];
		ui_format_alarm_time(state->alarms[i].hour, state->alarms[i].minute, time_text, sizeof(time_text));
		snprintf(text, sizeof(text), "%s %s", time_text, state->alarms[i].enabled ? "ON" : "OFF");
		const bool selected = i == focus;
		lv_color_t bg = selected ? ui_color_white() : lv_color_hex(0x9f4300);
		lv_color_t fg = selected ? ui_color_black() : ui_color_white();
		ui_box(area, 4, 4 + i * 62, 228, 52, bg);
		ui_label(area, text, UI_FONT_36, fg, 8, 11 + i * 62, 220, 42, LV_TEXT_ALIGN_CENTER);
	}

	if (state->alarm_count == 0U) {
		ui_label(area, "NO ALARM", UI_FONT_40, ui_color_white(), 4, 44, 232, 50, LV_TEXT_ALIGN_CENTER);
	}
	if (state->alarm_count > 0U) {
		lv_obj_scroll_to_y(area, focus * 62, LV_ANIM_OFF);
	}
}

void ui_pages_render_todo(lv_obj_t *screen, const ui_pages_state_t *state)
{
	if (state == NULL) {
		return;
	}

	app_todo_snapshot_t todo = { 0 };
	(void)sync_service_get_todo_snapshot(&todo);
	lv_obj_t *area = titled_scroll_body(screen, "TODO", ui_color_purple());
	uint8_t focus = state->todo_page_focus != NULL ? *state->todo_page_focus : 0;

	uint8_t shown = 0;
	for (uint8_t i = 0; i < todo.count && i < APP_TODO_MAX_ITEMS; i++) {
		const bool selected = shown == focus;
		lv_color_t bg = selected ? ui_color_white() : lv_color_hex(0x3f1f86);
		lv_color_t fg = selected ? ui_color_black() : ui_color_white();
		ui_box(area, 4, 4 + shown * 62, 228, 52, bg);
		char text[112];
		snprintf(text, sizeof(text), "%s %s", todo.items[i].done ? LV_SYMBOL_OK : LV_SYMBOL_MINUS,
			 todo.items[i].text[0] != '\0' ? todo.items[i].text : "(empty)");
		ui_label_wrap(area, text, UI_FONT_24, fg, 8, 9 + shown * 62, 220, 42, LV_TEXT_ALIGN_LEFT);
		shown++;
	}

	{
		const bool selected = shown == focus;
		lv_color_t bg = selected ? ui_color_white() : lv_color_hex(0x3f1f86);
		lv_color_t fg = selected ? ui_color_black() : ui_color_white();
		ui_box(area, 4, 4 + shown * 62, 228, 52, bg);
		ui_label(area, LV_SYMBOL_SETTINGS " SETTINGS", UI_FONT_24, fg, 8, 15 + shown * 62, 220, 30,
			 LV_TEXT_ALIGN_LEFT);
		shown++;
	}

	if (shown == 1U && ui_todo_item_count(&todo) == 0U && todo.sync_in_progress) {
		ui_label(area, "SYNCING", UI_FONT_40, ui_color_white(), 4, 70, 232, 50, LV_TEXT_ALIGN_CENTER);
	}
	if (focus >= shown) {
		focus = 0;
		if (state->todo_page_focus != NULL) {
			*state->todo_page_focus = focus;
		}
	}
	lv_obj_scroll_to_y(area, focus * 62, LV_ANIM_OFF);
}

void ui_pages_render_env(lv_obj_t *screen, const ui_pages_state_t *state)
{
	if (state == NULL) {
		return;
	}

	app_environment_snapshot_t env = { 0 };
	app_presence_status_t presence = { 0 };
	const bool ok = environment_service_get_snapshot(&env);
	const bool presence_ok = presence_service_get_status(&presence);
	char temp[16];
	char humi[16];
	char lux[16];
	char radar[16];
	snprintf(temp, sizeof(temp), ok && env.dht11_valid ? "%.0fC" : "--C", env.temperature_c);
	snprintf(humi, sizeof(humi), ok && env.dht11_valid ? "%.0f%%" : "--%%", env.humidity_percent);
	snprintf(lux, sizeof(lux), ok && env.bh1750_valid ? "%.0f" : "--", env.lux);
	snprintf(radar, sizeof(radar), presence_ok && presence.radar_healthy ? (presence.detected ? "PRES" : "NONE") :
									 (presence.detected ? "OUT" : "ERR"));

	const bool temp_alert = state->env_alert_on && ok && env.dht11_valid &&
				(env.temperature_c < (float)state->env_temp_low_c ||
				 env.temperature_c > (float)state->env_temp_high_c);
	const bool humi_alert = state->env_alert_on && ok && env.dht11_valid &&
				(env.humidity_percent < (float)state->env_humi_low_percent ||
				 env.humidity_percent > (float)state->env_humi_high_percent);
	const bool lux_alert = state->env_alert_on && ok && env.bh1750_valid &&
			       (env.lux < (float)state->env_lux_low || env.lux > (float)state->env_lux_high);

	ui_box(screen, 0, 0, UI_TILE_W, UI_TILE_H, temp_alert ? ui_color_red() : ui_color_green());
	ui_label(screen, "TEMP", UI_FONT_24, ui_color_white(), 2, 8, 116, 30, LV_TEXT_ALIGN_CENTER);
	ui_label(screen, temp, UI_FONT_40, ui_color_white(), 2, 58, 116, 50, LV_TEXT_ALIGN_CENTER);

	ui_box(screen, UI_TILE_W, 0, UI_TILE_W, UI_TILE_H, humi_alert ? ui_color_red() : ui_color_humi());
	ui_label(screen, "HUMI", UI_FONT_24, ui_color_white(), UI_TILE_W + 2, 8, 116, 30, LV_TEXT_ALIGN_CENTER);
	ui_label(screen, humi, UI_FONT_40, ui_color_white(), UI_TILE_W + 2, 58, 116, 50, LV_TEXT_ALIGN_CENTER);

	ui_box(screen, 0, UI_TILE_H, UI_TILE_W, UI_TILE_H, lux_alert ? ui_color_red() : ui_color_lux());
	ui_label(screen, "LUX", UI_FONT_24, ui_color_white(), 2, UI_TILE_H + 8, 116, 30, LV_TEXT_ALIGN_CENTER);
	ui_label(screen, lux, UI_FONT_40, ui_color_white(), 2, UI_TILE_H + 58, 116, 50, LV_TEXT_ALIGN_CENTER);

	ui_box(screen, UI_TILE_W, UI_TILE_H, UI_TILE_W, UI_TILE_H,
	       presence_ok && presence.radar_healthy ? ui_color_red() : ui_color_gray());
	ui_label(screen, "RADAR", UI_FONT_24, ui_color_white(), UI_TILE_W + 2, UI_TILE_H + 8, 116, 30,
		 LV_TEXT_ALIGN_CENTER);
	ui_label(screen, radar, UI_FONT_24, ui_color_white(), UI_TILE_W + 2, UI_TILE_H + 58, 116, 50,
		 LV_TEXT_ALIGN_CENTER);
}

void ui_pages_render_wifi(lv_obj_t *screen)
{
	app_net_status_t net = { 0 };
	app_todo_snapshot_t todo = { 0 };
	(void)net_service_get_status(&net);
	(void)sync_service_get_todo_snapshot(&todo);

	ui_box(screen, 0, 0, UI_TILE_W, UI_TILE_H, net.wifi_connected ? ui_color_teal() : ui_color_gray());
	ui_label(screen, "WIFI", UI_FONT_24, ui_color_white(), 2, 8, 116, 30, LV_TEXT_ALIGN_CENTER);
	ui_label(screen, net.wifi_connected ? "ON" : "OFF", UI_FONT_24, ui_color_white(), 2, 58, 116, 56,
		 LV_TEXT_ALIGN_CENTER);

	ui_box(screen, UI_TILE_W, 0, UI_TILE_W, UI_TILE_H, ui_color_ip());
	ui_label(screen, "IP", UI_FONT_24, ui_color_white(), UI_TILE_W + 2, 8, 116, 30, LV_TEXT_ALIGN_CENTER);
	char ip_text[24];
	if (net.ip_ready) {
		unsigned a = 0;
		unsigned b = 0;
		unsigned c = 0;
		unsigned d = 0;
		if (sscanf(net.ip_addr, "%u.%u.%u.%u", &a, &b, &c, &d) == 4) {
			snprintf(ip_text, sizeof(ip_text), "%u.%u\n%u.%u", a, b, c, d);
		} else {
			snprintf(ip_text, sizeof(ip_text), "%s", net.ip_addr);
		}
	} else {
		snprintf(ip_text, sizeof(ip_text), "--");
	}
	ui_label(screen, ip_text, UI_FONT_24, ui_color_white(), UI_TILE_W + 2, 52, 116, 62,
		 LV_TEXT_ALIGN_CENTER);

	ui_box(screen, 0, UI_TILE_H, UI_TILE_W, UI_TILE_H, net.time_synced ? ui_color_sync() : ui_color_orange());
	ui_label(screen, "TIME", UI_FONT_24, ui_color_white(), 2, UI_TILE_H + 8, 116, 30, LV_TEXT_ALIGN_CENTER);
	ui_label(screen, net.time_synced ? "SYNC" : "WAIT", UI_FONT_24, ui_color_white(), 2, UI_TILE_H + 58,
		 116, 56, LV_TEXT_ALIGN_CENTER);

	ui_box(screen, UI_TILE_W, UI_TILE_H, UI_TILE_W, UI_TILE_H, todo.sync_ok ? ui_color_todo_net() : ui_color_red());
	ui_label(screen, "TODO", UI_FONT_24, ui_color_white(), UI_TILE_W + 2, UI_TILE_H + 8, 116, 30,
		 LV_TEXT_ALIGN_CENTER);
	ui_label(screen, todo.sync_in_progress ? "..." : (todo.sync_ok ? "OK" : "ERR"), UI_FONT_24,
		 ui_color_white(), UI_TILE_W + 2, UI_TILE_H + 58, 116, 56, LV_TEXT_ALIGN_CENTER);
}

void ui_pages_render_low_clock(lv_obj_t *screen, const ui_pages_state_t *state)
{
	if (state == NULL) {
		return;
	}

	char time_text[16];
	char period_text[4];
	char date_text[16];
	ui_format_time(time_text, sizeof(time_text), state->low_use_24h, true);
	ui_format_time_period(period_text, sizeof(period_text), state->low_use_24h);
	ui_format_date(date_text, sizeof(date_text));
	ui_box(screen, 0, 0, UI_SCREEN_W, UI_CONTENT_H, ui_color_black());
	ui_label(screen, time_text, UI_FONT_40, ui_color_white(), state->low_use_24h ? 2 : 8, 66,
		 state->low_use_24h ? 236 : 190, 54, LV_TEXT_ALIGN_CENTER);
	if (!state->low_use_24h) {
		ui_label(screen, period_text, UI_FONT_20, ui_color_gray(), 192, 84, 40, 22, LV_TEXT_ALIGN_LEFT);
	}
	ui_label(screen, date_text, UI_FONT_24, ui_color_gray(), 2, 132, 236, 32, LV_TEXT_ALIGN_CENTER);
}
