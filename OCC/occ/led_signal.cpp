#include <Arduino.h>
#include "config.h"
#include "led_signal.h"

void leader() {
  digitalWrite(LED_PIN, 1);
  delayMicroseconds(SIGNAL_LEN*8);
  digitalWrite(LED_PIN, 0);
  delayMicroseconds(SIGNAL_LEN*4);
}

void data0() {
  digitalWrite(LED_PIN, 0);
  delayMicroseconds(SIGNAL_LEN);
  digitalWrite(LED_PIN, 1);
  delayMicroseconds(SIGNAL_LEN);
}

void data1() {
  digitalWrite(LED_PIN, 0);
  delayMicroseconds(SIGNAL_LEN);
  digitalWrite(LED_PIN, 1);
  delayMicroseconds(SIGNAL_LEN*2);
}

void trailer() {
  digitalWrite(LED_PIN, 0);
  delayMicroseconds(SIGNAL_LEN);
  digitalWrite(LED_PIN, 1);
  delayMicroseconds(SIGNAL_LEN*5);
}

int checkbit(char data, int bit) {
  return data & (0x80 >> bit);
}


void led_signal(String data) {
  int i, j;
  int n = 0;
  int data_len = data.length();
  int data_binary[data_len * 8] = {};

  for(i=0; i<data_len; i++) {
    for (j=0; j<8; j++) {
      data_binary[n] = checkbit(data[i], j) ? 1 : 0;
      n++;
    }
  }

  leader();
  for(i=0; i<n; i++) {
    if(data_binary[i]) {
      data1();
    }
    else {
      data0();
    }
  }
  trailer();
  digitalWrite(LED_PIN, 0);
}
