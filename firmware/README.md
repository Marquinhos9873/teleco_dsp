# firmware/
Sketches del ESP32 (Arduino/PlatformIO).

- `reference/` — ejemplos originales de la librería DFRobot_HumanDetection (basics/fall/sleep), sin modificar.
- `esp32_c1001_blynk/`, `esp32_c1001_blynk_12/` — pruebas con Blynk + SHT40/SGP40.
- `esp32_c1001_firebase/` — C1001 (modo Sleep, solo parámetros de `basics.ino`) publicando por REST a Firebase RTDB (`estado_actual/radar` + `historial/radar`). Sin probar aún en hardware real.

Cada carpeta de sketch necesita su propio `secrets.h` (copiar desde `secrets.h.example`, nunca se commitea).
