#pragma once

#ifndef SERVER_COMM_H
#define SERVER_COMM_H

#include <WiFi.h>
#include <ArduinoJson.h>
#include <string.h>
#include "config.h"

enum SystemState {
  STATE_IDLE,             // 待機中
  STATE_SEND_RUNNING,     // 可視光通信 送信中
  STATE_RECEIVE_RUNNING,  // 可視光通信 受信中
  STATE_TASK_COMPLETED,   // 可視光通信 タスク完了
};

// Parameters
extern int data_type;
extern int payload_size;
extern int signal_len;
// Role for VLC communication
extern char role[ROLE_SIZE];
// VLC data
extern char vlc_data[HEX_DATA_BUFFER_SIZE + 1];
// System state
extern SystemState system_state;

// Function Prototypes
inline void connect_to_server(WiFiClient &client);
inline void disconnect_from_server(WiFiClient &client);
inline bool handle_json(WiFiClient &client, JsonDocument &doc);
inline void send_response(WiFiClient &client, JsonDocument &response_doc);
inline void dispatch_method(WiFiClient &client, JsonDocument &doc);
inline void set_condition(WiFiClient &client, JsonDocument &request_doc);
inline void receive(WiFiClient &client, JsonDocument &request_doc);
inline void send(WiFiClient &client, JsonDocument &request_doc);
inline void report(WiFiClient &client, JsonDocument &request_doc);

#endif // SERVER_COMM_H

// ============================================================
// Server Communication Functions
// ============================================================

// サーバとの接続を確立
inline void connect_to_server(WiFiClient &client) {
  // 接続
  if (!client.connect(serverIP, serverPort)) {
    Serial.println("\nFailed to connect to server.");
    while (1); // Stay in a loop if connection fails
  }
  Serial.println("Connected to server.");
} // inline void connect_to_server(WiFiClient &client)

// サーバとの接続を切断
inline void disconnect_from_server(WiFiClient &client) {
  client.stop();
  Serial.println("Connection closed.");
} // inline void disconnect_from_server(WiFiClient &client)


// ============================================================
// Helper functions
// ============================================================

// サーバからの JSON を処理
inline bool handle_json(WiFiClient &client, JsonDocument &doc) {
  unsigned long timeout = millis();
  while (!client.available()) {
    if (millis() - timeout > 5000) {
      Serial.println("No response from server.");
      disconnect_from_server(client);
      return false;
    }
    delay(10);
  }

  // JSONを受信
  String json_string = client.readStringUntil('\n');
  json_string.trim();
  Serial.print("Received JSON from server: ");
  Serial.println(json_string);

  // JSONをパース
  DeserializationError error = deserializeJson(doc, json_string);
  if (error) {
    Serial.print("Failed to parse JSON: ");
    Serial.println(error.c_str());
    disconnect_from_server(client);
    return false;
  } 
  return true;
} // inline bool handle_json(WiFiClient &client, JsonDocument &doc)

// JSON-RPC 形式のレスポンスをサーバに送信
inline void send_response(WiFiClient &client, JsonDocument &response_doc) {
  String response;
  serializeJson(response_doc, response);
  client.println(response);
  Serial.print("Sent response to server: ");
  Serial.println(response);
} // inline void send_response(WiFiClient &client, JsonDocument &response_doc)


// ============================================================
// Method Dispatching
// ============================================================

// method に応じた処理を実行
inline void dispatch_method(WiFiClient &client, JsonDocument &doc) {
  const char* method = doc["method"];

  if(strcmp(method, "set_condition") == 0) {
    set_condition(client, doc);
  } else if(strcmp(method, "receive") == 0) {
    receive(client, doc);
  } else if(strcmp(method, "send") == 0) {
    send(client, doc);
  } else if(strcmp(method, "report") == 0) {
    report(client, doc);
  } else {
    // TODO: handle unknown method
    Serial.println("Unknown method received from server.");
    return;
  }
} // inline void dispatch_method(WiFiClient &client, JsonDocument &doc)

// ============================================================
// Method Implementations
// ============================================================

// set_condition
inline void set_condition(WiFiClient &client, JsonDocument &request_doc) {
  // "params" から実験条件を取得
  JsonObject params = request_doc["params"];
  if (params.isNull()) {
    Serial.println("No params found in JSON.");
    return;
  }

  data_type = params["data_type"];
  payload_size = params["payload_size"];
  signal_len = params["signal_len"];

  Serial.print("data_type: ");
  Serial.println(data_type);
  Serial.print("payload_size: ");
  Serial.println(payload_size);
  Serial.print("signal_len: ");
  Serial.println(signal_len);

  // レスポンスをサーバに送信
  StaticJsonDocument<256> response_doc;
  response_doc["jsonrpc"] = "2.0";
  response_doc["result"] = "success";
  response_doc["id"] = request_doc["id"];
  send_response(client, response_doc);
} // inline void set_condition(WiFiClient &client, JsonDocument &request_doc)

// receive
inline void receive(WiFiClient &client, JsonDocument &request_doc) {
  // State を更新
  system_state = STATE_RECEIVE_RUNNING;

  // receive を一時的に覚えておく
  const char* method = request_doc["method"];
  memset(role, 0, strlen(role));
  strcpy(role, method);

  // レスポンスをサーバに送信
  StaticJsonDocument<256> response_doc;
  response_doc["jsonrpc"] = "2.0";
  response_doc["result"] = "success";
  response_doc["id"] = request_doc["id"];
  send_response(client, response_doc);
} // inline void receive(WiFiClient &client, JsonDocument &request_doc)

// send
inline void send(WiFiClient &client, JsonDocument &request_doc) {
  // State を更新
  system_state = STATE_SEND_RUNNING;

  // send を一時的に覚えておく
  const char* method = request_doc["method"];
  memset(role, 0, strlen(role));
  strcpy(role, method);

  // レスポンスをサーバに送信
  StaticJsonDocument<256> response_doc;
  response_doc["jsonrpc"] = "2.0";
  response_doc["result"] = "success";
  response_doc["id"] = request_doc["id"];
  send_response(client, response_doc);
} // inline void send(WiFiClient &client, JsonDocument &request_doc)

// report
inline void report(WiFiClient &client, JsonDocument &request_doc) {
  if(system_state != STATE_TASK_COMPLETED) {
    Serial.println("System is not in a state to report.");

    // TODO: error を返すべき
    StaticJsonDocument<256> error_doc;
    error_doc["jsonrpc"] = "2.0";
    error_doc["error"]["code"] = -32603; // Internal error
    error_doc["error"]["message"] = "System is not in a state to report.";
    error_doc["id"] = request_doc["id"];
    send_response(client, error_doc);

    return;
  }

  // レスポンスをサーバに送信
  StaticJsonDocument<512> response_doc;
  response_doc["jsonrpc"] = "2.0";
  response_doc["result"]["role"] = role;
  response_doc["result"]["data"] = vlc_data;
  response_doc["id"] = request_doc["id"];
  send_response(client, response_doc);

  // State を更新
  system_state = STATE_IDLE;
}
