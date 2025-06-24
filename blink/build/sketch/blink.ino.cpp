#include <Arduino.h>
#line 1 "/home/cslt19/Arduino/blink/blink.ino"
int led = 4;

#line 3 "/home/cslt19/Arduino/blink/blink.ino"
void setup();
#line 8 "/home/cslt19/Arduino/blink/blink.ino"
void loop();
#line 3 "/home/cslt19/Arduino/blink/blink.ino"
void setup() {
  // put your setup code here, to run once:
  pinMode(led, OUTPUT);
}

void loop() {
  // put your main code here, to run repeatedly:
  digitalWrite(led, HIGH);
  delay(2000);
  digitalWrite(led, LOW);
  delay(500);
}

