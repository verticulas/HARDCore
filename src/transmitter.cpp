#include <Arduino.h>
#include <LiquidCrystal.h>
#include <saite.h>

// TX, vienvirziena, ar seriālo konsoli.
// Poga: nejaušs vārds. Seriālais: ieraksti vārdu un Enter, tiek nosūtīts tas.
// Komandas sākas ar '!':
//   !b   bāka ieslēgt/izslēgt (0x55 bez apstājas, RX kalibrēšanai)
//   !b HH bāka ar citu baitu, piem. !b A3
//   !t   nepārtraukts tonis ieslēgt/izslēgt (multimetram Hz, pjezo)
//   !u   viens 0x55 kadrs
//   !r N atkārtojumu skaits (1..9)
//   !v   detalizēta izdruka ieslēgt/izslēgt
//   ?    palīdzība
// Rindas datoram (tools/txmon.py):
//   @R,BIT,FREQ,atk        gatavs pēc starta
//   @W,laiks,VARDS         sākas vārds
//   @F,laiks,i,kopa,hex,seq,biti   katrs kadrs (10 biti: starts, 8 dati, paritāte)
//   @S,laiks,summa,ilgums  vārds beidzies
// LCD: 16 pinu paralēlais, 4 bitu režīms.

const int WORD_MAX = 12;      // RX LCD vietas
const int WORD_LEN = 4;       // nejaušā vārda garums pogai
const int PIN_BTN  = 2;       // poga uz GND

//                RS  E  D4 D5 D6 D7
LiquidCrystal lcd(12, 11, 5, 4, 3, 7);

int  repeatN = 3;
bool verbose = true;
bool beaconOn = false, toneOn = false;
byte beaconByte = 0x55;

char line[WORD_MAX + 4];
int  lineLen = 0;

void at(int col, int row, const char *s) { lcd.setCursor(col, row); lcd.print(s); }

void status(const char *s) {
  lcd.setCursor(0, 1); lcd.print("                ");
  lcd.setCursor(0, 1); lcd.print(s);
}

void help() {
  Serial.println(F("--- TX konsole ---"));
  Serial.println(F("VARDS<Enter>  nosuta vardu (32..126 ASCII, lidz 12)"));
  Serial.println(F("!b baka (!b A3 cits baits)  !t tonis  !u viens 0x55  !r N atkartojumi  !v izdruka  ? palidziba"));
  Serial.print(F("atkartojumi=")); Serial.print(repeatN);
  Serial.print(F(" BIT=")); Serial.print(saite::BIT);
  Serial.print(F("ms FREQ=")); Serial.println(saite::FREQ);
}

bool buttonPressed() {
  if (digitalRead(PIN_BTN) == HIGH) return false;
  delay(30);
  while (digitalRead(PIN_BTN) == LOW) ;
  return true;
}

// Mašīnlasāma rinda datoram: @F,laiks,i,kopa,dati_hex,seq,biti(10)
void printFrame(int i, int total, byte d, byte seq, byte f) {
  if (!verbose) return;
  Serial.print(F("@F,")); Serial.print(millis());
  Serial.print(','); Serial.print(i); Serial.print(','); Serial.print(total);
  Serial.print(','); if (d < 16) Serial.print('0'); Serial.print(d, HEX);
  Serial.print(','); Serial.print(seq); Serial.print(',');
  Serial.print('1');                                   // starta bits
  for (int b = 7; b >= 0; b--) Serial.print(bitRead(f, b));
  Serial.println(saite::oddOnes(f) ? 1 : 0);           // paritāte
}

void sendRepeated(int i, int total, byte d, byte seq) {
  byte f = (d & 0x7F) | (byte)(seq << 7);
  printFrame(i, total, d, seq, f);
  for (int k = 0; k < repeatN; k++) saite::sendByte(f);
}

