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
  const int data_size = 1024;
  const int chunk_size = 8;
  int count = data_size / chunk_size;

  char data[data_size+1] = {0};
  char chunk[chunk_size+1] = {0};
  
  chunk[data_size] = '\0';

  for (i=0; i<data_size; i++) {
    bit = random(0, 2);
    data[i] = dtoc(bit);
  }
  
  Serial.printf("Data: %s\r\n", data);

  for (i=0; i<count; i++) {
    memcpy(chunk, data + i * chunk_size, chunk_size);
    Serial.printf("Chunk %d: %s\r\n", i, chunk);
    LED_send(chunk);
    delay(5000);
  }

  Serial.printf("End\r\n");

}

void loop() {

}

void LED_send(char* data) {
  int i;
  int data_len = strlen(data);

  Serial.printf("\r\nReady...\r\n");

  // リーダ
  send_leader();

  // データ
  for(i=0; i<data_len; i++) {
    if(data[i] == '1') {
      send_data1();
    }
    else {
      send_data0();
    }
  }

  // トレイラ
  send_trailer();

  digitalWrite(LED_PIN, 0);
}
