# teleco_dsp — C1001 · ESP32 · Firebase RTDB · Cloudflare Workers

Control local de lo publicado en https://teleco-dsp.pradovasquezm.workers.dev/
(el sitio ya migró de Netlify a Cloudflare Workers; `netlify.toml` queda en el repo
como residuo de la configuración anterior, sin usarse).

```
src/                       <- directorio publicado (wrangler.toml: [assets] directory = "./src")
  index.html               <- landing de producto (monitoreo y asistencia), paleta propia
  simulador.html           <- gemelo digital (simulación académica, NO tocar su lógica)
  login.html / registro.html  <- autenticación Firebase
  dashboard.html           <- dashboard en vivo (Firebase RTDB), botón CSV completo + modo "Simulado"
  assets/
    favicon.svg
    enclosure-c1001.png    <- render del enclosure real, generado desde hardware/cad/*.step
    vendor/                <- three.js, OrbitControls (para simulador.html)
  deprecated_backup/       <- páginas previas al rediseño de index.html/simulador.html
worker/index.js            <- Worker de Cloudflare: sirve src/ como assets estáticos (sin rutas de API todavía)
wrangler.toml               <- config de Cloudflare: assets + D1 binding (teleco-dsp-db, aún sin usar)
schema.sql                  <- borrador de esquema D1, SIN aplicar todavía (ver cabecera del archivo)
firmware/
  esp32_c1001_firebase/     <- firmware vigente (Firebase RTDB), versionado en carpetas _vN/
  esp32_c1001_blynk/, esp32_c1001_blynk_12/   <- sketches ESP32 + C1001 + SHT40/SGP40 + Blynk
  reference/                <- ejemplos DFRobot (basics, fall, sleep)
  secrets.h.example         <- plantilla; secrets.h real está en .gitignore
hardware/cad/                <- modelos STEP: C1001, SGP40 (válido también para SHT4x), ESP32 DevKit V1
utils/serve.sh                <- servidor local en :8080
utils/check_live.sh           <- compara src/ vs producción
docs/                          <- snapshots y notas (la documentación "oficial" vive en Windows, ver docs/README.md)
```

Flujo: editar en `src/` → probar local con `utils/serve.sh` → `git push` a `main` (el usuario
pushea manualmente) → `npx wrangler deploy` para publicar en Cloudflare. El push a GitHub y el
deploy a Cloudflare son pasos separados — uno no dispara al otro automáticamente.

Secretos: nunca en el repo. Copia `firmware/secrets.h.example` a `secrets.h` junto a cada `.ino`.
