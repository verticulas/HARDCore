#include <Arduino.h>
#include <LiquidCrystal_I2C.h>
#include <saite.h>

// RX, vienvirziena: klausās, atmet atkārtojumus, pārbauda kontrolsummu.
// Nekad neraida. LCD: I2C modulis (4 vadi).

const int MODE     = 0;      // 0 darbs, 1 kalibrēšana, 2 diagnostika, 3 neapstrādāti dati (2 un 3: 115200 bodi)
const int MY_TH    = 33;     // minimālais slieksnis; ar AUTO_TH RX pats ceļ virs fona
const int WORD_MAX = 12;

LiquidCrystal_I2C lcd(0x27, 16, 2);   // dažiem moduļiem 0x3F

byte lastSeq = 1, sum = 0;
bool gotEot = false;
int n = 0;
unsigned long lastFrame = 0;

void at(int col, int row, const char *s) { lcd.setCursor(col, row); lcd.print(s); }

// ---- Diagnostika: katru kadru sadala 50 logos pa 10 ms un izdrukā
// lielāko lēcienu katrā logā. tools/linkdiag.py no tā atkodē bitus
// ar jebkuru nobīdi, loga platumu un slieksni.
const int SLOTS = 50, SLOT_MS = 10;
int slotV[SLOTS];
unsigned long diagFrames = 0;
char cmd[12]; int cmdLen = 0;

void diagCommands() {
  while (Serial.available()) {
    char ch = Serial.read();
    if (ch != '\n' && ch != '\r') { if (cmdLen < 11) cmd[cmdLen++] = ch; continue; }
    cmd[cmdLen] = 0;
    if (cmd[0] == 't') { saite::TH = atoi(cmd + 1); }
    Serial.print(F("@C,TH,")); Serial.println(saite::TH);
    cmdLen = 0;
  }
}

void diagnose() {
  diagCommands();
  saite::listen();
  int w = saite::measure(5);
  if (w < saite::TH) return;
  unsigned long t0 = millis();
  for (int k = 0; k < SLOTS; k++) {
    while (millis() - t0 < (unsigned long)(k * SLOT_MS)) ;
    slotV[k] = saite::measure(SLOT_MS - 1);
  }
  Serial.print(F("@D,")); Serial.print(t0); Serial.print(',');
  Serial.print(w); Serial.print(',');
  for (int k = 0; k < SLOTS; k++) { Serial.print(slotV[k]); Serial.print(k < SLOTS - 1 ? ' ' : '\n'); }
  diagFrames++;
  lcd.setCursor(0, 1); lcd.print("Kadri: "); lcd.print(diagFrames);
}

// ---- MODE 3: neapstrādāti dati vizualizācijai (tools/viz.py), 115200 bodi.
// Pēc starta: 480 ADC paraugi (6,2 ms, 24 uz tona periodu), tad 98 bloki
// pa 5 ms ar I un Q summām līdz kadra beigām. Tieši tas, ko dara measure().
const int RAW_N = 480, IQ_N = 98;
uint8_t rawV[RAW_N];
int16_t iqI[IQ_N], iqQ[IQ_N];
const int8_t S24[24] = {0, 16, 32, 45, 55, 61, 63, 61, 55, 45, 32, 16,
                        0, -16, -32, -45, -55, -61, -63, -61, -55, -45, -32, -16};
unsigned long rawFrames = 0;

void rawCapture() {
  saite::listen();
  int v = saite::measure(5);
  int sth = saite::startTH();
  if (v < sth) {                                   // klusums: mācās fonu
    long d = (long)v * 16 - saite::floor16;
    saite::floor16 += d / 16;
    saite::dev16 += ((d < 0 ? -d : d) - saite::dev16) / 16;
    saite::floorLvl = (int)(saite::floor16 / 16);
    return;
  }
  if (millis() - saite::lastLoud < (unsigned long)saite::QUIET) { saite::lastLoud = millis(); return; }

  analogRead(saite::PIN_IN);
  uint8_t sa = ADCSRA, sb = ADCSRB;
  ADCSRB = 0;
  ADCSRA = _BV(ADEN) | _BV(ADATE) | _BV(ADIF) | 0x04;
  ADCSRA |= _BV(ADSC);
  for (int n = 0; n < RAW_N; n++) {
    while (!(ADCSRA & _BV(ADIF))) ;
    ADCSRA |= _BV(ADIF);
    int x = ADC;
    rawV[n] = x > 255 ? 255 : x;
  }
  uint8_t ph = 0, pq = 6;                          // 480 = 20 periodi, fāze turpinās
  for (int k = 0; k < IQ_N; k++) {
    long I = 0, Q = 0;
    for (int n = 0; n < 24 * 16; n++) {
      while (!(ADCSRA & _BV(ADIF))) ;
      ADCSRA |= _BV(ADIF);
      int x = ADC;
      if (x > 511) x = 511;
      I += (int)(x * S24[ph]);
      Q += (int)(x * S24[pq]);
      if (++ph == 24) ph = 0;
      if (++pq == 24) pq = 0;
    }
    iqI[k] = I / 512; iqQ[k] = Q / 512;
  }
  ADCSRA = sa; ADCSRB = sb;
  while (ADCSRA & _BV(ADSC)) ;

  Serial.print(F("@LV,")); Serial.print(v); Serial.print(',');
  Serial.print(saite::floorLvl); Serial.print(','); Serial.println(sth);
  Serial.print(F("@RAW,"));
  for (int n = 0; n < RAW_N; n++) { Serial.print(rawV[n]); Serial.print(n < RAW_N - 1 ? ' ' : '\n'); }
  Serial.print(F("@IQ,"));
  for (int k = 0; k < IQ_N; k++) {
    Serial.print(iqI[k]); Serial.print(' '); Serial.print(iqQ[k]);
    Serial.print(k < IQ_N - 1 ? ' ' : '\n');
  }
  saite::lastLoud = millis();
  rawFrames++;
  lcd.setCursor(0, 1); lcd.print("Kadri: "); lcd.print(rawFrames);
}

