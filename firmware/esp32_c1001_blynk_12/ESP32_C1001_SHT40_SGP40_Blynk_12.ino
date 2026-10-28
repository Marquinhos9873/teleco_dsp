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

// Lectura del C1001 por el monitor serie (y Blynk en modo SLEEP),
// segun el modo activo.
// Provisional: coincide con el delay(1000) de los ejemplos de DFRobot.
// Pendiente de ajustar con la medicion real de la tasa de UART (osciloscopio).
const unsigned long INTERVALO_RADAR = 1000UL;

// =====================================================
// 5. ESTADO DE LOS SENSORES
// =====================================================

// Ultimas mediciones validas del SHT40.
// Se usan para compensar temperatura y humedad en el SGP40.
// Empiezan en NAN (sin dato real todavia); nunca se usan antes
// de tiempo porque leerSGP40()/leerSGP40Raw() lo bloquean con
// la bandera lecturaSHTValida.

float temperaturaActual = NAN;
float humedadActual = NAN;

bool lecturaSHTValida = false;
bool sgp40Disponible = false;

// =====================================================
// 6. CONTROL DE MODO DEL C1001
// =====================================================

int modoActual = 0;
int modoSolicitado = 0;

bool cambioPendiente = false;

unsigned long tiempoSolicitud = 0;

// Indica si hu.begin() necesito mas de un intento en el arranque.
bool c1001NecesitoReintento = false;

// Ultimo codigo publicado en V11 (Error).
int codigoErrorActual = 0;

// =====================================================
// 7. REPORTAR ERROR (V11)
// =====================================================
// Esquema provisional, pendiente de confirmar con el equipo:
//   0 = sin error
//   1 = lectura invalida del SHT40
//   2 = SGP40 no disponible
//   3 = el C1001 necesito reintentos para inicializar
//   4 = solicitud de modo invalida por V10

void reportarError(int codigo)
{
  codigoErrorActual = codigo;

  if (Blynk.connected())
  {
    Blynk.virtualWrite(V11, codigoErrorActual);
  }
}

// =====================================================
// 8. LEER SHT40
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
    reportarError(1);
    return;
  }

  // Si el error activo era justamente el de SHT40, se limpia.
  if (codigoErrorActual == 1)
  {
    reportarError(0);
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

    Serial.println("SHT40 enviado a Blynk (V0, V1)");
  }
  else
  {
    Serial.println("Blynk desconectado");
  }

  Serial.println("-------------------------");
}

// =====================================================
// 9. LEER INDICE VOC DEL SGP40 (V2 = Air quality)
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

  Serial.print("Indice VOC (calidad de aire): ");
  Serial.println(indiceVOC);

  Serial.print("Temperatura usada: ");
  Serial.print(temperaturaActual, 2);
  Serial.println(" C");

  Serial.print("Humedad usada: ");
  Serial.print(humedadActual, 2);
  Serial.println(" %");

  if (Blynk.connected())
  {
    Blynk.virtualWrite(V2, indiceVOC);
    Serial.println("Calidad de aire enviada a Blynk (V2)");
  }
  else
  {
    Serial.println("Blynk desconectado: calidad de aire no publicada");
  }

  Serial.println("-------------------------");
}

// =====================================================
// 10. LEER SENAL BRUTA DEL SGP40 (solo diagnostico serial)
// =====================================================
// El valor crudo no forma parte del mapa de datastreams de Blynk
// (V8 ya no existe en el mapa oficial), asi que se queda solo
// como apoyo para depuracion local.

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

  Serial.print("SGP40 - Senal bruta (diagnostico, no va a Blynk): ");
  Serial.println(senalBruta);
}

// =====================================================
// 11. LEER C1001 SEGUN EL MODO ACTIVO
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

  Serial.println("Nota: el modo FALL aun no publica a Blynk (V7 pendiente)");
  Serial.println("------------------------------");
}

