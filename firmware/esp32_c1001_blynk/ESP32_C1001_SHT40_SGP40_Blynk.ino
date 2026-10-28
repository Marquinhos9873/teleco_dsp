#define BLYNK_TEMPLATE_ID   "TMPL2m4Lc25fT"
#define BLYNK_TEMPLATE_NAME "SHT40 PRueba"
#include "secrets.h"

#include <WiFi.h>
#include <BlynkSimpleEsp32.h>
#include <DFRobot_HumanDetection.h>
#include <Wire.h>
#include <Adafruit_SHT4x.h>
#include <Adafruit_SGP40.h>
#include <math.h>

// =====================================================
// 1. CONFIGURACION WIFI
// =====================================================

char ssid[] = WIFI_SSID;
char pass[] = WIFI_PASS;

// =====================================================
// 2. PINES
// =====================================================

const int RXD2 = 16;
const int TXD2 = 17;

const int SDA_I2C = 21;
const int SCL_I2C = 22;

// =====================================================
// 3. OBJETOS
// =====================================================

DFRobot_HumanDetection hu(&Serial2);
Adafruit_SHT4x sht4;
Adafruit_SGP40 sgp40;

BlynkTimer timer;

// =====================================================
// 4. INTERVALOS
// =====================================================

const unsigned long INTERVALO_SHT40 = 100000UL;
const unsigned long INTERVALO_VOC = 1000UL;
const unsigned long INTERVALO_RAW = 10000UL;
const unsigned long TIEMPO_ESPERA = 20000UL;

// Lectura del C1001 por el monitor serie, segun el modo activo.
// Provisional: coincide con el delay(1000) de los ejemplos de DFRobot.
// Pendiente de ajustar con la medicion real de la tasa de UART (osciloscopio).
const unsigned long INTERVALO_RADAR = 1000UL;

// =====================================================
// 5. ESTADO DE LOS SENSORES
// =====================================================

// Ultimas mediciones validas del SHT40.
// Se utilizan para compensar temperatura y humedad
// en las mediciones del SGP40.

float temperaturaActual = 25.0;
float humedadActual = 50.0;

bool lecturaSHTValida = false;
bool sgp40Disponible = false;

// =====================================================
// 6. CONTROL DE MODO DEL C1001
// =====================================================

int modoActual = 0;
int modoSolicitado = 0;

bool cambioPendiente = false;

unsigned long tiempoSolicitud = 0;

// =====================================================
// 7. LEER SHT40
// =====================================================

void leerSHT40()
{
  sensors_event_t humedad;
  sensors_event_t temperatura;

  sht4.getEvent(&humedad, &temperatura);

  if (isnan(temperatura.temperature) ||
      isnan(humedad.relative_humidity))
  {
    Serial.println("ERROR: lectura no valida del SHT40");
    return;
  }

  // Actualizar los valores compartidos con el SGP40.
  temperaturaActual = temperatura.temperature;
  humedadActual = humedad.relative_humidity;
  lecturaSHTValida = true;

  Serial.println();
  Serial.println("----- LECTURA SHT40 -----");

  Serial.print("Temperatura: ");
  Serial.print(temperaturaActual, 2);
  Serial.println(" C");

  Serial.print("Humedad: ");
  Serial.print(humedadActual, 2);
  Serial.println(" %");

  if (Blynk.connected())
  {
    Blynk.virtualWrite(V0, temperaturaActual);
    Blynk.virtualWrite(V1, humedadActual);

    Serial.println("SHT40 enviado a Blynk");
  }
  else
  {
    Serial.println("Blynk desconectado");
  }

  Serial.println("-------------------------");
}

// =====================================================
// 8. LEER INDICE VOC DEL SGP40
// =====================================================

