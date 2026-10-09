#include <Arduino.h>
#include <LiquidCrystal.h>
#include <saite.h>

// TX, vienvirziena: pēc pogas izdomā nejaušu vārdu un raida to.
// Katrs kadrs 3 reizes, beigās EOT un kontrolsumma. Atbildi negaida.
// LCD: 16 pinu paralēlais, 4 bitu režīms.

const int  MODE     = 0;      // 0 darbs, 2 bāka (RX kalibrēšanai)
const bool AUTO     = false;  // true: jauns vārds ik pēc 20 s bez pogas
const int  WORD_LEN = 4;
const int  REPEAT   = 3;      // cik reizes atkārto katru kadru
const int  PIN_BTN  = 2;      // poga uz GND

//                RS  E  D4 D5 D6 D7
LiquidCrystal lcd(12, 11, 5, 4, 3, 7);
unsigned long lastRound = 0;

void at(int col, int row, const char *s) { lcd.setCursor(col, row); lcd.print(s); }

bool startPressed() {
  if (AUTO) return millis() - lastRound > 20000;
  if (digitalRead(PIN_BTN) == HIGH) return false;
  delay(30);
  while (digitalRead(PIN_BTN) == LOW) ;
  return true;
}

void sendRepeated(byte d, byte seq) {
  byte f = (d & 0x7F) | (byte)(seq << 7);
  for (int k = 0; k < REPEAT; k++) saite::sendByte(f);
}

void setup() {
  Serial.begin(9600);
  saite::begin();
  pinMode(PIN_BTN, INPUT_PULLUP);
  randomSeed(analogRead(A1) ^ micros());   // troksnis no nepieslēgta pina
  lcd.begin(16, 2);
  at(0, 0, MODE == 2 ? "Baka" : "TX gatavs");
  at(0, 1, (MODE == 0 && !AUTO) ? "Spied pogu" : "");
}

void loop() {
  if (MODE == 2) { saite::beacon(); return; }
  if (!startPressed()) return;
  lastRound = millis();

  char msg[WORD_LEN + 1];
  for (int i = 0; i < WORD_LEN; i++) msg[i] = 'A' + random(26);
  msg[WORD_LEN] = 0;

  lcd.clear();
  at(0, 0, "TX:");
  Serial.print("TX "); Serial.println(msg);

  const int frames = WORD_LEN + 2;          // burti + EOT + summa
  byte seq = 0, sum = 0;

  for (int i = 0; i < WORD_LEN; i++) {
    lcd.setCursor(3 + i, 0); lcd.print(msg[i]);
    lcd.setCursor(9, 0); lcd.print(i + 1); lcd.print('/'); lcd.print(frames); lcd.print("  ");
    sum += msg[i];
    sendRepeated(msg[i], seq); seq ^= 1;
  }

  lcd.setCursor(9, 0); lcd.print(WORD_LEN + 1); lcd.print('/'); lcd.print(frames);
  sendRepeated(saite::EOT, seq); seq ^= 1;

  lcd.setCursor(9, 0); lcd.print(frames); lcd.print('/'); lcd.print(frames);
  sendRepeated(sum & 0x7F, seq);

  at(0, 1, "NOSUTITS");
  Serial.print("SUM "); Serial.println(sum & 0x7F);
}