#include "mqtt.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "custom.h"

static const char *TAG = "MQTT";

esp_mqtt_client_handle_t mqtt_client = NULL;
static bool s_connected = false;
static int s_req_id = 0;

/* 请求-响应映射（支持多个并发请求） */
#define MAX_PENDING 8

typedef struct {
    int req_id;
    mqtt_resp_cb_t cb;
} pending_req_t;

static pending_req_t s_pending[MAX_PENDING];
static SemaphoreHandle_t s_req_mutex = NULL;

/* 消息重组缓冲区（MQTT 大消息会分多片到达） */
static char *s_resp_buf = NULL;
static int  s_resp_buf_len = 0;
static int  s_resp_total_len = 0;

/* 前向声明 */
static void mqtt_process_response(const char *data, int len);

void mqtt_event_callback(void *event_handler_arg, esp_event_base_t event_base,
													int32_t event_id, void *event_data)
{
	esp_mqtt_event_handle_t data = (esp_mqtt_event_handle_t)event_data;

	switch (event_id) {
		case MQTT_EVENT_CONNECTED:
			ESP_LOGI(TAG, "Connected to broker");
			s_connected = true;
			custom_set_mqtt_status(true);
			esp_mqtt_client_subscribe(mqtt_client, MQTT_TOPIC_RESP, 0);
			break;
		case MQTT_EVENT_DISCONNECTED:
			ESP_LOGI(TAG, "Disconnected from broker");
			s_connected = false;
			custom_set_mqtt_status(false);
			break;
		case MQTT_EVENT_SUBSCRIBED:
			ESP_LOGI(TAG, "Subscribed to topic");
			break;
		case MQTT_EVENT_PUBLISHED:
			break;
		case MQTT_EVENT_DATA: {
			int offset = data->current_data_offset;
			int total  = data->total_data_len;
			int chunk  = data->data_len;

			/* 第一片：检查主题并分配缓冲区 */
			if (offset == 0) {
				/* 只处理响应主题 */
				bool is_resp = (data->topic_len == strlen(MQTT_TOPIC_RESP) &&
								strncmp(data->topic, MQTT_TOPIC_RESP, data->topic_len) == 0);
				if (!is_resp) break;

				if (s_resp_buf) { free(s_resp_buf); s_resp_buf = NULL; }
				s_resp_buf = malloc(total + 1);
				if (!s_resp_buf) { s_resp_buf_len = 0; s_resp_total_len = 0; break; }
				s_resp_buf_len = 0;
				s_resp_total_len = total;
				ESP_LOGI(TAG, "RESP start: total=%d, chunk=%d", total, chunk);
			}

			/* 追加当前分片（后续分片可能没有 topic，但仍在重组中） */
			if (s_resp_buf && offset + chunk <= s_resp_total_len) {
				memcpy(s_resp_buf + offset, data->data, chunk);
				s_resp_buf_len = offset + chunk;
				ESP_LOGD(TAG, "RESP frag: offset=%d, chunk=%d, accumulated=%d/%d", offset, chunk, s_resp_buf_len, s_resp_total_len);
			}

			/* 所有分片收齐后处理 */
			if (s_resp_buf && s_resp_buf_len >= s_resp_total_len) {
				s_resp_buf[s_resp_buf_len] = '\0';
				ESP_LOGI(TAG, "RESP complete: %d bytes", s_resp_buf_len);
				mqtt_process_response(s_resp_buf, s_resp_buf_len);
				free(s_resp_buf);
				s_resp_buf = NULL;
				s_resp_buf_len = 0;
				s_resp_total_len = 0;
			}
			break;
		}
		default:
			ESP_LOGD(TAG, "Event id: %d", event_id);
	}
}

