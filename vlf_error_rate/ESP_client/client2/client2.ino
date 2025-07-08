#include <WiFi.h>
#include "server_comm.h"
#include "config.h"

WiFiClient client;

inline void connect_to_WiFi();


void setup() {
  Serial.begin(115200);
  delay(1000);

  connect_to_WiFi();

#ifdef ROLE_SENDER
  Serial.println("Running as SENDER");
  comm_as_sender(client);
#endif
#ifdef ROLE_RECEIVER
  Serial.println("Running as RECEIVER");
  comm_as_receiver(client);
#endif

}

void loop() {
}


inline void connect_to_WiFi() {
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");

  int attempts = 0;
  while(WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if(WiFi.status() != WL_CONNECTED) {
    Serial.println("\nFailed to connect to Wi-Fi");
    while(1); // Stay in a loop if WiFi connection fails
  }
  Serial.println("\nWi-Fi connected!");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}