void resetWord() { n = 0; sum = 0; gotEot = false; lastSeq = 1; }

void calibrate() {
  static int mx = 0;
  static unsigned long tShow = 0, tReset = 0;
  int v = saite::measure(30);
  Serial.println(v);
  if (v > mx) mx = v;
  if (millis() - tShow > 250) {
    lcd.setCursor(0, 0); lcd.print("Tagad: "); lcd.print(v);  lcd.print("    ");
    lcd.setCursor(0, 1); lcd.print("Max2s: "); lcd.print(mx); lcd.print("    ");
    tShow = millis();
  }
  if (millis() - tReset > 2000) { mx = 0; tReset = millis(); }
}

void setup() {
  Serial.begin(MODE >= 2 ? 115200 : 9600);
  saite::begin();
  saite::TH = MY_TH;
  lcd.init();
  lcd.backlight();
  at(0, 0, MODE == 1 ? "Kalibresana" : MODE == 2 ? "Diagnostika" : MODE == 3 ? "Raw dati" : "RX klausas");
  delay(1000);
  if (MODE == 1) lcd.clear();
  if (MODE == 2) { Serial.print(F("@R,")); Serial.print(saite::BIT); Serial.print(',');
                   Serial.print(SLOT_MS); Serial.print(','); Serial.println(saite::TH); }
}

void loop() {
  if (MODE == 1) { calibrate(); return; }
  if (MODE == 2) { diagnose(); return; }
  if (MODE == 3) { rawCapture(); return; }

  byte f;
  int r = saite::receiveByte(f, 1000);

  if (r == -1) {                               // klusums
    if ((n || gotEot) && millis() - lastFrame > 3000) {
      at(9, 1, "NEPILNS");
      Serial.println("NEPILNS");
      resetWord();
    }
    return;
  }
  // katrs kadrs datoram (tools/txmon.py): @B,baits_hex,paritāte(1 ok/0 bojāts),starts,fons,bitu slieksnis,starta slieksnis
  Serial.print(F("@B,")); if (f < 16) Serial.print('0'); Serial.print(f, HEX);
  Serial.print(','); Serial.print(r);
  Serial.print(','); Serial.print(saite::lastRef);      // starta bita līmenis
  Serial.print(','); Serial.print(saite::floorLvl);     // fons
  Serial.print(','); Serial.print(saite::lastBitTH);    // bitu slieksnis
  Serial.print(','); Serial.println(saite::lastStartTH); // starta slieksnis
  // Līmeņi uz LCD (2. rinda, pa kreisi), lai tos redz arī bez datora: S starts, F fons
  char lv[10];
  snprintf(lv, sizeof lv, "S%-3dF%-3d", saite::lastRef, saite::floorLvl);
  lcd.setCursor(0, 1); lcd.print(lv);
  if (r == 0) return;                          // paritāte nesakrīt: izmet
  lastFrame = millis();

  byte s = f >> 7, d = f & 0x7F;
  if (s == lastSeq) return;                    // atkārtojums, jau saņemts
  lastSeq = s;

  if (gotEot) {                                // šis kadrs ir kontrolsumma
    bool ok = d == (sum & 0x7F);
    at(9, 1, ok ? "OK     " : "KLUDA  ");
    Serial.println(ok ? "OK" : "KLUDA");
    resetWord();
    return;
  }
  if (d == saite::EOT) { gotEot = true; return; }

  if (n == 0) { lcd.clear(); at(0, 0, "RX:"); }
  if (n < WORD_MAX) {
    lcd.setCursor(3 + n, 0); lcd.print((char)d);
    Serial.print("RX "); Serial.println((char)d);
  }
  sum += d;
  n++;
}