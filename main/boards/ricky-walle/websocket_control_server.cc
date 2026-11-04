#include "websocket_control_server.h"

#include <cJSON.h>
#include <esp_http_server.h>
#include <esp_log.h>
#include <sys/param.h>
#include <wifi_station.h>

#include <cstdlib>
#include <cstring>
#include <map>
#include <string>

#include "board.h"
#include "mcp_server.h"

static const char* TAG = "WSControl";

extern const uint8_t walle_control_html_start[] asm("_binary_walle_control_html_start");
extern const uint8_t walle_control_html_end[] asm("_binary_walle_control_html_end");

WebSocketControlServer* WebSocketControlServer::instance_ = nullptr;

WebSocketControlServer::WebSocketControlServer() : server_handle_(nullptr) {
    instance_ = this;
}

WebSocketControlServer::~WebSocketControlServer() {
    Stop();
    instance_ = nullptr;
}

void WebSocketControlServer::DispatchJson(cJSON* root) {
    if (root == nullptr) {
        return;
    }

    cJSON* jsonrpc = cJSON_GetObjectItem(root, "jsonrpc");
    if (cJSON_IsString(jsonrpc)) {
        McpServer::GetInstance().ParseMessage(root);
        return;
    }

    cJSON* type = cJSON_GetObjectItem(root, "type");
    if (type && cJSON_IsString(type) && strcmp(type->valuestring, "mcp") == 0) {
        cJSON* payload = cJSON_GetObjectItem(root, "payload");
        if (payload != nullptr) {
            McpServer::GetInstance().ParseMessage(payload);
        }
        return;
    }

    const char* tool_name = "self.walle.action";
    cJSON* tool = cJSON_GetObjectItem(root, "tool");
    if (cJSON_IsString(tool) && tool->valuestring[0] != '\0') {
        tool_name = tool->valuestring;
    }

    cJSON* msg = cJSON_CreateObject();
    cJSON_AddStringToObject(msg, "jsonrpc", "2.0");
    cJSON_AddNumberToObject(msg, "id", 1);
    cJSON_AddStringToObject(msg, "method", "tools/call");
    cJSON* params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "name", tool_name);
    cJSON* arguments = cJSON_Duplicate(root, 1);
    cJSON_DeleteItemFromObject(arguments, "tool");
    cJSON_AddItemToObject(params, "arguments", arguments);
    cJSON_AddItemToObject(msg, "params", params);
    McpServer::GetInstance().ParseMessage(msg);
    cJSON_Delete(msg);
}

esp_err_t WebSocketControlServer::index_handler(httpd_req_t* req) {
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    const size_t len = walle_control_html_end - walle_control_html_start;
    return httpd_resp_send(req, reinterpret_cast<const char*>(walle_control_html_start), len);
}

esp_err_t WebSocketControlServer::status_handler(httpd_req_t* req) {
    int level = 0;
    bool charging = false;
    bool discharging = false;
    Board::GetInstance().GetBatteryLevel(level, charging, discharging);
    std::string ip = WifiStation::GetInstance().GetIpAddress();

    std::string json = "{\"ip\":\"" + ip + "\",\"level\":" + std::to_string(level) +
                       ",\"charging\":" + (charging ? "true" : "false") + "}";
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    return httpd_resp_send(req, json.c_str(), json.size());
}

esp_err_t WebSocketControlServer::action_handler(httpd_req_t* req) {
    if (req->content_len <= 0 || req->content_len > 1024) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid length");
        return ESP_FAIL;
    }

    std::string body(req->content_len, '\0');
    int remaining = req->content_len;
    int offset = 0;
    while (remaining > 0) {
        int received = httpd_req_recv(req, body.data() + offset, remaining);
        if (received <= 0) {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "recv failed");
            return ESP_FAIL;
        }
        remaining -= received;
        offset += received;
    }

    cJSON* root = cJSON_Parse(body.c_str());
    if (root == nullptr) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid json");
        return ESP_FAIL;
    }
    DispatchJson(root);
    cJSON_Delete(root);

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}

