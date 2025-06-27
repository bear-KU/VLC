#define LED_PIN 4
#define REST 1

#define SEND_LEADER() { \
  digitalWrite(LED_PIN, 1); \
  delay(REST); \
}
#define SEND_DATA0() { \
  digitalWrite(LED_PIN, 0); \
  delay(REST); \
  digitalWrite(LED_PIN, 1); \
  delay(REST); \
}
#define SEND_DATA1() { \
  digitalWrite(LED_PIN, 0); \
  delay(REST); \
  digitalWrite(LED_PIN, 1); \
  delay(REST*2); \
}
#define SEND_TRAILER() { \
  digitalWrite(LED_PIN, 0); \
  delay(REST); \
  digitalWrite(LED_PIN, 1); \
  delay(REST*5); \
}

inline char dtoc(int n) {
  if (n < 0 || n > 9) return '\0';  
  return '0' + n;
}


void setup() {
  Serial.begin(115200);

  pinMode(LED_PIN, OUTPUT);

  Serial.printf("Booting\r\n");
  delay(1000);

  Serial.printf("Start\r\n");
  delay(1000);

  int i, bit;
  const int n = 1000;
  char data[n+1] = {0};

  for (i=0; i<n; i++) {
    bit = random(0, 2);
    data[i] = dtoc(bit);
  }
  data[n] = '\0';
  Serial.printf("Data: %s\r\n", data);

  LED_send(data);
  Serial.printf("End\r\n");
}

void loop() {

}

// int checkbit(char data, int bit) {
//   return data & (0x80 >> bit);
// }

void LED_send(char* data) {
  int i;
  int data_len = strlen(data);

  Serial.printf("\r\nReady...\r\n");

  // リーダ
  SEND_LEADER();

  // データ
  for(i=0; i<data_len; i++) {
    if(data[i] == '1') {
      SEND_DATA1();
    }
    else {
      SEND_DATA0();
    }
  }

  // トレイラ
  SEND_TRAILER();

  digitalWrite(LED_PIN, 0);
}
