#!/usr/bin/env bash
# Compara src/ con lo publicado en Netlify. Reporta diferencias; no modifica nada.
# Ignora los scripts que Netlify inyecta en runtime (/.netlify/scripts/*).
SITE="${SITE:-https://teleco2-senalesysistemas.netlify.app}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
for f in index.html assets/favicon.svg assets/vendor/three.min.js assets/vendor/OrbitControls.js dashboard.html; do
  code=$(curl -s -o /tmp/_live -w "%{http_code}" "$SITE/$f")
  if [ "$code" != 200 ]; then echo "[NO PUBLICADO] $f ($code)"; continue; fi
  if diff -q <(grep -v '/.netlify/scripts/' /tmp/_live) "$ROOT/src/$f" >/dev/null; then
    echo "[IGUAL]        $f"
  else
    echo "[DIFERENTE]    $f"
  fi
done
rm -f /tmp/_live
