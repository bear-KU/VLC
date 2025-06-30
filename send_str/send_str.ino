#define LED_PIN 4
#define REST 1


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


void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(LED_PIN, OUTPUT);
}

void loop() {
  String data = "Hello World!";

  Serial.printf("\r\n----------------------------------------\r\n");
  Serial.printf("data: %s\r\n", data);

  LED_send(data);
}

int checkbit(char data, int bit) {
  return data & (0x80 >> bit);
}

void LED_send(String data) {
  int i, j;
  int n = 0;
  int data_len = data.length();
  int data_binary[data_len * 8] = {};

  for(i=0; i<data_len; i++) {
    for (j=0; j<8; j++) {
      data_binary[n] = checkbit(data[i], j) ? 1 : 0; // 0 or 1
      n++;
    }
  }

  Serial.printf("\r\nReady...\r\n");

  // リーダ
  send_leader();

  // データ
  for(i=0; i<n; i++) {
    if(data_binary[i]) {
      send_data1();
    }
    else {
      send_data0();
    }
  }

  // トレイラ
  send_trailer();

  digitalWrite(LED_PIN, 0);
  delay(5000);
}