void leerSGP40()
{
  if (!sgp40Disponible)
  {
    return;
  }

  // No calcular el indice hasta disponer de una lectura
  // valida del SHT40 para compensar temperatura y humedad.

  if (!lecturaSHTValida)
  {
    Serial.println("SGP40: esperando lectura valida del SHT40");
    return;
  }

  int32_t indiceVOC = sgp40.measureVocIndex(
    temperaturaActual,
    humedadActual
  );

  Serial.println();
  Serial.println("----- LECTURA SGP40 -----");

  Serial.print("Indice VOC: ");
  Serial.println(indiceVOC);

  Serial.print("Temperatura usada: ");
  Serial.print(temperaturaActual, 2);
  Serial.println(" C");

  Serial.print("Humedad usada: ");
  Serial.print(humedadActual, 2);
  Serial.println(" %");

  if (Blynk.connected())
  {
    Blynk.virtualWrite(V9, indiceVOC);
    Serial.println("Indice VOC enviado a Blynk");
  }
  else
  {
    Serial.println("Blynk desconectado: VOC no publicado");
  }

  Serial.println("-------------------------");
}

// =====================================================
// 9. LEER SENAL BRUTA DEL SGP40
// =====================================================

void leerSGP40Raw()
{
  if (!sgp40Disponible || !lecturaSHTValida)
  {
    return;
  }

  uint16_t senalBruta = sgp40.measureRaw(
    temperaturaActual,
    humedadActual
  );

  Serial.print("SGP40 - Senal bruta: ");
  Serial.println(senalBruta);

  if (Blynk.connected())
  {
    Blynk.virtualWrite(V8, senalBruta);
  }
}

// =====================================================
// 9B. LEER C1001 SEGUN EL MODO ACTIVO (solo monitor serie)
// =====================================================

void leerModoFALL()
{
  Serial.println();
  Serial.println("----- C1001 (modo FALL) -----");

  Serial.print("Presencia: ");
  switch (hu.smHumanData(hu.eHumanPresence))
  {
    case 0:
      Serial.println("Nadie presente");
      break;
    case 1:
      Serial.println("Persona presente");
      break;
    default:
      Serial.println("Error de lectura");
  }

  Serial.print("Movimiento: ");
  switch (hu.smHumanData(hu.eHumanMovement))
  {
    case 0:
      Serial.println("Ninguno");
      break;
    case 1:
      Serial.println("Quieto");
      break;
    case 2:
      Serial.println("Activo");
      break;
    default:
      Serial.println("Error de lectura");
  }

  Serial.print("Parametro de movimiento corporal: ");
  Serial.println(hu.smHumanData(hu.eHumanMovingRange));

  Serial.print("Estado de caida: ");
  switch (hu.getFallData(hu.eFallState))
  {
    case 0:
      Serial.println("Sin caida");
      break;
    case 1:
      Serial.println("CAIDA DETECTADA");
      break;
    default:
      Serial.println("Error de lectura");
  }

  Serial.print("Permanencia estatica: ");
  switch (hu.getFallData(hu.estaticResidencyState))
  {
    case 0:
      Serial.println("Sin permanencia estatica");
      break;
    case 1:
      Serial.println("Permanencia estatica presente");
      break;
    default:
      Serial.println("Error de lectura");
  }

  Serial.println("------------------------------");
}

