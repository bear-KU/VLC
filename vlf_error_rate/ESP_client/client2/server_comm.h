#pragma once
#include <WiFi.h>

#include "server_comm.h"
#include "config.h"
#include <string.h>

inline void connect_to_server(WiFiClient &client);
inline void disconnect_from_server(WiFiClient &client);
inline void handle_communication(WiFiClient &client);
inline void comm_as_sender(WiFiClient &client, String &role);
inline void comm_as_receiver(WiFiClient &client, String &role);

// サーバとの接続を確立
inline void connect_to_server(WiFiClient &client) {
  // 接続
  if (!client.connect(serverIP, serverPort)) {
    Serial.println("\nFailed to connect to server.");
    while(1); // Stay in a loop if connection fails
  }
  Serial.println("Connected to server.");
}

// サーバとの接続を切断
inline void disconnect_from_server(WiFiClient &client) {
  client.stop();
  Serial.println("Connection closed.");
}

// サーバとの処理
inline void handle_communication(WiFiClient &client) {
  // 通信開始
  connect_to_server(client);

  // ROLE の取得
  String role;
  unsigned long timeout = millis();
  while (!client.available()) {
    if (millis() - timeout > 5000) {
      Serial.println("No response from server.");
      disconnect_from_server(client);
      return;
    }
    delay(10);
  }
  role = client.readStringUntil('\n');
  role.trim();
  
  // 役割に応じた処理
  if(role.equals("SENDER")) {
    Serial.println("Running as SENDER");
    comm_as_sender(client, role);
  } else if(role.equals("RECEIVER")) {
    Serial.println("Running as RECEIVER");
    comm_as_receiver(client, role);
  } else {
    Serial.println("Unknown role received from server.");
    disconnect_from_server(client);
    return;
  }

  // 通信終了
  disconnect_from_server(client);
}


// SENDER としての通信処理
inline void comm_as_sender(WiFiClient &client, String &role) {
  // SENDER としての役割をサーバに送信
  client.print(role);
  delay(1000);

  // サーバからデータを受信
  unsigned long timeout = millis();
  while (!client.available()) {
    if (millis() - timeout > 5000) {
      Serial.println("No response from server.");
      disconnect_from_server(client);
      return;
    }
    delay(10);
  }

  char sender_buf[SENDER_BUFFER_SIZE];
  strncpy(sender_buf, client.readStringUntil('\n').c_str(), sizeof(sender_buf) - 1);
  sender_buf[sizeof(sender_buf) - 1] = '\0';
  Serial.print("Received from server: ");
  Serial.println(sender_buf);  


}

// RECEIVER としての通信処理
inline void comm_as_receiver(WiFiClient &client, String &role) {
  // RECEIVER としての役割をサーバに送信
  client.print(role);
  delay(1000);

  // RECEIVER の場合，サーバにデータを送信
  char receiver_buf[RECEIVER_BUFFER_SIZE];
  strncpy(receiver_buf, "Hello from ESP32 Receiver!\n", sizeof(receiver_buf) - 1);
  receiver_buf[sizeof(receiver_buf) - 1] = '\0';
  client.print(receiver_buf);
  Serial.print("Sent to server: ");
  Serial.println(receiver_buf);
}


