# Wokwi

| Mape | Kas tā ir | Darbojas |
|------|-----------|----------|
| tx/  | TX: Uno + 16 pinu LCD + potenciometrs + 220 Ω + poga + 4,7 kΩ | jā, firmware `.pio/build/tx` |
| rx/  | RX: Uno + I2C LCD + 4,7 kΩ + 2× 1 MΩ | jā, firmware `.pio/build/rx` (klusē, jo nav signāla) |
| abi/ | abas plates vienā diagrammā | tikai skatīšanai, Wokwi darbina vienu MCU |

Palaišana VS Code:
1. `pio run -e tx` (vai `-e rx`)
2. F1 → `Wokwi: Select Config File` → izvēlies `wokwi/<mape>/wokwi.toml`
3. F1 → `Wokwi: Start Simulator`

Elektrodi (kapacitīvā saite) netiek simulēti nevienā versijā.