void leerModoSLEEP()
{
  Serial.println();
  Serial.println("----- C1001 (modo SLEEP) -----");

  Serial.print("Estado en cama: ");
  switch (hu.smSleepData(hu.eInOrNotInBed))
  {
    case 0:
      Serial.println("Fuera de la cama");
      break;
    case 1:
      Serial.println("En la cama");
      break;
    default:
      Serial.println("Error de lectura");
  }

  Serial.print("Estado de sueno: ");
  switch (hu.smSleepData(hu.eSleepState))
  {
    case 0:
      Serial.println("Sueno profundo");
      break;
    case 1:
      Serial.println("Sueno ligero");
      break;
    case 2:
      Serial.println("Despierto");
      break;
    case 3:
      Serial.println("Sin dato");
      break;
    default:
      Serial.println("Error de lectura");
  }

  Serial.print("Frecuencia respiratoria (getBreatheValue): ");
  Serial.println(hu.getBreatheValue());

  Serial.print("Frecuencia cardiaca (getHeartRate): ");
  Serial.println(hu.getHeartRate());

  sSleepComposite estadoCompuesto = hu.getSleepComposite();

  Serial.println("Estado compuesto de sueno:");

  Serial.print("  Presencia: ");
  Serial.println(estadoCompuesto.presence == 1 ? "Si" : "No");

  Serial.print("  Respiracion promedio: ");
  Serial.println(estadoCompuesto.averageRespiration);

  Serial.print("  Latido promedio: ");
  Serial.println(estadoCompuesto.averageHeartbeat);

  Serial.print("  Numero de giros: ");
  Serial.println(estadoCompuesto.turnoverNumber);

  Serial.print("  Movimiento corporal grande (%): ");
  Serial.println(estadoCompuesto.largeBodyMove);

  Serial.print("  Movimiento corporal leve (%): ");
  Serial.println(estadoCompuesto.minorBodyMove);

  Serial.print("  Eventos de apnea: ");
  Serial.println(estadoCompuesto.apneaEvents);

  Serial.print("Alteraciones del sueno: ");
  switch (hu.smSleepData(hu.eSleepDisturbances))
  {
    case 0:
      Serial.println("Duracion de sueno menor a 4 horas");
      break;
    case 1:
      Serial.println("Duracion de sueno mayor a 12 horas");
      break;
    case 2:
      Serial.println("Ausencia prolongada de la persona");
      break;
    case 3:
      Serial.println("Ninguna");
      break;
    default:
      Serial.println("Error de lectura");
  }

  Serial.println("-------------------------------");
}

void leerRadar()
{
  if (modoActual == 0)
  {
    leerModoFALL();
  }
  else
  {
    leerModoSLEEP();
  }
}

// =====================================================
// 10. APLICAR MODO DEL RADAR
// =====================================================

void aplicarModo(int nuevoModo)
{
  Serial.println();
  Serial.println("Aplicando nuevo modo al C1001...");

  if (nuevoModo == 0)
  {
    Serial.println("Configurando FALL MODE");
    hu.configWorkMode(hu.eFallingMode);
  }
  else if (nuevoModo == 1)
  {
    Serial.println("Configurando SLEEP MODE");
    hu.configWorkMode(hu.eSleepMode);
  }
  else
  {
    Serial.println("ERROR: modo solicitado no valido");
    return;
  }

  modoActual = nuevoModo;
  cambioPendiente = false;

  Serial.print("Modo aplicado: ");

  if (modoActual == 0)
  {
    Serial.println("FALL");
  }
  else
  {
    Serial.println("SLEEP");
  }

  Serial.println("-------------------------");
}

// =====================================================
// 11. PROCESAR CAMBIO DE MODO
// =====================================================

void procesarCambioModo()
{
  if (!cambioPendiente)
  {
    return;
  }

  if (millis() - tiempoSolicitud >= TIEMPO_ESPERA)
  {
    Serial.println("Finalizaron los 20 segundos de espera");
    aplicarModo(modoSolicitado);
  }
}

// =====================================================
// 12. EVENTO BLYNK V10
// =====================================================

BLYNK_WRITE(V10)
{
  int nuevoModo = param.asInt();

  Serial.println();
  Serial.println("===== SOLICITUD DE MODO =====");

  Serial.print("Valor recibido en V10: ");
  Serial.println(nuevoModo);

  if (nuevoModo != 0 && nuevoModo != 1)
  {
    Serial.println("ERROR: V10 debe ser 0 o 1");
    return;
  }

  if (nuevoModo == modoActual)
  {
    if (cambioPendiente)
    {
      Serial.println("Se cancela la solicitud pendiente");
    }
    else
    {
      Serial.println("El radar ya esta en ese modo");
    }

    modoSolicitado = modoActual;
    cambioPendiente = false;
    return;
  }

  modoSolicitado = nuevoModo;
  tiempoSolicitud = millis();
  cambioPendiente = true;

  Serial.print("Modo solicitado: ");
  Serial.println(modoSolicitado == 0 ? "FALL" : "SLEEP");

  Serial.println("Esperando 20 segundos antes del cambio");
}

// =====================================================
// 13. SETUP
// =====================================================

