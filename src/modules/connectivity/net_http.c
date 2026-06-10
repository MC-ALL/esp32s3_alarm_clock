#include <connectivity/net_http.h>

#include <esp_http_client.h>
#include <string.h>

static esp_err_t net_http_event_handler(esp_http_client_event_t *evt)
{
	app_net_http_response_t *response = (app_net_http_response_t *)evt->user_data;
	if (response == NULL || response->body == NULL || response->body_cap == 0U) {
		return ESP_OK;
	}
	if (evt->event_id == HTTP_EVENT_ON_DATA && evt->data != NULL && evt->data_len > 0) {
		if ((response->body_len + (size_t)evt->data_len + 1U) > response->body_cap) {
			return ESP_FAIL;
		}
		memcpy(response->body + response->body_len, evt->data, (size_t)evt->data_len);
		response->body_len += (size_t)evt->data_len;
		response->body[response->body_len] = '\0';
	}
	return ESP_OK;
}

int net_http_request(const char *method, const char *url, const char *body, app_net_http_response_t *response)
{
	if (method == NULL || url == NULL || response == NULL) {
		return -1;
	}
	response->body_len = 0;
	response->status_code = 0;
	if (response->body != NULL && response->body_cap > 0U) {
		response->body[0] = '\0';
	}

	esp_http_client_config_t cfg = {
		.url = url,
		.timeout_ms = 5000,
		.event_handler = net_http_event_handler,
		.user_data = response,
	};
	esp_http_client_handle_t client = esp_http_client_init(&cfg);
	if (client == NULL) {
		return -1;
	}

	if (strcmp(method, "GET") == 0) {
		esp_http_client_set_method(client, HTTP_METHOD_GET);
	} else if (strcmp(method, "POST") == 0) {
		esp_http_client_set_method(client, HTTP_METHOD_POST);
	} else if (strcmp(method, "PUT") == 0) {
		esp_http_client_set_method(client, HTTP_METHOD_PUT);
	} else if (strcmp(method, "DELETE") == 0) {
		esp_http_client_set_method(client, HTTP_METHOD_DELETE);
	} else {
		esp_http_client_cleanup(client);
		return -1;
	}

	if (body != NULL) {
		esp_http_client_set_header(client, "Content-Type", "application/json");
		esp_http_client_set_post_field(client, body, (int)strlen(body));
	}

	esp_err_t err = esp_http_client_perform(client);
	response->status_code = esp_http_client_get_status_code(client);
	esp_http_client_cleanup(client);
	return err == ESP_OK ? 0 : (int)err;
}
