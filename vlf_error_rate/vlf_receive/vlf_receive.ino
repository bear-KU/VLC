#include "esp_timer.h"
// #include <Wire.h>
// #include "SSD1306Wire.h"

#define MAX_STR_LEN 1000
#define LED_PIN  4
// SSD1306Wire display(0x3c, 21, 22, GEOMETRY_128_32); // I2C address, SDA, SCL


void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  delay(100);

  // Wait for serial connection
  while (!Serial) {
    ;
  }

  char data[MAX_STR_LEN+1] = {0};

  Serial.printf("-----------------------------------\r\n");
  LED_read_binary(data);
  Serial.printf("Data: %s\r\n", data);

  // draw_display(data);
}

void loop() {
  
}

void LED_read_binary(char *data) {
  int value = 0, prev_v = 0;
  int64_t signal_unit = 0, signal_len = 0;
  int64_t time;
  int64_t edge_down_time = 0;
  int64_t edge_up_time = 0;
  int data_len = 0;
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

    // 終了判定
    if( (signal_unit != 0) &&
        (signal_unit * 4 < (time - edge_up_time)) &&
     	  (edge_down_time < edge_up_time))
    {
      Serial.printf("Finished receiving.\r\n\r\n");
      break; // 終了
    }

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
      if(signal_unit == 0) {
        signal_unit = signal_len;
      }
      else {
        if(((signal_unit / 2) < signal_len) && data_len < MAX_STR_LEN+1) {
          // データが0/1を判定
          if(signal_unit < signal_len * 0.6) {
            data[data_len] = '1';          
          }
          else {
            data[data_len] = '0';
          }
          data_len++;
        }  
      }
    }
    // 3. 直前と現在が同じ場合
    else {
      // 何もしない
    }

    // 直前の値を更新
    prev_v = value;

  }
} // LED_read_binary

inline int get_light_intensity() {
  int intensity = 0;
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, 0);
  pinMode(LED_PIN, INPUT);
  intensity = analogRead(LED_PIN);
  return intensity;
}

// void draw_display(char *str) {
//   display.init();
//   display.clear();
//   display.setFont(ArialMT_Plain_16);
//   display.setTextAlignment(TEXT_ALIGN_LEFT);
//   display.drawString(0, 0, str);
//   display.display();
// }