static void mqtt_process_response(const char *data, int len)
{
	/* 创建以 null 结尾的字符串 */
	char *json_str = malloc(len + 1);
	if (!json_str) return;
	memcpy(json_str, data, len);
	json_str[len] = '\0';

	ESP_LOGI(TAG, "RESP: %s", json_str);

	cJSON *root = cJSON_Parse(json_str);
	free(json_str);

	if (!root) {
		ESP_LOGE(TAG, "Failed to parse JSON response");
		return;
	}

	cJSON *req_id_item = cJSON_GetObjectItem(root, "req_id");
	int resp_req_id = req_id_item ? req_id_item->valueint : -1;

	/* 检查是否匹配待处理请求 */
	mqtt_resp_cb_t cb = NULL;
	if (xSemaphoreTake(s_req_mutex, pdMS_TO_TICKS(100))) {
		for (int i = 0; i < MAX_PENDING; i++) {
			if (s_pending[i].req_id == resp_req_id && s_pending[i].cb) {
				cb = s_pending[i].cb;
				s_pending[i].req_id = -1;
				s_pending[i].cb = NULL;
				break;
			}
		}
		xSemaphoreGive(s_req_mutex);
	}

	if (cb) {
		cb(root);
	} else {
		ESP_LOGW(TAG, "No callback for req_id=%d", resp_req_id);
		cJSON_Delete(root);
	}
}

void mqtt_start(void)
{
	ESP_LOGI(TAG, "Starting MQTT client...");

	s_req_mutex = xSemaphoreCreateMutex();

	/* 初始化 pending 槽位 */
	for (int i = 0; i < MAX_PENDING; i++) {
		s_pending[i].req_id = -1;
		s_pending[i].cb = NULL;
	}

	esp_mqtt_client_config_t mqtt_cfg = {0};
	mqtt_cfg.broker.address.uri = MQTT_BROKER;
	mqtt_cfg.broker.address.port = MQTT_PORT;
	mqtt_cfg.credentials.client_id = MQTT_CLIENT_ID;
	mqtt_cfg.credentials.username = MQTT_USER;
	mqtt_cfg.credentials.authentication.password = MQTT_PASS;
	mqtt_cfg.buffer.size = 4096;  /* 增大接收缓冲区，避免大 JSON 响应被截断 */

	mqtt_client = esp_mqtt_client_init(&mqtt_cfg);

	esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_callback, NULL);

	esp_mqtt_client_start(mqtt_client);
}

bool mqtt_is_connected(void)
{
	return s_connected;
}

int mqtt_send_request(const char *action, cJSON *params, mqtt_resp_cb_t callback)
{
	if (!mqtt_client || !s_connected) {
		ESP_LOGW(TAG, "MQTT not connected, cannot send");
		return -1;
	}

	int req_id = ++s_req_id;

	/* 构建请求 JSON */
	cJSON *root = cJSON_CreateObject();
	cJSON_AddStringToObject(root, "action", action);
	cJSON_AddNumberToObject(root, "req_id", req_id);

	if (params) {
		cJSON_AddItemToObject(root, "params", cJSON_Duplicate(params, 1));
	}

	char *json_str = cJSON_PrintUnformatted(root);
	cJSON_Delete(root);

	if (!json_str) return -1;

	/* 注册回调 */
	if (callback) {
		if (xSemaphoreTake(s_req_mutex, pdMS_TO_TICKS(100))) {
			/* 找空槽位 */
			int slot = -1;
			for (int i = 0; i < MAX_PENDING; i++) {
				if (s_pending[i].req_id == -1) {
					slot = i;
					break;
				}
			}
			if (slot >= 0) {
				s_pending[slot].req_id = req_id;
				s_pending[slot].cb = callback;
			} else {
				/* 没有空槽位，覆盖最旧的 */
				ESP_LOGW(TAG, "No free slot, overwriting oldest (req_id=%d)", s_pending[0].req_id);
				s_pending[0].req_id = req_id;
				s_pending[0].cb = callback;
			}
			xSemaphoreGive(s_req_mutex);
		} else {
			ESP_LOGE(TAG, "Failed to acquire mutex for callback registration");
		}
	}

	/* 发布请求 */
	int msg_id = esp_mqtt_client_publish(mqtt_client, MQTT_TOPIC_REQ, json_str, 0, 0, 0);
	ESP_LOGI(TAG, "REQ: %s (msg_id=%d, req_id=%d)", json_str, msg_id, req_id);

	cJSON_free(json_str);

	return req_id;
}

void mqtt_cancel_callback(int req_id)
{
	if (xSemaphoreTake(s_req_mutex, pdMS_TO_TICKS(100))) {
		for (int i = 0; i < MAX_PENDING; i++) {
			if (s_pending[i].req_id == req_id) {
				s_pending[i].cb = NULL;
				s_pending[i].req_id = -1;
				break;
			}
		}
		xSemaphoreGive(s_req_mutex);
	}
}
