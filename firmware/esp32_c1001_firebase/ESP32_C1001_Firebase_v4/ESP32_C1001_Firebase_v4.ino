// ESP32 + C1001 -> Firebase Realtime Database, por REST puro.
//
// Cambio respecto a v3:
//   Reconfiguracion de WiFi a DISTANCIA desde el dashboard (ademas del boton
//   BOOT fisico que ya existia). Util para cuando el equipo se muda de lugar
//   (ej. de la red de casa al hotspot del celular) y no da la gana de ir
//   hasta el equipo a presionar el boton -- ver seccion 7B mas abajo.
//
// Todo lo demas es igual a v3 (ver ese archivo para el detalle completo):
// portal cautivo WiFiManager + correo de propietario, control remoto de modo
// FALL/SLEEP con debounce de 20s, lectura de los parametros propios de cada
// modo. Ver diagramas/mapa_flujo_datos.md, seccion 3, para la estrategia de
// identidad del equipo.
//
// Parametros del radar:
//   Comunes a ambos modos (ver firmware/reference/basics.ino y fall.ino):
//     presencia          <- hu.smHumanData(hu.eHumanPresence)
//     movimiento         <- hu.smHumanData(hu.eHumanMovement)
//     movimiento_valor   <- hu.smHumanData(hu.eHumanMovingRange)
//   Solo en modo Sleep (ver basics.ino):
//     respiracion        <- hu.getBreatheValue()
//     frecuencia_cardiaca<- hu.getHeartRate()
//   Solo en modo Fall (ver fall.ino):
//     estado_caida           <- hu.getFallData(hu.eFallState)
//     permanencia_estatica   <- hu.getFallData(hu.estaticResidencyState)
//
// Pendiente, a proposito (no resolver aqui):
//   - Reglas reales de RTDB (hoy sigue en "modo de prueba", abiertas).
//   - Alertas por email/Telegram ante valores anomalos.
//   - SHT40/SGP40.

#include <WiFi.h>
#include <WiFiManager.h>
#include <Preferences.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include "DFRobot_HumanDetection.h"

// ===================== 1. FIREBASE =====================
// databaseURL del proyecto de prueba iot-monitoring-c1001-fb-test.
// No es secreto (ver nota en CONTEXTO_FIREBASE_WEBHOST_v2.md, seccion 8).
const char* firebaseHost = "https://iot-monitoring-c1001-fb-test-default-rtdb.firebaseio.com";

// ===================== 2. PINES / RADAR =====================
const int RXD2 = 16;
const int TXD2 = 17;
const int PIN_BOTON_RESET_WIFI = 0;  // boton BOOT del DevKit, ya cableado de fabrica

DFRobot_HumanDetection hu(&Serial2);

// WiFiManager ahora es global (antes vivia solo dentro de setup()) porque el
// nuevo comando remoto de reconfiguracion necesita llamar wm.resetSettings()
// desde loop(), no solo una vez al arrancar.
WiFiManager wm;

// ===================== 3. IDENTIDAD DEL EQUIPO (WiFiManager + Preferences) =====================
Preferences preferencias;
String correoPropietario = "";
String macEquipo = "";
String rutaDispositivo = "";  // "dispositivos/<MAC>", se arma en setup() una vez conocida la MAC

bool configuracionNueva = false;
void alGuardarConfigWifi() {
  configuracionNueva = true;
}

// ===================== 4. MODO DEL RADAR (control remoto, portado de Blynk V10) =====================
// 0 = FALL, 1 = SLEEP. Arranca en SLEEP para mantener continuidad con lo que
// ya estaba publicando el equipo antes de este cambio.
int modoActual = 1;
int modoSolicitado = 1;
bool cambioModoPendiente = false;
unsigned long tiempoSolicitudModo = 0;
const unsigned long ESPERA_CAMBIO_MODO = 20000UL;  // 20 s, igual que el Blynk original