void leerModoSLEEP()
{
  Serial.println();
  Serial.println("----- C1001 (modo SLEEP) -----");

  // Cada dato se lee una sola vez (para no duplicar consultas UART)
  // y se reutiliza tanto para el serial como para Blynk.

  int enCama = hu.smSleepData(hu.eInOrNotInBed);
  int estadoSueno = hu.smSleepData(hu.eSleepState);
  int respiracion = hu.getBreatheValue();
  int latido = hu.getHeartRate();
  int alteracion = hu.smSleepData(hu.eSleepDisturbances);

  const char* textoEnCama;
  switch (enCama)
  {
    case 0:
      textoEnCama = "Fuera de cama";
      break;
    case 1:
      textoEnCama = "En cama";
      break;
    default:
      textoEnCama = "Error";
  }

  const char* textoEstadoSueno;
  switch (estadoSueno)
  {
    case 0:
      textoEstadoSueno = "Profundo";
      break;
    case 1:
      textoEstadoSueno = "Ligero";
      break;
    case 2:
      textoEstadoSueno = "Despierto";
      break;
    case 3:
      textoEstadoSueno = "Sin dato";
      break;
    default:
      textoEstadoSueno = "Error";
  }

  const char* textoAlteracion;
  switch (alteracion)
  {
    case 0:
      textoAlteracion = "Sueno<4h";
      break;
    case 1:
      textoAlteracion = "Sueno>12h";
      break;
    case 2:
      textoAlteracion = "Ausencia prolongada";
      break;
    case 3:
      textoAlteracion = "Ninguna";
      break;
    default:
      textoAlteracion = "Error";
  }

  Serial.print("Estado en cama: ");
  Serial.println(textoEnCama);

  Serial.print("Estado de sueno: ");
  Serial.println(textoEstadoSueno);

  Serial.print("Frecuencia respiratoria (getBreatheValue): ");
  Serial.println(respiracion);

  Serial.print("Frecuencia cardiaca (getHeartRate): ");
  Serial.println(latido);

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
  Serial.println(textoAlteracion);

  // ---------------------------------------------------
  // Publicar a Blynk: V3 (texto), V4 (heart rate), V5 (breathing rate)
  // ---------------------------------------------------

  if (Blynk.connected())
  {
    Blynk.virtualWrite(V4, latido);
    Blynk.virtualWrite(V5, respiracion);

    char textoSleep[150];
    snprintf(
      textoSleep,
      sizeof(textoSleep),
      "Cama:%s | Sueno:%s | FR:%d | FC:%d | Alt:%s",
      textoEnCama,
      textoEstadoSueno,
      respiracion,
      latido,
      textoAlteracion
    );
    Blynk.virtualWrite(V3, textoSleep);

    Serial.println("Datos de sueno enviados a Blynk (V3, V4, V5)");
  }
  else
  {
    Serial.println("Blynk desconectado: datos de sueno no publicados");
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
// 12. APLICAR MODO DEL RADAR
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
// 13. PROCESAR CAMBIO DE MODO
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
// 14. EVENTOS BLYNK (V6 Reboot, V10 Mode)
// =====================================================

BLYNK_WRITE(V6)
{
  int valor = param.asInt();

  if (valor == 1)
  {
    Serial.println();
    Serial.println("===== REINICIO SOLICITADO POR BLYNK (V6) =====");
    Serial.println("Reiniciando ESP32...");
    delay(200);
    ESP.restart();
  }
}

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
    reportarError(4);
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
// 15. SETUP
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
    c1001NecesitoReintento = true;
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

  // Asegurar que el boton de reinicio (V6) no quede en 1.
  Blynk.virtualWrite(V6, 0);

  // Publicar el estado de error inicial (ver esquema en la seccion 7).
  // Si el C1001 necesito reintentos, ese codigo prevalece sobre el del SGP40.
  int codigoErrorInicial = 0;
  if (!sgp40Disponible)
  {
    codigoErrorInicial = 2;
  }
  if (c1001NecesitoReintento)
  {
    codigoErrorInicial = 3;
  }
  reportarError(codigoErrorInicial);

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
  Serial.println("SGP40: senal bruta (diagnostico) cada 10 segundos");
  Serial.println("C1001: lectura cada 1 segundo, segun modo activo (FALL/SLEEP)");

  Serial.println();
  Serial.println("Sistema inicializado");
  Serial.println("V0  = Temperatura");
  Serial.println("V1  = Humedad");
  Serial.println("V2  = Calidad de aire (VOC)");
  Serial.println("V3  = Texto de estado de sueno (solo en SLEEP)");
  Serial.println("V4  = Heart rate aprox (solo en SLEEP)");
  Serial.println("V5  = Breathing rate aprox (solo en SLEEP)");
  Serial.println("V6  = Reboot remoto");
  Serial.println("V7  = Automatization_fall (pendiente, no se usa)");
  Serial.println("V10 = 0 FALL / 1 SLEEP");
  Serial.println("V11 = Codigo de error");
  Serial.println("====================================");
}

// =====================================================
// 16. LOOP PRINCIPAL
// =====================================================

void loop()
{
  Blynk.run();

  timer.run();

  procesarCambioModo();
}
