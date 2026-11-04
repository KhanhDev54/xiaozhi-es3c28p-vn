#include "websocket_control_server.h"

#include <cJSON.h>
#include <esp_http_server.h>
#include <esp_log.h>
#include <unistd.h>
#include <wifi_station.h>

#include <cstring>
#include <string>

#include "mcp_server.h"
#include "tank_controller.h"

static const char* TAG = "WSControl";

extern const uint8_t tank_control_html_start[] asm("_binary_tank_control_html_start");
extern const uint8_t tank_control_html_end[] asm("_binary_tank_control_html_end");

WebSocketControlServer* WebSocketControlServer::instance_ = nullptr;

WebSocketControlServer::WebSocketControlServer() : server_handle_(nullptr) {
    instance_ = this;
}

WebSocketControlServer::~WebSocketControlServer() {
    Stop();
    instance_ = nullptr;
}

void WebSocketControlServer::SetNoKeepAlive(httpd_req_t* req) {
    httpd_resp_set_hdr(req, "Connection", "close");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
}

void WebSocketControlServer::OnSocketClose(httpd_handle_t /*hd*/, int sockfd) {
    if (sockfd >= 0) {
        close(sockfd);
    }
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

    const char* tool_name = "self.tank.action";
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
    SetNoKeepAlive(req);
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    const size_t len = tank_control_html_end - tank_control_html_start;
    return httpd_resp_send(req, reinterpret_cast<const char*>(tank_control_html_start), len);
}

esp_err_t WebSocketControlServer::status_handler(httpd_req_t* req) {
    SetNoKeepAlive(req);
    std::string ip = WifiStation::GetInstance().GetIpAddress();
    std::string json = "{\"ip\":\"" + ip + "\",\"sound\":" +
                       (TankSoundEnabled() ? "true" : "false") + "}";
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, json.c_str(), json.size());
}

esp_err_t WebSocketControlServer::action_handler(httpd_req_t* req) {
    SetNoKeepAlive(req);
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

bool WebSocketControlServer::Start(int port) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = port;
    /* Leave sockets for MQTT + music HTTP; do not hog the LWIP pool. */
    config.max_open_sockets = 4;
    config.max_uri_handlers = 8;
    config.lru_purge_enable = true;
    config.recv_wait_timeout = 3;
    config.send_wait_timeout = 3;
    config.stack_size = 6144;
    config.close_fn = OnSocketClose;
#if defined(CONFIG_LWIP_MAX_SOCKETS)
    /* ctrl_port must not collide with another httpd on the device */
    config.ctrl_port = 32769;
#endif

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

    if (httpd_start(&server_handle_, &config) == ESP_OK) {
        httpd_register_uri_handler(server_handle_, &index_uri);
        httpd_register_uri_handler(server_handle_, &index_html_uri);
        httpd_register_uri_handler(server_handle_, &status_uri);
        httpd_register_uri_handler(server_handle_, &action_uri);
        ESP_LOGI(TAG, "Tank HTTP control on port %d (no WebSocket)", port);
        return true;
    }

    ESP_LOGE(TAG, "Failed to start HTTP control server on port %d", port);
    return false;
}

void WebSocketControlServer::Stop() {
    if (server_handle_) {
        httpd_stop(server_handle_);
        server_handle_ = nullptr;
        ESP_LOGI(TAG, "HTTP control server stopped");
    }
}