const unsigned long INTERVALO_COMANDO = 4000UL;  // revisa los comandos remotos cada 4 s
unsigned long ultimaRevisionComando = 0;

// ===================== 5. TEMPORIZADOR NO BLOQUEANTE DEL RADAR =====================
const unsigned long INTERVALO_RADAR = 2000UL;  // lectura + PUT cada 2 s
const int LECTURAS_POR_HISTORIAL = 15;         // POST a historial cada 15*2s = 30 s

unsigned long ultimaLecturaRadar = 0;
int contadorLecturasRadar = 0;

// ===================== 6. HTTP (lectura y publicacion a Firebase) =====================

bool wifiListo() {
  return WiFi.status() == WL_CONNECTED;
}

// Un solo punto para PUT/POST con el reintento corto (400 ms) ya validado
// en Wokwi (ver CONTEXTO_FIREBASE_WEBHOST_v2.md, seccion 2). No bloquea
// temporizadores porque ocurre dentro de una sola llamada ya programada.
int httpEscribir(const String& metodo, const String& url, const String& cuerpo) {
  if (!wifiListo()) {
    Serial.println("WiFi caido: no se publica (" + url + ")");
    return -100;
  }

  WiFiClientSecure client;
  client.setInsecure();  // TEMPORAL, pendiente de endurecer antes de la demo final

  HTTPClient http;
  http.begin(client, url);
  http.addHeader("Content-Type", "application/json");

  int codigo = (metodo == "POST") ? http.POST(cuerpo) : http.PUT(cuerpo);

  if (codigo < 0) {
    http.end();
    delay(400);
    http.begin(client, url);
    http.addHeader("Content-Type", "application/json");
    codigo = (metodo == "POST") ? http.POST(cuerpo) : http.PUT(cuerpo);
  }

  http.end();
  return codigo;
}

// Lee un valor de Firebase. Devuelve el cuerpo crudo (puede venir con
// comillas si es un string JSON, ej. "FALL") o "" si fallo o no hay WiFi.
String httpLeer(const String& url) {
  if (!wifiListo()) {
    return "";
  }

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  http.begin(client, url);
  int codigo = http.GET();

  String cuerpo = "";
  if (codigo == 200) {
    cuerpo = http.getString();
  }

  http.end();
  return cuerpo;
}

// Quita las comillas de un string JSON simple (ej. "\"FALL\"" -> "FALL").
// No sirve para JSON anidado, solo para los strings sueltos que usamos aqui.
String quitarComillas(String valor) {
  valor.trim();
  if (valor.length() >= 2 && valor.startsWith("\"") && valor.endsWith("\"")) {
    valor = valor.substring(1, valor.length() - 1);
  }
  return valor;
}

void publicarEstadoActual(const String& ruta, const String& camposJson) {
  String url = String(firebaseHost) + "/" + rutaDispositivo + "/estado_actual/" + ruta + ".json";
  int codigo = httpEscribir("PUT", url, camposJson);
  Serial.print("PUT ");
  Serial.print(rutaDispositivo);
  Serial.print("/estado_actual/");
  Serial.print(ruta);
  Serial.print(" -> ");
  Serial.println(codigo);
}

void publicarHistorial(const String& ruta, const String& camposJson) {
  String url = String(firebaseHost) + "/" + rutaDispositivo + "/historial/" + ruta + ".json";
  int codigo = httpEscribir("POST", url, camposJson);
  Serial.print("POST ");
  Serial.print(rutaDispositivo);
  Serial.print("/historial/");
  Serial.print(ruta);
  Serial.print(" -> ");
  Serial.println(codigo);
}

