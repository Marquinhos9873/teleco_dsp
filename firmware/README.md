# firmware/
Sketches del ESP32 (Arduino/PlatformIO).

- `reference/` — ejemplos originales de la librería DFRobot_HumanDetection (basics/fall/sleep), sin modificar.
- `esp32_c1001_blynk/`, `esp32_c1001_blynk_12/` — pruebas con Blynk + SHT40/SGP40.
- `esp32_c1001_firebase/` — C1001 (modo Sleep, solo parámetros de `basics.ino`) publicando por REST a Firebase RTDB bajo `/dispositivos/{mac}/estado_actual/radar` + `/historial/radar`. Aprovisiona WiFi con portal cautivo (`WiFiManager`, librería de tzapu — instalar desde el Library Manager) en vez de credenciales fijas, porque cada equipo se conecta a una red distinta según quién lo use. Ver `diagramas/mapa_flujo_datos.md` (sección 3.3) para la estrategia completa de recuperación de MAC. Sin probar aún en hardware real.

Los sketches con Blynk (`esp32_c1001_blynk*`) sí necesitan su propio `secrets.h` (copiar desde `secrets.h.example`, nunca se commitea) porque usan WiFi fijo. `esp32_c1001_firebase` ya no usa `secrets.h` — el WiFi y el correo del propietario se capturan en runtime vía el portal cautivo.
