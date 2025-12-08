#include <Arduino.h>
#include "config.h"
#include "led_signal.h"

void setup()
{
  Serial.begin(115200);
  while (!Serial)
  {
    // wait for serial port to connect. Needed for native USB
  }

  delay(3000);

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  delay(1000);

  String data = "Hello, world from esp32!";
  led_signal(data);
}


void loop()
{

}
