// ESP32 + C1001 (modo Sleep) -> Firebase Realtime Database, por REST puro.
//
// Usa SOLO los parametros que aparecen en firmware/reference/basics.ino:
//   presencia          <- hu.smHumanData(hu.eHumanPresence)
//   movimiento         <- hu.smHumanData(hu.eHumanMovement)
//   movimiento_valor   <- hu.smHumanData(hu.eHumanMovingRange)
//   respiracion        <- hu.getBreatheValue()
//   frecuencia_cardiaca<- hu.getHeartRate()
// No se usan los parametros extra de fall.ino (altura, sensibilidad de caida, etc.)
// ni los extra del modo Sleep del sketch de Blynk (sSleepComposite, eInOrNotInBed...).
//
// Transporte y patron de publicacion: los mismos ya validados en Wokwi
// (ver CONTEXTO_FIREBASE_WEBHOST_v2.md, seccion 2):
//   PUT  /estado_actual/radar  -> siempre, sobrescribe, para el dashboard en vivo.
//   POST /historial/radar      -> 1 de cada N lecturas, acumula con timestamp de servidor.
// Reintento corto (400 ms) si la primera llamada HTTP falla (codigo < 0),
// sin usar delay() bloqueante en el loop principal.
//
// Pendiente, a proposito (no resolver aqui):
//   - Reglas de Firebase siguen en "modo de prueba" (abiertas); falta el secreto
//     de base de datos (?auth=) antes de cualquier despliegue real.
//   - SHT40/SGP40 y namespacing por MAC: se integran despues, sin tocar este archivo.

#include "secrets.h"

#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include "DFRobot_HumanDetection.h"

// ===================== 1. WIFI =====================
const char* ssid = WIFI_SSID;
const char* password = WIFI_PASS;

// ===================== 2. FIREBASE =====================
// databaseURL del proyecto de prueba iot-monitoring-c1001-fb-test.
// No es secreto (ver nota en CONTEXTO_FIREBASE_WEBHOST_v2.md, seccion 8);
// lo que protege los datos son las reglas de la base, no esta URL.
const char* firebaseHost = "https://iot-monitoring-c1001-fb-test-default-rtdb.firebaseio.com";

// ===================== 3. PINES / RADAR =====================
// Mismo UART ya validado para ESP32 real en firmware/esp32_c1001_blynk_12.
const int RXD2 = 16;
const int TXD2 = 17;

DFRobot_HumanDetection hu(&Serial2);

// ===================== 4. TEMPORIZADOR NO BLOQUEANTE =====================
const unsigned long INTERVALO_RADAR = 2000UL;  // lectura + PUT cada 2 s
const int LECTURAS_POR_HISTORIAL = 15;         // POST a historial cada 15*2s = 30 s

unsigned long ultimaLecturaRadar = 0;
int contadorLecturasRadar = 0;

// ===================== 5. PUBLICACION A FIREBASE =====================

bool wifiListo() {
  return WiFi.status() == WL_CONNECTED;
}

void publicarEstadoActual(const String& ruta, const String& camposJson) {
  if (!wifiListo()) {
    Serial.println("WiFi caido: estado_actual no publicado");
    return;
  }

  WiFiClientSecure client;
  client.setInsecure();  // TEMPORAL, pendiente de endurecer antes de la demo final

  HTTPClient http;
  String url = String(firebaseHost) + "/estado_actual/" + ruta + ".json";
  http.begin(client, url);
  http.addHeader("Content-Type", "application/json");

  int codigo = http.PUT(camposJson);
  if (codigo < 0) {
    http.end();
    delay(400);  // espera corta y acotada dentro de esta sola llamada, no congela el loop
    http.begin(client, url);
    http.addHeader("Content-Type", "application/json");
    codigo = http.PUT(camposJson);
  }

  Serial.print("PUT estado_actual/");
  Serial.print(ruta);
  Serial.print(" -> ");
  Serial.println(codigo);
  http.end();
}

void publicarHistorial(const String& ruta, const String& camposJson) {
  if (!wifiListo()) {
    Serial.println("WiFi caido: historial no publicado");
    return;
  }

  WiFiClientSecure client;
  client.setInsecure();  // TEMPORAL, pendiente de endurecer antes de la demo final

  HTTPClient http;
  String url = String(firebaseHost) + "/historial/" + ruta + ".json";
  http.begin(client, url);
  http.addHeader("Content-Type", "application/json");

  int codigo = http.POST(camposJson);
  if (codigo < 0) {
    http.end();
    delay(400);
    http.begin(client, url);
    http.addHeader("Content-Type", "application/json");
    codigo = http.POST(camposJson);
  }

  Serial.print("POST historial/");
  Serial.print(ruta);
  Serial.print(" -> ");
  Serial.println(codigo);
  http.end();
}

// ===================== 6. SETUP =====================
void setup() {
  Serial.begin(115200);
  delay(1000);

  WiFi.begin(ssid, password);
  Serial.print("Conectando a WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("Conectado, IP: ");
  Serial.println(WiFi.localIP());

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