void setup()
{
  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("====================================");
  Serial.println(" ESP32 - C1001 - SHT40 - SGP40");
  Serial.println("====================================");

  // ---------------------------------------------------
  // Inicializar bus I2C
  // ---------------------------------------------------

  Wire.begin(SDA_I2C, SCL_I2C);

  // ---------------------------------------------------
  // Inicializar SHT40
  // ---------------------------------------------------

  Serial.println("Inicializando SHT40...");

  if (!sht4.begin())
  {
    Serial.println("ERROR: SHT40 no encontrado");

    while (true)
    {
      delay(1000);
    }
  }

  sht4.setPrecision(SHT4X_HIGH_PRECISION);
  sht4.setHeater(SHT4X_NO_HEATER);

  Serial.println("SHT40 detectado");
  Serial.println("Alta precision: activada");
  Serial.println("Calentador: desactivado");

  // ---------------------------------------------------
  // Inicializar SGP40 en el mismo bus I2C
  // ---------------------------------------------------

  Serial.println("Inicializando SGP40...");

  if (sgp40.begin(&Wire))
  {
    sgp40Disponible = true;
    Serial.println("SGP40 detectado correctamente");
  }
  else
  {
    sgp40Disponible = false;

    Serial.println("ADVERTENCIA: SGP40 no detectado");
    Serial.println("El sistema continuara sin el SGP40");
    Serial.println("Revisar alimentacion, SDA y SCL");
  }

  // ---------------------------------------------------
  // Inicializar UART2 y C1001
  // ---------------------------------------------------

  Serial.println("Inicializando UART2...");

  Serial2.begin(115200, SERIAL_8N1, RXD2, TXD2);

  Serial.println("Inicializando C1001...");
  Serial.println("Esperar el proceso de calibracion");

  while (hu.begin() != 0)
  {
    Serial.println("ERROR: no se pudo inicializar el C1001, reintentando...");
    delay(1000);
  }

  Serial.println("Inicializacion del C1001 finalizada");
  Serial.println("Configurando modo inicial FALL");

  hu.configWorkMode(hu.eFallingMode);

  modoActual = 0;
  modoSolicitado = 0;
  cambioPendiente = false;

  Serial.println("Modo inicial configurado");

  // ---------------------------------------------------
  // Conectar con Blynk
  // ---------------------------------------------------

  Serial.println("Conectando a Blynk...");

  Blynk.begin(
    BLYNK_AUTH_TOKEN,
    ssid,
    pass
  );

  Serial.println("Conexion con Blynk establecida");

  // Recuperar el selector de modo guardado en Blynk.
  Blynk.syncVirtual(V10);

  // ---------------------------------------------------
  // Obtener una primera lectura del SHT40
  // ---------------------------------------------------

  // Permite que el SGP40 disponga de temperatura y
  // humedad reales desde el comienzo de sus mediciones.

  leerSHT40();

  // ---------------------------------------------------
  // Configurar temporizadores
  // ---------------------------------------------------

  timer.setInterval(
    INTERVALO_SHT40,
    leerSHT40
  );

  timer.setInterval(
    INTERVALO_VOC,
    leerSGP40
  );

  timer.setInterval(
    INTERVALO_RAW,
    leerSGP40Raw
  );

  timer.setInterval(
    INTERVALO_RADAR,
    leerRadar
  );

  Serial.println("SHT40: lectura cada 100 segundos");
  Serial.println("SGP40: indice VOC cada 1 segundo");
  Serial.println("SGP40: senal bruta cada 10 segundos");
  Serial.println("C1001: lectura cada 1 segundo, segun modo activo (FALL/SLEEP)");

  Serial.println();
  Serial.println("Sistema inicializado");
  Serial.println("V8 = senal bruta SGP40");
  Serial.println("V9 = indice VOC");
  Serial.println("V10 = 0 FALL / 1 SLEEP");
  Serial.println("Datos del C1001: visibles solo en el monitor serie por ahora");
  Serial.println("====================================");
}

// =====================================================
// 14. LOOP PRINCIPAL
// =====================================================

void loop()
{
  Blynk.run();

  timer.run();

  procesarCambioModo();
}
