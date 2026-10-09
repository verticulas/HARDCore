#include <Arduino.h>
#include <LiquidCrystal_I2C.h>
#include <saite.h>

// RX, vienvirziena: klausās, atmet atkārtojumus, pārbauda kontrolsummu.
// Nekad neraida. LCD: I2C modulis (4 vadi).

const int MODE     = 0;      // 0 darbs, 1 kalibrēšana, 2 diagnostika (115200 bodi)
const int MY_TH    = 33;     // slieksnis pēc kalibrēšanas
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
  Serial.begin(MODE == 2 ? 115200 : 9600);
  saite::begin();
  saite::TH = MY_TH;
  lcd.init();
  lcd.backlight();
  at(0, 0, MODE == 1 ? "Kalibresana" : MODE == 2 ? "Diagnostika" : "RX klausas");
  delay(1000);
  if (MODE == 1) lcd.clear();
  if (MODE == 2) { Serial.print(F("@R,")); Serial.print(saite::BIT); Serial.print(',');
                   Serial.print(SLOT_MS); Serial.print(','); Serial.println(saite::TH); }
}

void loop() {
  if (MODE == 1) { calibrate(); return; }
  if (MODE == 2) { diagnose(); return; }

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
  // katrs kadrs datoram (tools/txmon.py): @B,baits_hex,paritāte(1 ok/0 bojāts),starts,fons,slieksnis
  Serial.print(F("@B,")); if (f < 16) Serial.print('0'); Serial.print(f, HEX);
  Serial.print(','); Serial.print(r);
  Serial.print(','); Serial.print(saite::lastRef);      // starta bita līmenis
  Serial.print(','); Serial.print(saite::floorLvl);     // fons
  Serial.print(','); Serial.println(saite::lastBitTH);  // izmantotais slieksnis
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