const int led = 4;

void setup() {
  Serial.begin(115200);
}

void loop() {
  Serial.printf("Light Intensity: %d, \r\n", get_light_intensity());
  delay(1000);
}

int get_light_intensity()
{
  int intensity = 0;
  pinMode(led, OUTPUT);
  digitalWrite(led, 0);
  // delay(200);
  pinMode(led, INPUT);
  intensity = analogRead(led);
  return intensity;
}
