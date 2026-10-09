#include <Arduino.h>

const bool CAL = true;   // true: kalibrēšana, false: dekodēšana
const int  TH  = 10;     // slieksnis, ieraksti pēc kalibrēšanas!
const int  BIT = 50;     // jāsakrīt ar raidītāju

int measure(int ms) {                        // lielākais lēciens starp paraugiem
  int prev = analogRead(A0), best = 0;
  unsigned long t = millis();
  while (millis() - t < (unsigned long)ms) {
    int v = analogRead(A0);
    if (v - prev > best) best = v - prev;
    prev = v;
  }
  return best;
}

void setup() {
  ADCSRA = (ADCSRA & 0xF8) | 0x04;           // ātrais ADC, ķer īsos 2 MΩ impulsus
  Serial.begin(9600);
}

void loop() {
  if (CAL) {                                 // kalibrēšanas režīms
    Serial.println(measure(30));
    return;
  }

  if (measure(5) < TH) return;               // gaida starta bitu
  unsigned long t0 = millis();
  byte b = 0;
  for (int i = 0; i < 8; i++) {
    while (millis() - t0 < (unsigned long)(BIT * (i + 1) + 10)) ;
    if (measure(30) > TH) bitSet(b, 7 - i);
  }
  while (millis() - t0 < (unsigned long)(BIT * 10)) ;
  Serial.write(b);
}