# teleco_dsp — C1001 · ESP32 · Firebase RTDB · Netlify

Control local de lo publicado en https://teleco2-senalesysistemas.netlify.app/

```
src/                 <- directorio publicado (netlify.toml: publish = "src"), sin build step
  index.html         <- gemelo digital (simulación, NO tocar su lógica)
  dashboard.html     <- dashboard en vivo (Firebase RTDB)
  assets/            <- favicon y vendor/ (three.js, OrbitControls)
firmware/
  esp32_c1001_blynk/, esp32_c1001_blynk_12/   <- sketches ESP32 + C1001 + SHT40/SGP40 + Blynk
  reference/         <- ejemplos DFRobot (basics, fall, sleep)
  secrets.h.example  <- plantilla; secrets.h real está en .gitignore
hardware/cad/        <- modelos STEP: C1001, SGP40 (válido también para SHT4x), ESP32 DevKit V1
utils/serve.sh       <- servidor local en :8080
utils/check_live.sh  <- compara src/ vs producción
docs/                <- snapshots y notas
```
Flujo: editar en `src/` → `utils/serve.sh` → push a `main` → Netlify despliega `src/`.

Secretos: nunca en el repo. Copia `firmware/secrets.h.example` a `secrets.h` junto a cada `.ino`.
