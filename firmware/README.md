# firmware/
Sketches del ESP32 (Arduino/PlatformIO).

- `reference/` — ejemplos originales de la librería DFRobot_HumanDetection (basics/fall/sleep), sin modificar.
- `esp32_c1001_blynk/`, `esp32_c1001_blynk_12/` — pruebas con Blynk + SHT40/SGP40.
- `esp32_c1001_firebase/` — C1001 publicando por REST a Firebase RTDB bajo `/dispositivos/{mac}/estado_actual/radar` + `/historial/radar`. Aprovisiona WiFi con portal cautivo (`WiFiManager`, librería de tzapu — instalar desde el Library Manager) en vez de credenciales fijas, porque cada equipo se conecta a una red distinta según quién lo use. **Probado en hardware real por primera vez el 2026-10-29** (versión `_v3`): se encontró y corrigió un bug de visibilidad del AP de configuración (ver `diagramas/mapa_flujo_datos.md`, sección 7.1). `_v3` también agrega control remoto de modo FALL/SLEEP vía `/dispositivos/{mac}/comando/modo_solicitado` (reemplaza los botones V10 de Blynk) y lee los campos propios de modo FALL, no solo Sleep.

Los sketches con Blynk (`esp32_c1001_blynk*`) sí necesitan su propio `secrets.h` (copiar desde `secrets.h.example`, nunca se commitea) porque usan WiFi fijo. `esp32_c1001_firebase` ya no usa `secrets.h` — el WiFi y el correo del propietario se capturan en runtime vía el portal cautivo.

## Convención de versiones dentro de `esp32_c1001_firebase/`

El Arduino IDE exige que el archivo principal de un sketch esté dentro de una carpeta con el
**mismo nombre exacto** (ej. `MiSketch/MiSketch.ino`), si no, no lo abre. Para no perder el
historial de iteraciones y poder abrir cualquier versión directamente en el IDE:

- `ESP32_C1001_Firebase.ino` (suelto, en la raíz de esta carpeta) = **punto de partida / backup**.
  No se edita más — es la referencia congelada desde la que arrancan las versiones numeradas.
- Cada iteración nueva vive en su propia carpeta `ESP32_C1001_Firebase_vN/ESP32_C1001_Firebase_vN.ino`
  (nombre de carpeta y de archivo iguales, como pide el IDE). La versión más alta de `N` es la
  vigente; las anteriores quedan como historial, no se borran.
- Vigente: `ESP32_C1001_Firebase_v3/` (fix de AP + botón BOOT + control remoto de modo). `v2`
  queda como historial. La próxima vez que se modifique, se crea `ESP32_C1001_Firebase_v4/`.