// Reclama/actualiza el propietario de este equipo. Se llama una vez por
// arranque, despues de tener WiFi y MAC. Idempotente: si el correo no
// cambio, vuelve a escribir el mismo valor (no rompe nada); si cambio
// (equipo reconfigurado para otra persona), reasigna el propietario.
void registrarPropietario() {
  if (correoPropietario.length() == 0) {
    Serial.println("ADVERTENCIA: no hay correo guardado, el equipo queda sin propietario");
    return;
  }

  String url = String(firebaseHost) + "/" + rutaDispositivo + "/meta.json";
  String campos = "{\"propietario_correo\": \"" + correoPropietario + "\"" +
                   ", \"fecha_registro\": {\".sv\": \"timestamp\"}}";

  int codigo = httpEscribir("PUT", url, campos);
  Serial.print("PUT ");
  Serial.print(rutaDispositivo);
  Serial.print("/meta -> ");
  Serial.println(codigo);
}

// ===================== 7A. CONTROL REMOTO DE MODO (portado de Blynk V10) =====================

const char* nombreModo(int modo) {
  return modo == 0 ? "FALL" : "SLEEP";
}

void aplicarModoFall() {
  Serial.println("Aplicando modo FALL al C1001...");
  hu.configWorkMode(hu.eFallingMode);
  hu.configLEDLight(hu.eFALLLed, 1);
  hu.sensorRet();
}

void aplicarModoSleep() {
  Serial.println("Aplicando modo SLEEP al C1001...");
  hu.configWorkMode(hu.eSleepMode);
  hu.configLEDLight(hu.eHPLed, 1);
  hu.sensorRet();
}

void aplicarModo(int nuevoModo) {
  if (nuevoModo == 0) {
    aplicarModoFall();
  } else {
    aplicarModoSleep();
  }

  modoActual = nuevoModo;
  cambioModoPendiente = false;
  contadorLecturasRadar = 0;

  Serial.print("Modo aplicado: ");
  Serial.println(nombreModo(modoActual));

  String campo = "\"" + String(nombreModo(modoActual)) + "\"";
  String url = String(firebaseHost) + "/" + rutaDispositivo + "/estado_actual/modo.json";
  httpEscribir("PUT", url, campo);
}

// Revisa /dispositivos/{mac}/comando/modo_solicitado. Si pide un modo
// distinto al actual y no hay ya un cambio pendiente, arranca la espera de
// 20 s (igual que el Blynk original, para evitar falsos disparos por un
// clic accidental en la web).
void revisarComandoModo() {
  String url = String(firebaseHost) + "/" + rutaDispositivo + "/comando/modo_solicitado.json";
  String valor = quitarComillas(httpLeer(url));

  if (valor != "FALL" && valor != "SLEEP") {
    return;  // sin comando valido todavia, o fallo de lectura
  }

  int pedido = (valor == "FALL") ? 0 : 1;

  if (pedido == modoActual) {
    cambioModoPendiente = false;
    return;
  }

  if (!cambioModoPendiente || modoSolicitado != pedido) {
    modoSolicitado = pedido;
    tiempoSolicitudModo = millis();
    cambioModoPendiente = true;
    Serial.print("Cambio de modo solicitado desde la web: ");
    Serial.println(nombreModo(pedido));
  }
}

void procesarCambioModoPendiente() {
  if (!cambioModoPendiente) {
    return;
  }

  if (millis() - tiempoSolicitudModo >= ESPERA_CAMBIO_MODO) {
    aplicarModo(modoSolicitado);
  }
}

