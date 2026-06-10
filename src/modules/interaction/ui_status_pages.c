#include <ui_status_pages.h>

#include <environment_service.h>
#include <net_service.h>
#include <presence_service.h>
#include <sync_service.h>
#include <ui_layout.h>
#include <ui_style.h>
#include <ui_todo_view.h>

#include <stdio.h>

void ui_status_pages_render_env(lv_obj_t *screen, const ui_pages_state_t *state)
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

void ui_status_pages_render_wifi(lv_obj_t *screen)
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
