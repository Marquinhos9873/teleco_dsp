// ESP32 + C1001 (modo Sleep) -> Firebase Realtime Database, por REST puro.
//
// Aprovisionamiento de WiFi + identidad del propietario (NUEVO):
// Este equipo no tiene una red WiFi fija -- cada persona que lo use lo conecta
// a su propia red. Por eso usa un portal cautivo (libreria WiFiManager de
// tzapu -- instalar desde el Library Manager del Arduino IDE, buscar
// "WiFiManager by tzapu"): al arrancar sin credenciales guardadas, el ESP32
// abre su propio punto de acceso "C1001-Setup"; el usuario se conecta desde
// el celular y ahi mismo completa 3 campos: SSID, contrasena y SU CORREO DE
// CUENTA. Ese correo es la pieza que permite reclamar el equipo sin que el
// usuario tenga que ver ni escribir una direccion MAC (ver
// diagramas/mapa_flujo_datos.md, seccion 3.3, para la explicacion completa
// de la estrategia).
//
// Una vez conectado a la red real, el ESP32 lee su propia MAC
// (WiFi.macAddress(), disponible apenas esta en modo STA) y hace UN solo PUT
// a /dispositivos/{mac}/meta con ese correo como propietario_correo. A partir
// de ahi, TODO lo que publica (estado_actual/historial) queda bajo
// /dispositivos/{mac}/..., no en rutas globales.
//
// Parametros del radar: SOLO los que aparecen en firmware/reference/basics.ino
//   presencia          <- hu.smHumanData(hu.eHumanPresence)
//   movimiento         <- hu.smHumanData(hu.eHumanMovement)
//   movimiento_valor   <- hu.smHumanData(hu.eHumanMovingRange)
//   respiracion        <- hu.getBreatheValue()
//   frecuencia_cardiaca<- hu.getHeartRate()
//
// Pendiente, a proposito (no resolver aqui):
//   - Reglas reales de RTDB (hoy sigue en "modo de prueba", abiertas).
//   - Gesto para reconfigurar WiFi/correo en un equipo ya aprovisionado
//     (ej. boton fisico + wm.resetSettings()) -- no implementado todavia.
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

DFRobot_HumanDetection hu(&Serial2);

// ===================== 3. IDENTIDAD DEL EQUIPO (WiFiManager + Preferences) =====================
Preferences preferencias;
String correoPropietario = "";
String macEquipo = "";
String rutaDispositivo = "";  // "dispositivos/<MAC>", se arma en setup() una vez conocida la MAC

bool configuracionNueva = false;
void alGuardarConfigWifi() {
  configuracionNueva = true;
}

// ===================== 4. TEMPORIZADOR NO BLOQUEANTE =====================
const unsigned long INTERVALO_RADAR = 2000UL;  // lectura + PUT cada 2 s
const int LECTURAS_POR_HISTORIAL = 15;         // POST a historial cada 15*2s = 30 s

unsigned long ultimaLecturaRadar = 0;
int contadorLecturasRadar = 0;

// ===================== 5. PUBLICACION A FIREBASE =====================

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

// ===================== 6. SETUP =====================
void setup() {
  Serial.begin(115200);
  delay(1000);

  preferencias.begin("c1001", false);
  correoPropietario = preferencias.getString("correo", "");

  WiFiManager wm;

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
  // Inicializar el radar C1001
  // ---------------------------------------------------

  Serial2.begin(115200, SERIAL_8N1, RXD2, TXD2);

  Serial.println("Inicializando C1001...");
  while (hu.begin() != 0) {
    Serial.println("init error!!!");
    delay(1000);
  }
  Serial.println("Inicializacion correcta");

  Serial.println("Configurando modo Sleep (requerido para respiracion/FC)...");
  while (hu.configWorkMode(hu.eSleepMode) != 0) {
    Serial.println("error al configurar el modo!!!");
    delay(1000);
  }

  hu.configLEDLight(hu.eHPLed, 1);
  hu.sensorRet();  // obligatorio tras cambiar configuracion, igual que en basics.ino

  Serial.println("C1001 listo, publicando a Firebase cada 2 s");

  ultimaLecturaRadar = millis() - INTERVALO_RADAR;  // primera lectura casi inmediata
}

// ===================== 7. LOOP =====================
void loop() {
  unsigned long ahora = millis();

  if (ahora - ultimaLecturaRadar >= INTERVALO_RADAR) {
    ultimaLecturaRadar = ahora;
    contadorLecturasRadar++;

    int presencia = hu.smHumanData(hu.eHumanPresence);
    int movimiento = hu.smHumanData(hu.eHumanMovement);
    int movimientoValor = hu.smHumanData(hu.eHumanMovingRange);
    int respiracion = hu.getBreatheValue();
    int frecuenciaCardiaca = hu.getHeartRate();

    Serial.print("Lectura #");
    Serial.print(contadorLecturasRadar);
    Serial.print(" | presencia=");
    Serial.print(presencia);
    Serial.print(" movimiento=");
    Serial.print(movimiento);
    Serial.print(" rango=");
    Serial.print(movimientoValor);
    Serial.print(" resp=");
    Serial.print(respiracion);
    Serial.print(" fc=");
    Serial.println(frecuenciaCardiaca);

    String campos = "{\"presencia\": " + String(presencia) +
                     ", \"movimiento\": " + String(movimiento) +
                     ", \"movimiento_valor\": " + String(movimientoValor) +
                     ", \"respiracion\": " + String(respiracion) +
                     ", \"frecuencia_cardiaca\": " + String(frecuenciaCardiaca) +
                     ", \"timestamp\": {\".sv\": \"timestamp\"}}";

    publicarEstadoActual("radar", campos);

    if (contadorLecturasRadar % LECTURAS_POR_HISTORIAL == 0) {
      publicarHistorial("radar", campos);
    }
  }

  // Aqui, mas adelante: SHT40/SGP40 en su propio temporizador independiente (20 s),
  // sin bloquear esta lectura del radar. No implementado a proposito en este archivo.
}