// ===================== 7B. RECONFIGURACION REMOTA DE WIFI (nuevo en v4) =====================
// La web escribe PUT /dispositivos/{mac}/comando/reconfigurar_wifi.json con
// el booleano true. Se revisa en el mismo ciclo que el comando de modo (cada
// 4 s). A diferencia del cambio de modo, esto NO espera ningun debounce: es
// una accion deliberada de una sola vez (el usuario ya confirmo en la web),
// asi que se aplica de inmediato.
//
// Importante: esto borra el WiFi guardado (wm.resetSettings()) pero NO el
// correo del propietario (ese vive aparte, en preferencias "c1001"/"correo")
// -- al reconectar, el portal cautivo ya viene con el correo precargado,
// igual que con el boton BOOT fisico.
//
// Tras aplicar, el equipo se reinicia y abre el AP "C1001-Setup" de nuevo.
// Deja de publicar al Firebase actual hasta que alguien lo reconecte a una
// red (la misma u otra) -- es el comportamiento esperado, igual que si se
// hubiera usado el boton BOOT.
void revisarComandoWifi() {
  String url = String(firebaseHost) + "/" + rutaDispositivo + "/comando/reconfigurar_wifi.json";
  String valor = quitarComillas(httpLeer(url));

  if (valor != "true") {
    return;
  }

  Serial.println("Reconfiguracion de WiFi solicitada desde la web. Borrando flag y reiniciando...");

  // Borra el flag ANTES de reiniciar, para que no se dispare de nuevo en el
  // proximo arranque apenas el equipo se reconecte a una red.
  httpEscribir("PUT", url, "false");

  wm.resetSettings();
  delay(300);
  ESP.restart();
}

// ===================== 8. SETUP =====================
void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(PIN_BOTON_RESET_WIFI, INPUT_PULLUP);

  preferencias.begin("c1001", false);
  correoPropietario = preferencias.getString("correo", "");

  // ---------------------------------------------------
  // Fix de visibilidad del AP: en varias combinaciones de core de ESP32 +
  // WiFiManager, si el intento de conexion STA previo falla, el radio WiFi
  // queda en un estado donde el AP "arranca" en el log pero no transmite
  // beacons de verdad (no aparece al escanear). Forzar un reset limpio del
  // stack de WiFi antes de que WiFiManager tome el control evita esto.
  // ---------------------------------------------------
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true, true);
  delay(200);

  wm.setConnectTimeout(15);  // antes esperaba el default (~60 s) por intento

  // Boton BOOT (GPIO0) presionado al arrancar -> olvida el WiFi/correo
  // guardado y fuerza el portal de configuracion. Sigue disponible como
  // alternativa fisica al comando remoto de la seccion 7B.
  if (digitalRead(PIN_BOTON_RESET_WIFI) == LOW) {
    Serial.println("Boton BOOT presionado: borrando configuracion de WiFi guardada...");
    wm.resetSettings();
    correoPropietario = "";
  }

  // Campo extra del portal cautivo: ademas de SSID/contrasena, pedimos el
  // correo de la cuenta para poder reclamar el equipo sin usar la MAC.
  WiFiManagerParameter paramCorreo("correo", "Correo de tu cuenta", correoPropietario.c_str(), 60);
  wm.addParameter(&paramCorreo);
  wm.setSaveConfigCallback(alGuardarConfigWifi);

  Serial.println("Iniciando WiFiManager (si no hay red guardada, abre el AP 'C1001-Setup')...");
  bool conectado = wm.autoConnect("C1001-Setup");

  if (!conectado) {
    Serial.println("No se pudo conectar ni completar la configuracion. Reiniciando...");
    delay(1000);
    ESP.restart();
  }

  if (configuracionNueva) {
    correoPropietario = String(paramCorreo.getValue());
    preferencias.putString("correo", correoPropietario);
    Serial.print("Nuevo correo guardado: ");
    Serial.println(correoPropietario);
  }

  Serial.print("WiFi conectado, IP: ");
  Serial.println(WiFi.localIP());

  macEquipo = WiFi.macAddress();
  rutaDispositivo = "dispositivos/" + macEquipo;

  Serial.print("MAC de este equipo: ");
  Serial.println(macEquipo);
  Serial.print("Ruta Firebase de este equipo: ");
  Serial.println(rutaDispositivo);

  registrarPropietario();

  // ---------------------------------------------------
  // Inicializar el radar C1001 en el modo inicial (SLEEP)
  // ---------------------------------------------------

  Serial2.begin(115200, SERIAL_8N1, RXD2, TXD2);

  Serial.println("Inicializando C1001...");
  while (hu.begin() != 0) {
    Serial.println("init error!!!");
    delay(1000);
  }
  Serial.println("Inicializacion correcta");

  aplicarModo(modoActual);  // SLEEP al arrancar, y ya publica el modo confirmado

  Serial.println("C1001 listo, publicando a Firebase cada 2 s");

  ultimaLecturaRadar = millis() - INTERVALO_RADAR;        // primera lectura casi inmediata
  ultimaRevisionComando = millis() - INTERVALO_COMANDO;   // primera revision casi inmediata
}