void sendWord(const char *msg, int len) {
  if (toneOn) { saite::listen(); toneOn = false; }
  beaconOn = false;

  lcd.clear();
  at(0, 0, "TX:");
  lcd.print(msg);
  Serial.print(F("@W,")); Serial.print(millis()); Serial.print(','); Serial.println(msg);

  const int total = len + 2;
  byte seq = 0, sum = 0;
  unsigned long t0 = millis();
  char buf[17];

  for (int i = 0; i < len; i++) {
    snprintf(buf, sizeof buf, "%d/%d", i + 1, total); status(buf);
    sum += msg[i];
    sendRepeated(i + 1, total, msg[i], seq); seq ^= 1;
  }
  snprintf(buf, sizeof buf, "%d/%d EOT", len + 1, total); status(buf);
  sendRepeated(len + 1, total, saite::EOT, seq); seq ^= 1;

  snprintf(buf, sizeof buf, "%d/%d SUM", total, total); status(buf);
  sendRepeated(total, total, sum & 0x7F, seq);

  status("NOSUTITS");
  Serial.print(F("@S,")); Serial.print(millis()); Serial.print(',');
  Serial.print(sum & 0x7F); Serial.print(','); Serial.println(millis() - t0);
}

void randomWord() {
  char msg[WORD_LEN + 1];
  for (int i = 0; i < WORD_LEN; i++) msg[i] = 'A' + random(26);
  msg[WORD_LEN] = 0;
  sendWord(msg, WORD_LEN);
}

void command(const char *c) {
  switch (c[1]) {
    case 'b':
      if (c[2]) { beaconByte = strtol(c + 2, NULL, 16); beaconOn = true; }
      else beaconOn = !beaconOn;
      if (toneOn) { saite::listen(); toneOn = false; }
      Serial.print(beaconOn ? F("baka ON 0x") : F("baka OFF 0x")); Serial.println(beaconByte, HEX);
      lcd.clear(); at(0, 0, beaconOn ? "Baka" : "TX gatavs");
      break;
    case 't':
      toneOn = !toneOn; beaconOn = false;
      if (toneOn) tone(saite::PIN_OUT, saite::FREQ); else saite::listen();
      Serial.println(toneOn ? F("tonis ON") : F("tonis OFF"));
      lcd.clear(); at(0, 0, toneOn ? "Tonis 3170 Hz" : "TX gatavs");
      break;
    case 'u':
      Serial.println(F("0x55"));
      sendRepeated(1, 1, 0x55, 0);
      break;
    case 'r': {
      int n = atoi(c + 2);
      if (n >= 1 && n <= 9) repeatN = n;
      Serial.print(F("atkartojumi=")); Serial.println(repeatN);
      break;
    }
    case 'v':
      verbose = !verbose;
      Serial.println(verbose ? F("izdruka ON") : F("izdruka OFF"));
      break;
    default:
      help();
  }
}

void handleLine() {
  line[lineLen] = 0;
  if (lineLen == 0) return;
  if (line[0] == '?') { help(); return; }
  if (line[0] == '!') { command(line); return; }

  char msg[WORD_MAX + 1]; int n = 0;
  for (int i = 0; i < lineLen && n < WORD_MAX; i++)
    if (line[i] >= 32 && line[i] < 127) msg[n++] = line[i];
  msg[n] = 0;
  if (n) sendWord(msg, n);
}

void pollSerial() {
  while (Serial.available()) {
    char ch = Serial.read();
    if (ch == '\r' || ch == '\n') { handleLine(); lineLen = 0; }
    else if (lineLen < (int)sizeof(line) - 1) line[lineLen++] = ch;
  }
}

void setup() {
  Serial.begin(9600);
  saite::begin();
  pinMode(PIN_BTN, INPUT_PULLUP);
  randomSeed(analogRead(A1) ^ micros());
  lcd.begin(16, 2);
  at(0, 0, "TX gatavs");
  at(0, 1, "Poga vai serial");
  help();
  Serial.print(F("@R,")); Serial.print(saite::BIT); Serial.print(',');
  Serial.print(saite::FREQ); Serial.print(','); Serial.println(repeatN);
}

void loop() {
  pollSerial();
  if (beaconOn) { saite::sendByte(beaconByte); delay(200); return; }
  if (buttonPressed()) randomWord();
}