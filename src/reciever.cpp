#include <Arduino.h>

const int BIT  = 50;     // ms vienam bitam, jāsakrīt ar uztvērēju
const int FREQ = 3170;   // Hz, nav 50 Hz daudzkārtnis
const int PIN  = 8;      // caur 4,7 kΩ uz stiepli A

void sendByte(byte b) {
  tone(PIN, FREQ); delay(BIT);               // starta bits
  for (int i = 7; i >= 0; i--) {
    if (bitRead(b, i)) tone(PIN, FREQ); else noTone(PIN);
    delay(BIT);
  }
  noTone(PIN); delay(2 * BIT);               // klusums starp baitiem
}

void setup() { Serial.begin(9600); }

void loop() {
  if (Serial.available()) {
    byte b = Serial.read();
    sendByte(b);
    Serial.write(b);                         // atbalss: ko tieši nosūtīja
  }
}