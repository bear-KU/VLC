#pragma once
#include <Arduino.h>
#include "server_comm.h"
#include "config.h"
#include "esp_timer.h"
#include "data_handling.h"

// Function Prototypes
inline String run_vlc_receive(int payload_size);
inline int get_threshold();
inline String LED_read_binary(int payload_size, int threshold);
inline int get_light_intensity();

// ============================================================
// VLC Communication Functions
// ============================================================

inline String run_vlc_receive(int payload_size) {
  String data = "";
  int i, j;

  data.reserve(BINARY_BUFFER_SIZE + 1);

  // 閾値の設定
  // int threshold = get_threshold();
  int threshold = 150;


  int count = BINARY_BUFFER_SIZE / payload_size;
  for (i = 0; i < count; i++) {
    String received = LED_read_binary(payload_size, threshold);

    if(received == "ERROR") {
      return "ERROR"; // タイムアウトした場合は"ERROR"を返す
    }

    // if(received.length() != payload_size) {
    //   // x埋めに時間がかかるため，以降が上手く行かない
    //   // received = "x";
    // }

    data += received;
    // Serial.printf("Iteration %d: %s\r\n", i+1, received.c_str());
    delay(90); // 前回のトレイラの残りを読み込まないようにするため待つ
  }

  // Serial.printf("All received data: %s\r\n", data.c_str());
  String hex_data;
  hex_data.reserve(HEX_DATA_BUFFER_SIZE + 1);
  hex_data = binary_to_hex_string(data);
  return hex_data;
}

inline int get_threshold() {
  int threshold = 150;
  int i = 0;
  int value = 0;

  while(i < 1000) {
    value = get_light_intensity();
    delay(1);
    if (threshold < value) {
      threshold = value;
    }
    i++;
  }
  Serial.printf("Threshold: %d\r\n", threshold);
  delay(10);

  return threshold;
}

inline String LED_read_binary(int payload_size, int threshold) {
  int value = 0, prev_v = 0;
  int64_t signal_unit = 0, signal_len = 0;
  int64_t time, prev_time, start_time;
  int64_t edge_down_time = 0;
  int64_t edge_up_time = 0;
  int data_len = 0;
  int i = 0;

  int value_bright = 1, value_dark = 1;
  int value_bright_num = 1, value_dark_num = 1;
  int64_t sig_reader_bright = 1, sig_reader_dark = 0;

  String result = "";
  result.reserve(payload_size + 1);

  // 信号の開始を判定
  start_time = esp_timer_get_time();

  while (value <= ((threshold + 1) * 3))
  {
    time = esp_timer_get_time();
    if(time - start_time > COMMUNICATION_TIMEOUT) {
      Serial.println("Communication timeout.");
      return "ERROR"; // タイムアウトした場合は空文字を返す
    }

    prev_time = time;
    value = get_light_intensity();
  }
  
  // リーダの取得
  while(1) {
    value = get_light_intensity();
    time = esp_timer_get_time();
    prev_time = time;

    if(time - start_time > COMMUNICATION_TIMEOUT) {
      Serial.println("Communication timeout.");
      return "ERROR"; // タイムアウトした場合は空文字を返す
    }

    // 明るい状態の取得
    while((threshold * 3) < value) {
      value_bright_num++;
      value_bright += value;
      time = esp_timer_get_time();
      sig_reader_bright += (time - prev_time);
      prev_time = time;
      value = get_light_intensity();
    }
    time = esp_timer_get_time();
    sig_reader_bright += (time - prev_time);

    // 暗い状態の取得
    prev_time = time;
    while(value <= (threshold * 3)) {
      value_dark_num++;
      value_dark += value;
      time = esp_timer_get_time();
      sig_reader_dark += (time - prev_time);
      prev_time = time;
      value = get_light_intensity();
    }
    time = esp_timer_get_time();
    sig_reader_dark += (time - prev_time);

    if (sig_reader_bright / 2 <= sig_reader_dark)
    {
      threshold = ((value_bright / value_bright_num) + (value_dark / value_dark_num)) / 3;
      break;
    }
  }

  // 基準時間の表示
  signal_unit = sig_reader_bright / 8;
  // Serial.printf("sig_reader_bright: %lld\r\n", sig_reader_bright);

  value = 0;
  prev_v = 0;


  start_time = esp_timer_get_time();
  // データの取得
  while(i < payload_size) {
    time = esp_timer_get_time();

    // 通信時間が長すぎる場合は終了
    if(time - start_time > COMMUNICATION_TIMEOUT) {
      Serial.println("Communication timeout.");
      return "ERROR"; // タイムアウトした場合は空文字を返す
    }

    // 現在の光の強度を取得
    value = get_light_intensity() - threshold;
    if (value < 0) {
      value = 0;
    }


    // // 終了判定
    // if( (signal_unit * 5 < (time - edge_up_time)) &&
    //  	  (edge_down_time < edge_up_time))
    // {
    //   // Serial.println(result);
    //   return result; // ここで結果を返して終了
    // }

    // 1. 直前が Low，現在が High の場合(立ち上がり)
    if(prev_v == 0 && value != 0) {
      if((signal_unit * 0.7) < (time - edge_down_time)) {
        edge_up_time = time;
      } 
    }
    // 2. 直前が High，現在が Low の場合(立ち下がり)
    else if(prev_v != 0 && value == 0) {
      edge_down_time = time;
      
      // デコード
      signal_len = edge_down_time - edge_up_time;
      // if(signal_unit == 0) {
      //   signal_unit = signal_len;
      //   // Serial.printf("signal_unit: %lld\r\n", signal_unit);
      // }
      // else {
      //   if(((signal_unit / 2) < signal_len) && data_len < BINARY_BUFFER_SIZE+1) {
      //     // データが0/1を判定
      //     if(signal_unit < signal_len * 0.6) {
      //       result += '1';          
      //     }
      //     else {
      //       result += '0';
      //     }
      //     data_len++;
      //   }  
      // }

      if(((signal_unit / 2) < signal_len) && data_len < BINARY_BUFFER_SIZE+1) {
        // データが0/1を判定
        if(signal_unit < signal_len * 0.55) {
          result += '1';
        }
        else {
          result += '0';
        }
        data_len++;
        i++;
      }  
    }
    // 3. 直前と現在が同じ場合
    else {
      // 何もしない
    }

    // 直前の値を更新
    prev_v = value;
  }
  // Serial.printf("result: %s\r\n", result.c_str());
  return result;
} // LED_read_binary

inline int get_light_intensity() {
  int intensity = 0;
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, 0);
  pinMode(LED_PIN, INPUT);
  intensity = analogRead(LED_PIN);
  return intensity;
}
