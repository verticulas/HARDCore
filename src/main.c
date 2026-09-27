/*
 * LCD1602 (HD44780) vadība un pinu nolasīšana tīrā AVR C valodā.
 * Bez Arduino bibliotēkām: tikai AVR reģistri (DDRx, PORTx, PINx, UBRR0...).
 *
 * Slēgums (tāds pats kā LiquidCrystal lcd(12, 11, 5, 4, 3, 2)):
 *   LCD RS -> D12 = PB4
 *   LCD E  -> D11 = PB3
 *   LCD D4 -> D5  = PD5
 *   LCD D5 -> D4  = PD4
 *   LCD D6 -> D3  = PD3
 *   LCD D7 -> D2  = PD2
 *   LCD RW -> GND (tikai rakstīšana)
 *
 * Pie katra E impulsa programma nolasa visu LCD pinu stāvokli no PINB/PIND
 * un izdrukā to seriālajā portā (9600 bodi).
 */

#include <avr/io.h>
#include <util/delay.h>
#include <stdint.h>

class a{

};

/* ---------- Pinu definīcijas ---------- */
#define RS_BIT  PB4
#define E_BIT   PB3
#define D4_BIT  PD5
#define D5_BIT  PD4
#define D6_BIT  PD3
#define D7_BIT  PD2

/* ---------- UART (seriālais ports) ---------- */
static void uart_init(void)
{
    uint16_t ubrr = (F_CPU / 16UL / 9600UL) - 1;   /* 103 pie 16 MHz */
    UBRR0H = (uint8_t)(ubrr >> 8);
    UBRR0L = (uint8_t)ubrr;
    UCSR0B = (1 << TXEN0);                          /* ieslēgt raidītāju */
    UCSR0C = (1 << UCSZ01) | (1 << UCSZ00);         /* 8 biti, 1 stop bits */
}

static void uart_putc(char c)
{
    while (!(UCSR0A & (1 << UDRE0)))                /* gaida, kamēr buferis brīvs */
        ;
    UDR0 = c;
}

static void uart_puts(const char *s)
{
    while (*s)
        uart_putc(*s++);
}

static void uart_hex(uint8_t v)
{
    const char *h = "0123456789ABCDEF";
    uart_putc(h[v >> 4]);
    uart_putc(h[v & 0x0F]);
}

/* ---------- Pinu nolasīšana ---------- */
/* PINx reģistrs rāda pina faktisko līmeni, arī ja pins ir izeja. */
static uint8_t read_bit(volatile uint8_t *pinreg, uint8_t bit)
{
    return (*pinreg >> bit) & 1;
}

static void print_pins(const char *kad)
{
    uint8_t rs = read_bit(&PINB, RS_BIT);
    uint8_t e  = read_bit(&PINB, E_BIT);
    uint8_t d4 = read_bit(&PIND, D4_BIT);
    uint8_t d5 = read_bit(&PIND, D5_BIT);
    uint8_t d6 = read_bit(&PIND, D6_BIT);
    uint8_t d7 = read_bit(&PIND, D7_BIT);
    uint8_t nibble = (d7 << 3) | (d6 << 2) | (d5 << 1) | d4;

    uart_puts(kad);
    uart_puts(" RS="); uart_putc('0' + rs);
    uart_puts(" E=");  uart_putc('0' + e);
    uart_puts(" D7="); uart_putc('0' + d7);
    uart_puts(" D6="); uart_putc('0' + d6);
    uart_puts(" D5="); uart_putc('0' + d5);
    uart_puts(" D4="); uart_putc('0' + d4);
    uart_puts("  nibble=0x"); uart_putc("0123456789ABCDEF"[nibble]);
    uart_puts(rs ? "  (dati)\r\n" : "  (komanda)\r\n");
}

/* ---------- LCD zemā līmeņa funkcijas ---------- */
static void set_bit(volatile uint8_t *port, uint8_t bit, uint8_t val)
{
    if (val) *port |=  (1 << bit);
    else     *port &= ~(1 << bit);
}

/* E impulss: displejs nolasa D4..D7 uz E krītošās frontes */
static void pulse_enable(void)
{
    set_bit(&PORTB, E_BIT, 1);
    _delay_us(1);
    print_pins("E^");            /* nolasa pinus, kamēr E = 1 */
    set_bit(&PORTB, E_BIT, 0);
    _delay_us(100);              /* lielākajai daļai komandu pietiek ar 37 us */
}

