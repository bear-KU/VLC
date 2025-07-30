#pragma once
#include <Arduino.h>
#include "server_comm.h"
#include "config.h"
#include "data_handling.h"

#define LED_PIN 4
#define REST 1

// Function Prototypes
inline void send_leader();
inline void send_data0();
inline void send_data1();
inline void send_trailer();
inline void LED_send(char* data);
inline String run_vlc_send(int data_type, int payload_size, int signal_len);


// ============================================================
// VLC Communication Functions
// ============================================================
inline void send_leader() {
  digitalWrite(LED_PIN, 1);
  delay(REST);
}

inline void send_data0() {
  digitalWrite(LED_PIN, 0);
  delay(REST);
  digitalWrite(LED_PIN, 1);
  delay(REST);
}

inline void send_data1() {
  digitalWrite(LED_PIN, 0);
  delay(REST);
  digitalWrite(LED_PIN, 1);
  delay(REST*2);
}

inline void send_trailer() {
  digitalWrite(LED_PIN, 0);
  delay(REST);
  digitalWrite(LED_PIN, 1);
  delay(REST*5);
}

void LED_send(char* data) {
  int i;
  int data_len = strlen(data);

  send_leader();
  for(i=0; i<data_len; i++) {
    if(data[i] == '1') {
      send_data1();
    }
    else {
      send_data0();
    }
  }
  send_trailer();
  digitalWrite(LED_PIN, 0);
}


inline String run_vlc_send(int data_type, int payload_size, int signal_len) {
    String data = "";
    int i;

    data.reserve(BINARY_BUFFER_SIZE + 1);
    for (i = 0; i < BINARY_BUFFER_SIZE; i++) {
        int bit = random(0, 2);
        data += dtoc(bit);
    }

    // Serial.printf("Generated Data: %s\r\n", data.c_str());

    int count = BINARY_BUFFER_SIZE / payload_size;
    char payload[payload_size + 1];

    // 少し待つ
    delay(3000);

    for (i = 0; i < count; i++) {
      int start_index = i * payload_size;
      memcpy(payload, data.c_str() + start_index, payload_size);
      payload[payload_size] = '\0';
      LED_send(payload);
      delay(100);
    }

    String hex_data;
    hex_data.reserve(HEX_DATA_BUFFER_SIZE + 1);
    hex_data = binary_to_hex_string(data);
    

    return hex_data;
}

