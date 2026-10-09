#include <Arduino.h>
#include <LiquidCrystal_I2C.h>
#include <saite.h>

// RX, vienvirziena: klausās, atmet atkārtojumus, pārbauda kontrolsummu.
// Nekad neraida. LCD: I2C modulis (4 vadi).

const int MODE     = 0;      // 0 darbs, 1 kalibrēšana
const int MY_TH    = 33;     // slieksnis pēc kalibrēšanas
const int WORD_MAX = 12;

LiquidCrystal_I2C lcd(0x27, 16, 2);   // dažiem moduļiem 0x3F

byte lastSeq = 1, sum = 0;
bool gotEot = false;
int n = 0;
unsigned long lastFrame = 0;

void at(int col, int row, const char *s) { lcd.setCursor(col, row); lcd.print(s); }

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
  Serial.begin(9600);
  saite::begin();
  saite::TH = MY_TH;
  lcd.init();
  lcd.backlight();
  at(0, 0, MODE == 1 ? "Kalibresana" : "RX klausas");
  delay(1000);
  if (MODE == 1) lcd.clear();
}

void loop() {
  if (MODE == 1) { calibrate(); return; }

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