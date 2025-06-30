#include "esp_timer.h"
#include <Wire.h>
#include "SSD1306Wire.h"

#define LED_PIN  4
SSD1306Wire display(0x3c, 21, 22, GEOMETRY_128_32); // I2C address, SDA, SCL


void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  delay(100);

  // Wait for serial connection
  while (!Serial) {
    ;
  }
}

void loop() {
  char str[100] = "\0";
  
  Serial.printf("-----------------------------------\r\n");
  LED_read_binary(str);
  Serial.printf("Data: %s\r\n\n", str);

  draw_display(str);
  delay(5000);
}

void LED_read_binary(char *str) {
  int value = 0, prev_v = 0;
  int64_t signal_unit = 0, signal_len = 0;
  int64_t time;
  int64_t edge_down_time = 0;
  int64_t edge_up_time = 0;
  int str_len = 0, binary_len = 0;
  unsigned char data = 0;
  int i = 0;

  int threshold = 150;

  pinMode(LED_PIN, INPUT);
  Serial.printf("Ready to read...\r\n");

  Serial.printf("Threshold before calibration: %d\r\n", threshold);
  while(i < 1000) {
    value = get_light_intensity();
    delay(1);
    if (threshold < value) {
      threshold = value;
    }
    Serial.printf("i: %d, value: %d, threshold: %d\r\n", i, value, threshold);
    i++;
  }
  Serial.printf("Threshold after calibration: %d\r\n", threshold);
  delay(1000);


  while(1) {
    time = esp_timer_get_time();

    value = get_light_intensity() - threshold;
    if (value < 0) {
      value = 0;
    }
    Serial.printf("prev_v: %d, value: %d\r\n", prev_v, value);

    // 終了判定
    if( (signal_unit != 0) &&                              // リーダを読んだ
        (signal_unit * 4 < (time - edge_up_time)) &&  // トレイラを読んだ
     	  (edge_down_time < edge_up_time))                // ...
    {
      Serial.printf("Finished receiving.\r\n\r\n");
      break; // 終了
    }

    // バイナリデータを判断???
    // 1. 直前が Low，現在が High の場合(立ち上がり)
    if(prev_v == 0 && value != 0) {
      // ノイズによる誤検出を防ぐ
      if((signal_unit * 0.7) < (time - edge_down_time)) {
        edge_up_time = time;
        Serial.printf("edge_up_time: %llf\r\n", edge_up_time);
      } 
    }
    // 2. 直前が High，現在が Low の場合(立ち下がり)
    else if(prev_v != 0 && value == 0) {
      // ノイズによる誤検出を防ぐ -> TODO: ここはまだ実装していない
      
      edge_down_time = time;
      Serial.printf("edge_down_time: %lld\r\n", edge_down_time);
      
      // デコード
      signal_len = edge_down_time - edge_up_time;
      if(signal_unit == 0) {
        // 最初の信号(リーダ)の信号長を基本時間 T とする
        signal_unit = signal_len;
        Serial.printf("Read leader\r\n");
        Serial.printf("signal_unit: %lld\r\n", signal_unit);
      }
      else {
        // ノイズによる誤検出を防ぐ
        if(((signal_unit / 2) < signal_len) && str_len < 100) {
          // データが0/1を判定
          if(signal_unit < signal_len * 0.6) {
            data |= 0x01;
            Serial.printf("1");
          }
          else {
            data |= 0x00;
            Serial.printf("0");
          }
          
          if(binary_len == 7) {
            // 1バイト分のデータが揃った
            str[str_len] = data;
            str_len++;
            Serial.printf(" --> '%c'\r\n", data);

            data = 0; // 次のデータ用に初期化
            binary_len = 0; // 次のデータ用に初期化
          }
          else {
            data <<= 1; // 次のビット用に左シフト
            binary_len++;
          }
        }  
      }
    } // else if(prev_v != 0 && value != 0))
    // 3. 直前と現在が同じ場合
    else {
      // 何もしない
    }

    // 直前の値を更新
    prev_v = value;

  } // while(1)
} // LED_read_binary

int get_light_intensity() {
  int intensity = 0;
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, 0);
  pinMode(LED_PIN, INPUT);
  intensity = analogRead(LED_PIN);
  return intensity;
}

void draw_display(char *str) {
  display.init();
  display.clear();
  display.setFont(ArialMT_Plain_16);
  display.setTextAlignment(TEXT_ALIGN_LEFT);
  display.drawString(0, 0, str);
  display.display();
}