/* Nosūta 4 bitus uz D4..D7 */
static void write4(uint8_t nibble)
{
    set_bit(&PORTD, D4_BIT, (nibble >> 0) & 1);
    set_bit(&PORTD, D5_BIT, (nibble >> 1) & 1);
    set_bit(&PORTD, D6_BIT, (nibble >> 2) & 1);
    set_bit(&PORTD, D7_BIT, (nibble >> 3) & 1);
    pulse_enable();
}

/* Nosūta pilnu baitu: rs=0 komanda, rs=1 dati */
static void lcd_send(uint8_t value, uint8_t rs)
{
    uart_puts(rs ? "-- dati 0x" : "-- komanda 0x");
    uart_hex(value);
    uart_puts("\r\n");

    set_bit(&PORTB, RS_BIT, rs);
    write4(value >> 4);          /* vispirms augšējie 4 biti */
    write4(value & 0x0F);        /* tad apakšējie */
}

static void lcd_command(uint8_t cmd) { lcd_send(cmd, 0); }
static void lcd_data(uint8_t c)      { lcd_send(c, 1); }

/* ---------- LCD augstā līmeņa funkcijas ---------- */
static void lcd_init(void)
{
    /* pini kā izejas */
    DDRB |= (1 << RS_BIT) | (1 << E_BIT);
    DDRD |= (1 << D4_BIT) | (1 << D5_BIT) | (1 << D6_BIT) | (1 << D7_BIT);

    _delay_ms(50);               /* displejam jāgaida pēc ieslēgšanas */
    set_bit(&PORTB, RS_BIT, 0);
    set_bit(&PORTB, E_BIT, 0);

    /* Inicializācija 4 bitu režīmā pēc HD44780 datu lapas */
    uart_puts("== inicializacija ==\r\n");
    write4(0x03); _delay_ms(5);
    write4(0x03); _delay_us(150);
    write4(0x03); _delay_us(150);
    write4(0x02);                /* pārslēdz uz 4 bitu režīmu */

    lcd_command(0x28);           /* 4 biti, 2 rindas, 5x8 fonts */
    lcd_command(0x0C);           /* displejs ieslēgts, kursors izslēgts */
    lcd_command(0x01);           /* notīrīt ekrānu */
    _delay_ms(2);                /* clear ir lēna komanda */
    lcd_command(0x06);           /* pēc rakstīšanas kursors pa labi */
}

static void lcd_set_cursor(uint8_t col, uint8_t row)
{
    static const uint8_t offsets[2] = { 0x00, 0x40 };
    if (row > 1) row = 1;
    lcd_command(0x80 | (col + offsets[row]));
}

static void lcd_print(const char *s)
{
    while (*s)
        lcd_data((uint8_t)*s++);
}

/* ---------- Galvenā programma ---------- */
int main(void)
{
    uart_init();
    uart_puts("\r\nLCD pinu nolasisana\r\n");

    lcd_init();

    uart_puts("== teksts ==\r\n");
    lcd_set_cursor(0, 0);
    lcd_print("Sveiki!");
    lcd_set_cursor(0, 1);
    lcd_print("Raw AVR C");

    uart_puts("== gatavs ==\r\n");

    for (;;) {
        /* dīkstāvē reizi sekundē parāda pinu stāvokli */
        print_pins("idle");
        _delay_ms(1000);
    }
}





















// #include <Arduino.h>
// #include <LiquidCrystal.h>

// LiquidCrystal lcd(12, 11, 5, 4, 3, 2); // RS, E, D4, D5, D6, D7

// byte heart[8] = {
//   0b00000,
//   0b01010,
//   0b11111,
//   0b11111,
//   0b01110,
//   0b00100,
//   0b00000,
//   0b00000
// };

// // byte sirds[8] = {
// //   0b00000,
// //   0b00000,
// //   0b00000,
// //   0b00000,
// //   0b00000,
// //   0b01010,
// //   0b11111,
// //   0b11111,
// // };
// // byte sirds2[8] =
// // {
// //   0b01110,
// //   0b00100,
// //   0b00000,
// //   0b00000,
// //   0b00000,
// //   0b00000,
// //   0b00000,
// //   0b00000
// // };

// void setup() {
//   lcd.begin(16, 2);
//   lcd.createChar(0, heart);
//   lcd.setCursor(0, 1);
//   // lcd.createChar(0, sirds2);
//   lcd.write(byte(0));
//   lcd.createChar(1, heart);
//   lcd.setCursor(1, 1);
//   lcd.write(byte(1));
// }

// void loop() 
// {

// }