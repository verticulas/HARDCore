#pragma once
#include <Arduino.h>

// Divvirzienu saite pa vienu antēnu katrai platei.
// D8 caur 4,7 kΩ raida, A0 caur 4,7 kΩ klausās, no A0 uz GND 2 MΩ.
// Kadrs: starta bits + 8 datu biti + paritātes bits, katrs BIT ms, tad klusums.
// Baita augstākais bits ir kārtas bits (0/1), dati ir 7 bitu ASCII.

namespace saite {

const int PIN_OUT = 8;
const int PIN_IN  = A0;
const int BIT     = 50;     // ms, abām platēm jābūt vienādam
const int FREQ    = 3170;   // Hz

const byte ACK = 0x06;
const byte EOT = 0x04;

static int TH = 33;         // slieksnis, katrai platei savs (iestata skicē)

inline void begin() {
  ADCSRA = (ADCSRA & 0xF8) | 0x04;   // ātrais ADC, ķer īsos impulsus
  pinMode(PIN_OUT, INPUT);           // sākumā klausās
}

// Atlaiž raidīšanas pinu: augsta pretestība, antēna brīva klausīšanai
inline void listen() {
  noTone(PIN_OUT);
  pinMode(PIN_OUT, INPUT);
}

// Lielākais lēciens starp blakus paraugiem logā
inline int measure(int ms) {
  int prev = analogRead(PIN_IN), best = 0;
  unsigned long t = millis();
  while (millis() - t < (unsigned long)ms) {
    int v = analogRead(PIN_IN);
    if (v - prev > best) best = v - prev;
    prev = v;
  }
  return best;
}

inline bool oddOnes(byte b) {
  bool p = false;
  for (int i = 0; i < 8; i++) if (bitRead(b, i)) p = !p;
  return p;
}

inline void sendByte(byte b) {
  tone(PIN_OUT, FREQ); delay(BIT);                              // starta bits
  for (int i = 7; i >= 0; i--) {
    if (bitRead(b, i)) tone(PIN_OUT, FREQ); else noTone(PIN_OUT);
    delay(BIT);
  }
  if (oddOnes(b)) tone(PIN_OUT, FREQ); else noTone(PIN_OUT);    // pāra paritāte
  delay(BIT);
  listen();
  delay(2 * BIT);                                               // klusums, otrs pārslēdzas
}

// 1 = baits saņemts, 0 = kadrs bojāts (paritāte), -1 = nekas laikā
inline int receiveByte(byte &out, unsigned long timeoutMs) {
  listen();
  unsigned long start = millis();
  while (measure(5) < TH) {
    if (millis() - start > timeoutMs) return -1;
  }
  unsigned long t0 = millis();
  byte b = 0;
  for (int i = 0; i < 8; i++) {
    while (millis() - t0 < (unsigned long)(BIT * (i + 1) + 10)) ;
    if (measure(30) > TH) bitSet(b, 7 - i);
  }
  while (millis() - t0 < (unsigned long)(BIT * 9 + 10)) ;
  bool p = measure(30) > TH;
  while (millis() - t0 < (unsigned long)(BIT * 11)) ;
  if (p != oddOnes(b)) return 0;
  out = b;
  return 1;
}

// Nosūta 7 bitu datus ar kārtas bitu un gaida ACK. true, ja apstiprināts.
inline bool sendReliable(byte data, byte &seq, int tries = 5) {
  byte frame = (data & 0x7F) | (byte)(seq << 7);
  byte want  = ACK | (byte)(seq << 7);
  for (int k = 0; k < tries; k++) {
    sendByte(frame);
    byte a;
    if (receiveByte(a, 1500) == 1 && a == want) {
      seq ^= 1;
      return true;
    }
  }
  return false;
}

// 1 = jauni dati, 0 = dublikāts vai bojāts kadrs, -1 = nekas laikā
inline int receiveReliable(byte &data, byte &expect, unsigned long timeoutMs) {
  byte f;
  int r = receiveByte(f, timeoutMs);
  if (r != 1) return r;
  byte s = f >> 7;
  delay(2 * BIT);                          // sūtītājs pārslēdzas uz klausīšanos
  sendByte(ACK | (byte)(s << 7));          // apstiprina arī dublikātu
  if (s != expect) return 0;               // iepriekšējais ACK pazuda, šis ir atkārtojums
  expect ^= 1;
  data = f & 0x7F;
  return 1;
}

// Kalibrēšanas bāka: nepārtraukti sūta 0x55 (01010101)
inline void beacon() {
  sendByte(0x55);
  delay(200);
}

}  // namespace saite