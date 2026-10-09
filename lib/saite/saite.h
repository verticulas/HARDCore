#pragma once
#include <Arduino.h>

// Divvirzienu saite pa vienu antēnu katrai platei.
// D8 caur 4,7 kΩ raida, A0 caur 4,7 kΩ klausās, no A0 uz GND 2 MΩ.
// Ar -D SAITE_IQ (platformio.ini build_flags) RX mēra tikai raidītāja toni.
// Kadrs: starta bits + 8 datu biti + paritātes bits, katrs BIT ms, tad klusums.
// Baita augstākais bits ir kārtas bits (0/1), dati ir 7 bitu ASCII.

namespace saite {

const int PIN_OUT = 8;
const int PIN_IN  = A0;
const int BIT     = 50;     // ms, abām platēm jābūt vienādam
const int FREQ    = 3170;   // Hz
const int GAP     = 3 * BIT;  // klusums starp kadriem (TX)
const int QUIET   = 2 * BIT + 20;  // RX gaida startu tikai pēc tik ilga klusuma

const byte ACK = 0x06;
const byte EOT = 0x04;

static int TH = 33;         // slieksnis, katrai platei savs (iestata skicē)
static bool RELATIVE = true; // bitu slieksni rēķina no starta bita un fona
static bool AUTO_TH = true;  // starta slieksni pielāgo fonam darbības laikā
static int floorLvl = 0;    // fona līmenis, mērīts klusumā
static long floor16 = 0, dev16 = 0;      // fons un tā svārstības ×16
static unsigned long lastLoud = 0;       // pēdējais brīdis, kad bija tonis
static int lastRef = 0, lastBitTH = 0, lastStartTH = 0;   // diagnostikai

// Starta slieksnis: fons + 6 svārstības, bet ne zemāk par TH (TH = minimums)
inline int startTH() {
  if (!AUTO_TH) return TH;
  int t = (int)((floor16 + 6 * dev16) / 16) + 3;
  return t > TH ? t : TH;
}

inline void begin() {
  ADCSRA = (ADCSRA & 0xF8) | 0x04;   // ātrais ADC, ķer īsos impulsus
  pinMode(PIN_OUT, INPUT);           // sākumā klausās
}

// Atlaiž raidīšanas pinu: augsta pretestība, antēna brīva klausīšanai
inline void listen() {
  noTone(PIN_OUT);
  pinMode(PIN_OUT, INPUT);
}

#ifdef SAITE_IQ
// Sinhronā detekcija tieši raidītāja tonī.
// tone(FREQ) uz Timer2 reāli dod F_CPU/64/(F_CPU/64/FREQ): 3170 -> 3205,13 Hz.
// ADC brīvajā režīmā ar dalītāju 16 dod 16e6/16/13 = 76923 paraugus/s,
// tas ir tieši 24 paraugi uz tona periodu. Katru paraugu reizina ar
// sin/cos tabulu un summē: tona amplitūda summējas, viss pārējais izdzēšas.
// RX redz nevis sinusu, bet īsas smailes frontēs, tāpēc vajag daudz paraugu
// periodā; ar 4 paraugiem rezultāts bija atkarīgs no fāzes.
const int8_t SIN24[24] = {0, 16, 32, 45, 55, 61, 63, 61, 55, 45, 32, 16,
                          0, -16, -32, -45, -55, -61, -63, -61, -55, -45, -32, -16};

// Tona amplitūda (relatīvās vienībās), 5 ms blokos: ms/5 bloki, vismaz 1
inline int measure(int ms) {
  int blocks = ms / 5; if (blocks < 1) blocks = 1;
  analogRead(PIN_IN);                                    // iestata ADMUX uz A0
  uint8_t sa = ADCSRA, sb = ADCSRB;
  ADCSRB = 0;                                            // brīvais režīms
  ADCSRA = _BV(ADEN) | _BV(ADATE) | _BV(ADIF) | 0x04;    // dalītājs 16
  ADCSRA |= _BV(ADSC);
  long total = 0;
  for (int blk = 0; blk < blocks; blk++) {
    long I = 0, Q = 0;
    uint8_t ph = 0, pq = 6;                              // cos = sin + 90°
    for (int n = 0; n < 24 * 16; n++) {                  // 16 periodi ≈ 5 ms
      while (!(ADCSRA & _BV(ADIF))) ;
      ADCSRA |= _BV(ADIF);
      int x = ADC;
      if (x > 511) x = 511;                              // reizinājums ietilpst int
      I += (int)(x * SIN24[ph]);
      Q += (int)(x * SIN24[pq]);
      if (++ph == 24) ph = 0;
      if (++pq == 24) pq = 0;
    }
    long ai = labs(I), aq = labs(Q);
    total += (ai > aq) ? ai + aq * 3 / 8 : aq + ai * 3 / 8;   // ≈ sqrt(I²+Q²)
  }
  ADCSRA = sa; ADCSRB = sb;
  while (ADCSRA & _BV(ADSC)) ;                           // pēdējā konversija
  return (int)(total / blocks / 1512);                   // mērogs: 35 ADC smaile ≈ 58
}
#else
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
#endif

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
  delay(GAP);                                                   // klusums: kadra robeža
}

// 1 = baits saņemts, 0 = kadrs bojāts (paritāte), -1 = nekas laikā
inline int receiveByte(byte &out, unsigned long timeoutMs) {
  listen();
  unsigned long start = millis();
  int v, sth;
  // Starts skaitās tikai tad, ja pirms tā bija vismaz QUIET ms klusuma.
  // Tā RX neiekrīt kadra vidū un nepaliek "iesprūdis" nobīdītā ritmā.
  while (true) {
    v = measure(5);
    sth = startTH();
    if (v >= sth) {
      if (millis() - lastLoud >= (unsigned long)QUIET) break;
      lastLoud = millis();                      // tonis, bet bez klusuma pirms tā
    } else {
      // klusumā mācās fonu: vidējais un vidējā novirze (lēni, ~16 mērījumi)
      long d = (long)v * 16 - floor16;
      floor16 += d / 16;
      dev16 += ((d < 0 ? -d : d) - dev16) / 16;
      floorLvl = (int)(floor16 / 16);
    }
    if (millis() - start > timeoutMs) return -1;
  }
  lastStartTH = sth;
  unsigned long t0 = millis();
  // Relatīvais slieksnis: vidus starp starta bita līmeni un fonu.
  // Ja signāls vājāks (tālāk), slieksnis nolaižas līdzi.
  int bitTH = TH;
  if (RELATIVE) {
    int ref = measure(20);                      // starta bita atlikums
    int mid = (ref + floorLvl) / 2;
    if (mid > bitTH) bitTH = mid;
    lastRef = ref;
  }
  lastBitTH = bitTH;
  byte b = 0;
  for (int i = 0; i < 8; i++) {
    while (millis() - t0 < (unsigned long)(BIT * (i + 1) + 10)) ;
    if (measure(30) > bitTH) bitSet(b, 7 - i);
  }
  while (millis() - t0 < (unsigned long)(BIT * 9 + 10)) ;
  bool p = measure(30) > bitTH;
  lastLoud = millis();                          // kadra beigas; klusums skaitās no šejienes
  out = b;                                // arī bojātu kadru atdod, diagnostikai
  if (p != oddOnes(b)) return 0;
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