esp_err_t WebSocketControlServer::ws_handler(httpd_req_t* req) {
    if (instance_ == nullptr) {
        return ESP_FAIL;
    }

    if (req->method == HTTP_GET) {
        ESP_LOGI(TAG, "Handshake done, the new connection was opened");
        instance_->AddClient(req);
        return ESP_OK;
    }

    httpd_ws_frame_t ws_pkt;
    uint8_t* buf = NULL;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;

    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "httpd_ws_recv_frame failed to get frame len with %d", ret);
        return ret;
    }

    if (ws_pkt.len) {
        buf = (uint8_t*)calloc(1, ws_pkt.len + 1);
        if (buf == NULL) {
            ESP_LOGE(TAG, "Failed to calloc memory for buf");
            return ESP_ERR_NO_MEM;
        }
        ws_pkt.payload = buf;
        ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "httpd_ws_recv_frame failed with %d", ret);
            free(buf);
            return ret;
        }
    }

    if (ws_pkt.type == HTTPD_WS_TYPE_CLOSE) {
        instance_->RemoveClient(req);
        free(buf);
        return ESP_OK;
    }

    if (ws_pkt.type == HTTPD_WS_TYPE_TEXT) {
        if (ws_pkt.len > 0 && buf != nullptr) {
            buf[ws_pkt.len] = '\0';
            instance_->HandleMessage(req, (const char*)buf, ws_pkt.len);
        }
    }

    free(buf);
    return ESP_OK;
}

bool WebSocketControlServer::Start(int port) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = port;
    config.max_open_sockets = 7;
    config.max_uri_handlers = 8;
    config.lru_purge_enable = true;
    config.stack_size = 8192;

    httpd_uri_t index_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = index_handler,
        .user_ctx = nullptr,
    };
    httpd_uri_t index_html_uri = {
        .uri = "/index.html",
        .method = HTTP_GET,
        .handler = index_handler,
        .user_ctx = nullptr,
    };
    httpd_uri_t status_uri = {
        .uri = "/api/status",
        .method = HTTP_GET,
        .handler = status_handler,
        .user_ctx = nullptr,
    };
    httpd_uri_t action_uri = {
        .uri = "/api/action",
        .method = HTTP_POST,
        .handler = action_handler,
        .user_ctx = nullptr,
    };
    httpd_uri_t ws_uri = {
        .uri = "/ws",
        .method = HTTP_GET,
        .handler = ws_handler,
        .user_ctx = nullptr,
        .is_websocket = true,
    };

    if (httpd_start(&server_handle_, &config) == ESP_OK) {
        httpd_register_uri_handler(server_handle_, &index_uri);
        httpd_register_uri_handler(server_handle_, &index_html_uri);
        httpd_register_uri_handler(server_handle_, &status_uri);
        httpd_register_uri_handler(server_handle_, &action_uri);
        httpd_register_uri_handler(server_handle_, &ws_uri);
        ESP_LOGI(TAG, "Control web UI started on port %d", port);
        return true;
    }

    ESP_LOGE(TAG, "Failed to start WebSocket server");
    return false;
}

void WebSocketControlServer::Stop() {
    if (server_handle_) {
        httpd_stop(server_handle_);
        server_handle_ = nullptr;
        clients_.clear();
        ESP_LOGI(TAG, "WebSocket server stopped");
    }
}

void WebSocketControlServer::HandleMessage(httpd_req_t* req, const char* data, size_t len) {
    (void)req;
    if (data == nullptr || len == 0) {
        return;
    }
    if (len > 4096) {
        ESP_LOGE(TAG, "Message too long: %zu bytes", len);
        return;
    }

    char* temp_buf = (char*)malloc(len + 1);
    if (temp_buf == nullptr) {
        return;
    }
    memcpy(temp_buf, data, len);
    temp_buf[len] = '\0';

    cJSON* root = cJSON_Parse(temp_buf);
    free(temp_buf);
    if (root == nullptr) {
        ESP_LOGE(TAG, "Failed to parse JSON");
        return;
    }
    DispatchJson(root);
    cJSON_Delete(root);
}

void WebSocketControlServer::AddClient(httpd_req_t* req) {
    int sock_fd = httpd_req_to_sockfd(req);
    if (clients_.find(sock_fd) == clients_.end()) {
        clients_[sock_fd] = req;
        ESP_LOGI(TAG, "Client connected: %d (total: %zu)", sock_fd, clients_.size());
    }
}

void WebSocketControlServer::RemoveClient(httpd_req_t* req) {
    int sock_fd = httpd_req_to_sockfd(req);
    clients_.erase(sock_fd);
    ESP_LOGI(TAG, "Client disconnected: %d (total: %zu)", sock_fd, clients_.size());
}

size_t WebSocketControlServer::GetClientCount() const {
    return clients_.size();
}
