#include <Arduino.h>
#include <LiquidCrystal.h>
// #include <cstdint>

static constexpr byte heart[8] = 
{
  0b00000,
  0b01010,
  0b11111,
  0b11111,
  0b01110,
  0b00100,
  0b00000,
  0b00000
};

// galvenais būs žonglieris 

struct Coordinates
{
  byte x;
  bool y;
};

struct Entity
{
  const byte *figure = heart;
  Coordinates location = {0, 1};
};

struct GoodEntity
{

};

static constexpr uint16_t tick = 100;
//max buffer potenciāli jāmaina
static constexpr uint16_t customFigureMaxBuffer = sizeof(byte)*8;

void jump(Entity *entity)
{

}

//šo drīzāk var kā event kad visus 
void move() {}

uint16_t getTick()
{
  return (uint16_t)millis() / tick;
}

LiquidCrystal lcd(12, 11, 5, 4, 3, 2);

void setup() {
  
  // lcd.begin(16, 2);
  lcd.createChar(0, heart);
  lcd.setCursor(0, 1);
  // lcd.createChar(0, sirds2);
  lcd.write(byte(0));
  lcd.createChar(1, heart);
  lcd.setCursor(1, 1);
  lcd.write(byte(1));
}

void loop() 
{

}