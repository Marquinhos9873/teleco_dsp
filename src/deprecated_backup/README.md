# deprecated_backup/

- **Raíz** (`dashboard.html`, `index.html`, `login.html`, `registro.html`): copia de `src/*.html`
  tal como estaban el 2026-10-28, antes del rediseño de producto hecho con la skill
  `frontend-design` (hero/landing, portal de ingreso con layout partido, gemelo digital
  reubicado al final de la página).
- **`2026-11-07_pre-modo-oscuro-y-resumen-visual/`**: copia de esos mismos 4 archivos tal como
  estaban en el commit `2148f77` ("cambio a cloudflare"), justo antes de: el toggle de modo
  oscuro/claro (agregado por la sesión de Cloudflare) y la fusión de "Ambiente" dentro de
  "Última lectura" con íconos por tile (ver `diagramas/mapa_flujo_datos.md`).
- **`2026-11-08_sonido-anomalo-y-hero-medivara/`**: copia de `dashboard.html` e `index.html` tal
  como estaban en el commit `a73bb9b`, justo antes de: reemplazar el sonido de alerta (de pitidos
  repetidos a un timbre de dos notas) + el botón "Simular valor anómalo (SLEEP)" + la tile `warn`
  persistente en `dashboard.html`; y el hero con círculo dibujado a mano, tarjeta mockup en vivo y
  sección de pasos con fondo oscuro en `index.html` (inspirado en una referencia de diseño tipo
  Medivara que trajo el usuario).
- **`2026-11-08_hero-roto-primera-version/`**: copia de `index.html` tal como quedó publicada en
  producción en el primer intento del hero tipo Medivara — con bugs reales confirmados con
  capturas de pantalla (Playwright): el hero seguía con fondo claro y la tarjeta flotando en un
  vacío sin relación con el texto, el círculo a mano resaltaba "cámaras" (la palabra que se está
  negando, confuso), el aro de pulso se rompía en mobile, y los números "01/02/03" se veían como
  un ícono en vez de un número (un `.num{}` global de 22px/circular, ya existente para otra cosa
  en la página, se colaba porque la regla nueva no pisaba `background`/`width`/`height`). Se
  corrigió todo en el siguiente commit — este archivo queda solo como referencia del error.

Ninguna copia de esta carpeta se sirve con un link activo desde el sitio — es solo historial
para comparar o recuperar algo si un rediseño rompe algo.
