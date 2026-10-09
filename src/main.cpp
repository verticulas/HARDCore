#include <Arduino.h>

const int BIT = 50, FREQ = 3170, PIN = 8;

void sendByte(byte b) {
  tone(PIN, FREQ); delay(BIT);
  for (int i = 7; i >= 0; i--) {
    if (bitRead(b, i)) tone(PIN, FREQ); else noTone(PIN);
    delay(BIT);
  }
  noTone(PIN); delay(2 * BIT);
}

char c = 'A';

void setup() { pinMode(LED_BUILTIN, OUTPUT); }

void loop() {
  digitalWrite(LED_BUILTIN, HIGH);     // deg, kamēr raida
  sendByte(c);
  digitalWrite(LED_BUILTIN, LOW);
  c = (c == 'Z') ? 'A' : c + 1;
  delay(1000);
}