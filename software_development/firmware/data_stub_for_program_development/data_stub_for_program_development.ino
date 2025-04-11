#include <Arduino.h>

void setup() {
  Serial.begin(2000000);
}


void loop() {
  uint32_t gpio6 = GPIO6_DR;
  uint32_t gpio7 = GPIO7_DR;

  uint8_t buffer[8];
  memcpy(buffer, &gpio6, 4);
  memcpy(buffer + 4, &gpio7, 4);

  Serial.write(buffer, 8);  // Send dummy 8-byte packet
  // delayMicroseconds(10000);  // Adjust speed if needed
  delay(1000);
}
