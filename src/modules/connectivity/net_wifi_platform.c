#include <connectivity/net_wifi_platform.h>

#include <esp_err.h>
#include <esp_netif.h>
#include <esp_wifi.h>
#include <string.h>

static bool s_event_loop_owned;
static bool s_wifi_initialized;
static esp_netif_t *s_wifi_sta_netif;
static esp_event_handler_instance_t s_wifi_event_instance;
static esp_event_handler_instance_t s_ip_event_instance;
static bool s_wifi_event_registered;
static bool s_ip_event_registered;

int net_wifi_platform_init(esp_event_handler_t event_handler, void *handler_arg)
{
	esp_err_t err = esp_netif_init();
	if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
		return (int)err;
	}

	err = esp_event_loop_create_default();
	if (err == ESP_OK) {
		s_event_loop_owned = true;
	} else if (err != ESP_ERR_INVALID_STATE) {
		return (int)err;
	}

	s_wifi_sta_netif = esp_netif_create_default_wifi_sta();
	if (s_wifi_sta_netif == NULL) {
		return (int)ESP_FAIL;
	}

	wifi_init_config_t wifi_init_cfg = WIFI_INIT_CONFIG_DEFAULT();
	wifi_init_cfg.nvs_enable = false;
	err = esp_wifi_init(&wifi_init_cfg);
	if (err != ESP_OK) {
		return (int)err;
	}
	s_wifi_initialized = true;

	err = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, event_handler, handler_arg,
						  &s_wifi_event_instance);
	if (err != ESP_OK) {
		return (int)err;
	}
	s_wifi_event_registered = true;

	err = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, event_handler, handler_arg,
						  &s_ip_event_instance);
	if (err != ESP_OK) {
		return (int)err;
	}
	s_ip_event_registered = true;

	err = esp_wifi_set_mode(WIFI_MODE_STA);
	if (err != ESP_OK) {
		return (int)err;
	}
	err = esp_wifi_set_ps(WIFI_PS_NONE);
	if (err != ESP_OK) {
		return (int)err;
	}

	return 0;
}

int net_wifi_platform_configure_sta(const char *ssid, const char *password)
{
	if (ssid == NULL || ssid[0] == '\0') {
		return 0;
	}

	wifi_config_t wifi_config = { 0 };
	strlcpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid));
	strlcpy((char *)wifi_config.sta.password, password != NULL ? password : "",
		sizeof(wifi_config.sta.password));
	wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
	wifi_config.sta.pmf_cfg.capable = false;
	wifi_config.sta.pmf_cfg.required = false;
	return (int)esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
}

void net_wifi_platform_deinit(void)
{
	if (s_ip_event_registered) {
		(void)esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, s_ip_event_instance);
		s_ip_event_registered = false;
	}
	if (s_wifi_event_registered) {
		(void)esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, s_wifi_event_instance);
		s_wifi_event_registered = false;
	}
	if (s_wifi_initialized) {
		(void)esp_wifi_stop();
		(void)esp_wifi_deinit();
		s_wifi_initialized = false;
	}
	if (s_wifi_sta_netif != NULL) {
		esp_netif_destroy_default_wifi(s_wifi_sta_netif);
		s_wifi_sta_netif = NULL;
	}
	if (s_event_loop_owned) {
		(void)esp_event_loop_delete_default();
		s_event_loop_owned = false;
	}
}

bool net_wifi_platform_is_initialized(void)
{
	return s_wifi_initialized;
}
