#ifndef WEBSOCKET_CONTROL_SERVER_H
#define WEBSOCKET_CONTROL_SERVER_H

#include <cJSON.h>
#include <esp_http_server.h>

/** HTTP-only tank web control (no long-lived WebSocket — avoids socket exhaustion). */
class WebSocketControlServer {
public:
    WebSocketControlServer();
    ~WebSocketControlServer();

    bool Start(int port = 8081);
    void Stop();

private:
    httpd_handle_t server_handle_;

    static esp_err_t index_handler(httpd_req_t* req);
    static esp_err_t status_handler(httpd_req_t* req);
    static esp_err_t action_handler(httpd_req_t* req);
    static void SetNoKeepAlive(httpd_req_t* req);
    static void DispatchJson(cJSON* root);
    static void OnSocketClose(httpd_handle_t hd, int sockfd);

    static WebSocketControlServer* instance_;
};

#endif  // WEBSOCKET_CONTROL_SERVER_H
