-- Esquema D1 — borrador de CONTEXTO_CLOUDFLARE_WEBHOST_v3.md, sección 5. Sin aplicar todavía.
-- Pendiente antes de 35 equipos: política de retención/agregado de `lecturas` (sin definir).

CREATE TABLE cuidadores (
  id           INTEGER PRIMARY KEY AUTOINCREMENT,
  email        TEXT UNIQUE NOT NULL,
  pass_hash    TEXT NOT NULL,
  pass_salt    TEXT NOT NULL,
  nombre       TEXT,
  creado       INTEGER NOT NULL
);

-- `serial` es el codigo que escribe el cuidador para vincular su cuenta al
-- equipo (ej. "C1001-0001"); nunca es la MAC. Se precarga junto con `mac` y
-- `token_hash` cuando flasheamos/entregamos el equipo, con `cuidador_id` en
-- NULL hasta que alguien lo reclama con su numero de serie (POST /api/vincular,
-- pendiente del paso 4). Decision 2026-11-04, reemplaza el autoregistro por
-- correo que tenia la version Firebase.
-- `serial` sin UNIQUE inline a proposito: SQLite no permite agregar una
-- columna UNIQUE con ALTER TABLE, así que el unique real va en el índice de
-- abajo (misma convención que usamos al migrar la base ya creada en D1).
CREATE TABLE dispositivos (
  mac              TEXT PRIMARY KEY,
  serial           TEXT,
  alias            TEXT,
  cuidador_id      INTEGER REFERENCES cuidadores(id),
  token_hash       TEXT NOT NULL,
  modo_solicitado  TEXT CHECK(modo_solicitado IN ('FALL','SLEEP')),
  modo_aplicado    TEXT CHECK(modo_aplicado   IN ('FALL','SLEEP')),
  solicitado_en    INTEGER,
  visto_en         INTEGER,
  creado           INTEGER NOT NULL
);
CREATE UNIQUE INDEX idx_dispositivos_serial ON dispositivos(serial);

-- Columnas anulables: en FALL no hay respiracion/bpm, en SLEEP no hay rango_mov.
CREATE TABLE lecturas (
  id            INTEGER PRIMARY KEY AUTOINCREMENT,
  mac           TEXT NOT NULL REFERENCES dispositivos(mac),
  ts            INTEGER NOT NULL,
  modo          TEXT NOT NULL,
  temp_c        REAL,
  hum_pct       REAL,
  voc_idx       INTEGER,
  presencia     INTEGER,
  rango_mov     INTEGER,
  resp_rpm      INTEGER,
  bpm           INTEGER,
  estado_sueno  INTEGER
);
CREATE INDEX idx_lecturas_mac_ts ON lecturas(mac, ts DESC);

CREATE TABLE eventos (
  id            INTEGER PRIMARY KEY AUTOINCREMENT,
  mac           TEXT NOT NULL REFERENCES dispositivos(mac),
  ts            INTEGER NOT NULL,
  tipo          TEXT NOT NULL,      -- 'caida' | 'modo_aplicado' | 'sin_contacto'
  detalle       TEXT,
  notificado_en INTEGER
);
CREATE INDEX idx_eventos_mac_ts ON eventos(mac, ts DESC);

CREATE TABLE suscripciones_push (
  id          INTEGER PRIMARY KEY AUTOINCREMENT,
  cuidador_id INTEGER NOT NULL REFERENCES cuidadores(id),
  endpoint    TEXT UNIQUE NOT NULL,
  p256dh      TEXT NOT NULL,
  auth        TEXT NOT NULL,
  creado      INTEGER NOT NULL
);
