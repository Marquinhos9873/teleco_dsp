#!/usr/bin/env bash
# Sirve src/ en local (http://localhost:8080). Necesario para probar el dashboard
# con http:// y no file:// (Firebase Auth no funciona bien desde file://).
cd "$(dirname "$0")/../src" && python3 -m http.server "${1:-8080}"