// ===================== 9. LOOP =====================
void loop() {
  unsigned long ahora = millis();

  // ----- Comandos remotos (no bloqueante) -----
  if (ahora - ultimaRevisionComando >= INTERVALO_COMANDO) {
    ultimaRevisionComando = ahora;
    revisarComandoModo();
    revisarComandoWifi();  // si esto dispara, el equipo se reinicia dentro de la propia funcion
  }
  procesarCambioModoPendiente();

  // ----- Lectura y publicacion del radar -----
  if (ahora - ultimaLecturaRadar >= INTERVALO_RADAR) {
    ultimaLecturaRadar = ahora;
    contadorLecturasRadar++;

    int presencia = hu.smHumanData(hu.eHumanPresence);
    int movimiento = hu.smHumanData(hu.eHumanMovement);
    int movimientoValor = hu.smHumanData(hu.eHumanMovingRange);

    String campos;

    if (modoActual == 1) {
      // SLEEP: respiracion y frecuencia cardiaca (ver basics.ino)
      int respiracion = hu.getBreatheValue();
      int frecuenciaCardiaca = hu.getHeartRate();

      Serial.print("[SLEEP] Lectura #");
      Serial.print(contadorLecturasRadar);
      Serial.print(" | presencia="); Serial.print(presencia);
      Serial.print(" movimiento="); Serial.print(movimiento);
      Serial.print(" rango="); Serial.print(movimientoValor);
      Serial.print(" resp="); Serial.print(respiracion);
      Serial.print(" fc="); Serial.println(frecuenciaCardiaca);

      campos = "{\"presencia\": " + String(presencia) +
               ", \"movimiento\": " + String(movimiento) +
               ", \"movimiento_valor\": " + String(movimientoValor) +
               ", \"respiracion\": " + String(respiracion) +
               ", \"frecuencia_cardiaca\": " + String(frecuenciaCardiaca) +
               ", \"timestamp\": {\".sv\": \"timestamp\"}}";
    } else {
      // FALL: estado de caida y permanencia estatica (ver fall.ino)
      int estadoCaida = hu.getFallData(hu.eFallState);
      int permanenciaEstatica = hu.getFallData(hu.estaticResidencyState);

      Serial.print("[FALL] Lectura #");
      Serial.print(contadorLecturasRadar);
      Serial.print(" | presencia="); Serial.print(presencia);
      Serial.print(" movimiento="); Serial.print(movimiento);
      Serial.print(" rango="); Serial.print(movimientoValor);
      Serial.print(" caida="); Serial.print(estadoCaida);
      Serial.print(" permanencia="); Serial.println(permanenciaEstatica);

      campos = "{\"presencia\": " + String(presencia) +
               ", \"movimiento\": " + String(movimiento) +
               ", \"movimiento_valor\": " + String(movimientoValor) +
               ", \"estado_caida\": " + String(estadoCaida) +
               ", \"permanencia_estatica\": " + String(permanenciaEstatica) +
               ", \"timestamp\": {\".sv\": \"timestamp\"}}";
    }

    publicarEstadoActual("radar", campos);

    if (contadorLecturasRadar % LECTURAS_POR_HISTORIAL == 0) {
      publicarHistorial("radar", campos);
    }
  }

  // Aqui, mas adelante: SHT40/SGP40 en su propio temporizador independiente (20 s),
  // sin bloquear esta lectura del radar. No implementado a proposito en este archivo.
}
