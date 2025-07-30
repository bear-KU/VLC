#include <WiFi.h>
#include "server_comm.h"
#include "vlc_receive.h"
#include "vlc_send.h"
#include "config.h"

// ============================================================
// Global Variables
// ============================================================

// Communication Parameters
int data_type;
int payload_size;
int signal_len;

// Role for VLC communication
char role[ROLE_SIZE];

// VLC data buffer
char vlc_data[HEX_DATA_BUFFER_SIZE + 1];

// System state
SystemState system_state = STATE_IDLE;

// WiFi client
WiFiClient client;

// ============================================================
// Function Prototypes
// ============================================================
inline void connect_to_WiFi();
inline void handle_communication(WiFiClient &client);


// ============================================================
// WiFi Connection
// ============================================================

// Connect to Wi-Fi network
inline void connect_to_WiFi() {
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Failed to connect to Wi-Fi");
    while (1); // Stay in a loop if WiFi connection fails
  }
  Serial.println("\r\nWi-Fi connected!");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
} // inline void connect_to_WiFi()

// ============================================================
// Main Communication Handler
// ============================================================

// Handle communication with the server
inline void handle_communication(WiFiClient &client) {
  // 再接続
  if (!client.connected()) {
      Serial.println("Disconnected from server. Attempting to reconnect...");
      connect_to_server(client);
      delay(5000);
      return;
  }

  // サーバからのデータの取得
  StaticJsonDocument<256> doc;
  if(handle_json(client, doc)) {
    dispatch_method(client, doc);
  }


  // method が receive または send なら 可視光通信を行う
  if(system_state == STATE_RECEIVE_RUNNING) {
    // receive の場合
    Serial.println("VLC receive running...");
    
    // 可視光通信の受信処理
    String receive_data = run_vlc_receive(payload_size);

    memset(vlc_data, 0, sizeof(vlc_data));
    strncpy(vlc_data, receive_data.c_str(), HEX_DATA_BUFFER_SIZE);

    system_state = STATE_TASK_COMPLETED;
    Serial.println("VLC receive completed.");
  } else if(system_state == STATE_SEND_RUNNING) {
    // send の場合
    Serial.println("VLC send running...");

    // 可視光通信の送信処理
    String send_data = run_vlc_send(data_type, payload_size, signal_len);

    memset(vlc_data, 0, sizeof(vlc_data));
    strncpy(vlc_data, send_data.c_str(), HEX_DATA_BUFFER_SIZE);
    
    system_state = STATE_TASK_COMPLETED;
    Serial.println("VLC send completed.");
  }

} // inline void handle_communication(WiFiClient &client)


// ============================================================
// Main Setup and Loop
// ============================================================

void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  delay(100);

  // Wait for serial connection
  while (!Serial) {
    ;
  }
  
  connect_to_WiFi();
  connect_to_server(client);
}

void loop() {
  handle_communication(client);

  if (!client.connected()) {
    Serial.println("Not connected to server. Closing connection.");
    while(1) ;
  }
}

