#include <WiFi.h>
#include <HTTPClient.h>
#include <time.h>

#include "LoRaWan_APP.h"

// WIFI + Firebase

#define WIFI_SSID     "SSID"
#define WIFI_PASSWORD "PASS"

#define FIREBASE_URL \
"realtimedatabaselink"


// Slovenija, avtomatski CET / CEST
#define TZ_INFO "CET-1CEST,M3.5.0,M10.5.0/3"

// LORA

#define RF_FREQUENCY                868000000
#define TX_OUTPUT_POWER             10

#define LORA_BANDWIDTH              0
#define LORA_SPREADING_FACTOR       7
#define LORA_CODINGRATE             1

#define LORA_PREAMBLE_LENGTH        8
#define LORA_SYMBOL_TIMEOUT         0

#define LORA_FIX_LENGTH_PAYLOAD_ON  false
#define LORA_IQ_INVERSION_ON        false

// INTERVALI

#define INTERVAL_10_MIN   (10UL * 60UL * 1000UL)
#define INTERVAL_20_MIN   (20UL * 60UL * 1000UL)
#define INTERVAL_30_MIN   (30UL * 60UL * 1000UL)
#define INTERVAL_60_MIN   (60UL * 60UL * 1000UL)


// LORA 

static RadioEvents_t RadioEvents;

// MERITVE

// pH in turbidity se povprecita

double turbiditySum = 0.0;
double pHSum = 0.0;

unsigned long measurementCount = 0;

// Temperatura se NE povpreci
// Vedno hranimo zadnjo temperaturo

float lastTemperature = 0.0;
bool lastTemperatureAvailable = false;

int16_t lastRSSI = 0;
int8_t lastSNR = 0;

float previousUploadedTemperature = 0.0;

bool previousUploadedTemperatureAvailable = false;

unsigned long previousFirebaseTime = 0;

unsigned long firebaseInterval =
    INTERVAL_10_MIN;

unsigned long lastFirebaseUpload = 0;

//preveri first upload

bool firstUploadDone = false;

void connectWiFi()
{
  if (WiFi.status() == WL_CONNECTED)
  {
    return;
  }

  Serial.println();
  Serial.println("Povezovanje na WiFi...");

  WiFi.mode(WIFI_STA);

  WiFi.disconnect();

  delay(500);

  WiFi.begin(
    WIFI_SSID,
    WIFI_PASSWORD
  );

  Serial.print("Povezovanje");

  while (WiFi.status() != WL_CONNECTED)
  {
    Serial.print(".");
    delay(500);
  }

  Serial.println();
  Serial.println("WiFi povezan!");

  Serial.print("IP: ");
  Serial.println(WiFi.localIP());
}

void initTime()
{
  Serial.println();
  Serial.println("Sinhronizacija NTP ure...");

  configTzTime(
    TZ_INFO,
    "pool.ntp.org",
    "time.google.com"
  );

  struct tm timeinfo;

  if (getLocalTime(&timeinfo, 10000))
  {
    Serial.print("Trenutni cas: ");

    Serial.println(
      &timeinfo,
      "%d.%m.%Y %H:%M:%S"
    );
  }

  else
  {
    Serial.println(
      "NTP ure ni bilo mogoce pridobiti."
    );
  }
}

bool isNight()
{
  struct tm timeinfo;

  if (!getLocalTime(&timeinfo))
  {
    Serial.println(
      "Ura ni na voljo -> DAY mode."
    );

    return false;
  }

  int hour =
      timeinfo.tm_hour;

  Serial.print("Trenutni cas: ");

  Serial.println(
    &timeinfo,
    "%H:%M:%S"
  );

  // NOC:
  // 20:00 - 05:59

  if (
      hour >= 20 ||
      hour < 6
     )
  {
    return true;
  }

  return false;
}

// ADAPTIVNI INTERVAL

unsigned long calculateNextInterval(
  float currentTemperature,
  float previousTemperature,
  float elapsedHours)
{
  if (elapsedHours <= 0.0)
  {
    return INTERVAL_10_MIN;
  }

  float temperatureDifference =
      currentTemperature -
      previousTemperature;

  float temperatureRate =
      abs(temperatureDifference) /
      elapsedHours;

  Serial.println();
  Serial.println(
    "----- ADAPTIVNI ALGORITEM -----"
  );

  Serial.print(
    "Prejsnja temperatura: "
  );

  Serial.print(
    previousTemperature,
    3
  );

  Serial.println(" C");

  Serial.print(
    "Trenutna temperatura: "
  );

  Serial.print(
    currentTemperature,
    3
  );

  Serial.println(" C");

  Serial.print(
    "Razlika: "
  );

  Serial.print(
    temperatureDifference,
    4
  );

  Serial.println(" C");

  Serial.print(
    "|dT/dh|: "
  );

  Serial.print(
    temperatureRate,
    4
  );

  Serial.println(" C/h");

  // PRAGI

  if (temperatureRate >= 1.0)
  {
    Serial.println(
      "Naslednji interval: 10 min"
    );

    return INTERVAL_10_MIN;
  }

  else if (temperatureRate >= 0.4)
  {
    Serial.println(
      "Naslednji interval: 20 min"
    );

    return INTERVAL_20_MIN;
  }

  else if (temperatureRate >= 0.15)
  {
    Serial.println(
      "Naslednji interval: 30 min"
    );

    return INTERVAL_30_MIN;
  }

  else
  {
    Serial.println(
      "|dT/dh| < 0.15"
    );

    if (isNight())
    {
      Serial.println(
        "NOC -> naslednji interval: 60 min"
      );

      return INTERVAL_60_MIN;
    }

    else
    {
      Serial.println(
        "DAN -> naslednji interval: 30 min"
      );

      return INTERVAL_30_MIN;
    }
  }
}

// =====================================================
// FIREBASE
// =====================================================

bool sendToFirebase(
  float temperature,
  float avgTurbidity,
  float avgPH,
  unsigned long count,
  unsigned long nextInterval)
{
  connectWiFi();

  if (WiFi.status() != WL_CONNECTED)
  {
    Serial.println(
      "Ni WiFi povezave."
    );

    return false;
  }

  HTTPClient http;

  String url =
      String(FIREBASE_URL) +
      "/measurements.json";

  http.begin(url);

  http.addHeader(
    "Content-Type",
    "application/json"
  );

  // ==================================================
  // DATUM IN CAS
  // ==================================================

  struct tm timeinfo;

  char dateTimeBuffer[32];

  if (getLocalTime(&timeinfo))
  {
    strftime(
      dateTimeBuffer,
      sizeof(dateTimeBuffer),
      "%d.%m.%Y %H:%M:%S",
      &timeinfo
    );
  }

  else
  {
    strcpy(
      dateTimeBuffer,
      "unknown"
    );
  }

  // ==================================================
  // JSON
  // ==================================================

  String json = "{";

  // Zadnja temperatura
  json += "\"temperature\":";
  json += String(
    temperature,
    2
  );
  json += ",";

  // Povprecna turbidity
  json += "\"turbidity\":";
  json += String(
    avgTurbidity,
    3
  );
  json += ",";

  // Povprecni pH
  json += "\"pH\":";
  json += String(
    avgPH,
    2
  );
  json += ",";

  json += "\"samples\":";
  json += String(count);
  json += ",";

  json += "\"rssi\":";
  json += String(lastRSSI);
  json += ",";

  json += "\"snr\":";
  json += String(lastSNR);
  json += ",";

  json += "\"next_interval_min\":";
  json += String(
    nextInterval /
    60000UL
  );
  json += ",";

  // Berljiv datum in cas
  json += "\"datetime\":\"";
  json += dateTimeBuffer;
  json += "\"";

  json += "}";

  // ==================================================
  // POST
  // ==================================================

  Serial.println();
  Serial.println(
    "----- FIREBASE -----"
  );

  Serial.print(
    "JSON: "
  );

  Serial.println(json);

  int httpCode =
      http.POST(json);

  Serial.print(
    "HTTP code: "
  );

  Serial.println(httpCode);

  if (httpCode > 0)
  {
    String response =
        http.getString();

    Serial.print(
      "Response: "
    );

    Serial.println(response);
  }

  http.end();

  if (
      httpCode >= 200 &&
      httpCode < 300
     )
  {
    Serial.println(
      "Firebase upload uspesen."
    );

    return true;
  }

  Serial.println(
    "Firebase upload ni uspel."
  );

  return false;
}

// =====================================================
// FIREBASE UPLOAD
// =====================================================

void performFirebaseUpload()
{
  if (
      measurementCount == 0 ||
      !lastTemperatureAvailable
     )
  {
    Serial.println(
      "Ni meritev za upload."
    );

    return;
  }

  unsigned long currentTime =
      millis();

  // ==================================================
  // POVPRECJE PH + TURBIDITY
  // ==================================================

  float avgTurbidity =
      turbiditySum /
      measurementCount;

  float avgPH =
      pHSum /
      measurementCount;

  Serial.println();
  Serial.println(
    "==============================="
  );

  Serial.println(
    "FIREBASE UPLOAD"
  );

  Serial.println(
    "==============================="
  );

  Serial.print(
    "Zadnja temperatura: "
  );

  Serial.print(
    lastTemperature,
    3
  );

  Serial.println(" C");

  Serial.print(
    "Povprecna turbidity: "
  );

  Serial.print(
    avgTurbidity,
    3
  );

  Serial.println(" V");

  Serial.print(
    "Povprecni pH: "
  );

  Serial.println(
    avgPH,
    2
  );

  Serial.print(
    "Stevilo vzorcev: "
  );

  Serial.println(
    measurementCount
  );

  // ==================================================
  // NASLEDNJI INTERVAL
  // ==================================================

  unsigned long nextInterval =
      INTERVAL_10_MIN;

  if (
    previousUploadedTemperatureAvailable
  )
  {
    unsigned long elapsedMs =
        currentTime -
        previousFirebaseTime;

    float elapsedHours =
        elapsedMs /
        3600000.0;

    nextInterval =
        calculateNextInterval(
          lastTemperature,
          previousUploadedTemperature,
          elapsedHours
        );
  }

  else
  {
    Serial.println();
    Serial.println(
      "Prvi upload."
    );

    Serial.println(
      "Ni prejsnje temperature."
    );

    Serial.println(
      "Naslednji interval: 10 min"
    );

    nextInterval =
        INTERVAL_10_MIN;
  }

  // ==================================================
  // FIREBASE SEND
  // ==================================================

  sendToFirebase(
    lastTemperature,
    avgTurbidity,
    avgPH,
    measurementCount,
    nextInterval
  );

  // ==================================================
  // SHRANI TEMPERATURO
  // ==================================================

  previousUploadedTemperature =
      lastTemperature;

  previousUploadedTemperatureAvailable =
      true;

  previousFirebaseTime =
      currentTime;

  // ==================================================
  // NOV INTERVAL
  // ==================================================

  firebaseInterval =
      nextInterval;

  lastFirebaseUpload =
      millis();

  // ==================================================
  // RESET POVPRECJA
  // ==================================================

  turbiditySum =
      0.0;

  pHSum =
      0.0;

  measurementCount =
      0;

  firstUploadDone =
      true;

  Serial.println();
  Serial.println(
    "pH in turbidity povprecje resetirano."
  );

  Serial.print(
    "Naslednji upload cez: "
  );

  Serial.print(
    firebaseInterval /
    60000UL
  );

  Serial.println(
    " min"
  );
}

// =====================================================
// LORA RX CALLBACK
// =====================================================

void OnRxDone(
  uint8_t *payload,
  uint16_t size,
  int16_t rssi,
  int8_t snr)
{
  char packet[100];

  if (size >= sizeof(packet))
  {
    size =
        sizeof(packet) - 1;
  }

  memcpy(
    packet,
    payload,
    size
  );

  packet[size] =
      '\0';

  Serial.println();
  Serial.println(
    "==============================="
  );

  Serial.println(
    "LORA PAKET PREJET"
  );

  Serial.println(
    "==============================="
  );

  Serial.print(
    "Paket: "
  );

  Serial.println(packet);

  Serial.print(
    "RSSI: "
  );

  Serial.print(rssi);

  Serial.println(
    " dBm"
  );

  Serial.print(
    "SNR: "
  );

  Serial.print(snr);

  Serial.println(
    " dB"
  );

  // PARSING

  float temperature;
  float turbidity;
  float pH;

  int parsed =
      sscanf(
        packet,
        "%f,%f,%f",
        &temperature,
        &turbidity,
        &pH
      );

  if (parsed == 3)
  {
    Serial.println(
      "Veljaven paket."
    );

    Serial.print(
      "Temperatura: "
    );

    Serial.println(
      temperature,
      2
    );

    Serial.print(
      "Turbidity: "
    );

    Serial.println(
      turbidity,
      3
    );

    Serial.print(
      "pH: "
    );

    Serial.println(
      pH,
      2
    );

    // ZADNJA TEMPERATURA

    lastTemperature =
        temperature;

    lastTemperatureAvailable =
        true;

    // POVPRECJE PH + TURBIDITY

    turbiditySum +=
        turbidity;

    pHSum +=
        pH;

    measurementCount++;

    lastRSSI =
        rssi;

    lastSNR =
        snr;

    Serial.print(
      "Vzorcev: "
    );

    Serial.println(
      measurementCount
    );

    Serial.print(
      "Avg turbidity: "
    );

    Serial.println(
      turbiditySum /
      measurementCount,
      3
    );

    Serial.print(
      "Avg pH: "
    );

    Serial.println(
      pHSum /
      measurementCount,
      2
    );

    // PRVI PAKET -> TAKOJ FIREBASE

    if (!firstUploadDone)
    {
      Serial.println();
      Serial.println(
        "PRVI LORA PAKET -> TAKOJ FIREBASE"
      );

      performFirebaseUpload();
    }
  }

  else
  {
    Serial.println(
      "NAPAKA: neveljaven paket."
    );
  }

  Radio.Rx(0);
}

void OnRxTimeout()
{
  Radio.Rx(0);
}

void OnRxError()
{
  Serial.println(
    "LoRa RX error."
  );

  Radio.Rx(0);
}

void setup()
{
  Serial.begin(115200);

  delay(1000);

  Serial.println();
  Serial.println(
    "Inicializacija"
  );

  Mcu.begin(
    HELTEC_BOARD,
    SLOW_CLK_TPYE
  );

  connectWiFi();

  initTime();

  // LORA

  RadioEvents.RxDone =
      OnRxDone;

  RadioEvents.RxTimeout =
      OnRxTimeout;

  RadioEvents.RxError =
      OnRxError;

  Radio.Init(
    &RadioEvents
  );

  Radio.SetChannel(
    RF_FREQUENCY
  );

  Radio.SetRxConfig(
    MODEM_LORA,
    LORA_BANDWIDTH,
    LORA_SPREADING_FACTOR,
    LORA_CODINGRATE,
    0,
    LORA_PREAMBLE_LENGTH,
    LORA_SYMBOL_TIMEOUT,
    LORA_FIX_LENGTH_PAYLOAD_ON,
    0,
    true,
    0,
    0,
    LORA_IQ_INVERSION_ON,
    true
  );

  Serial.println(
    "LoRa inicializiran."
  );

  Serial.println(
    "Cakam na prvi LoRa paket..."
  );

  Radio.Rx(0);
}

void loop()
{
  Radio.IrqProcess();

  // Prvi upload naredi OnRxDone()
  if (!firstUploadDone)
  {
    return;
  }


  // NASLEDNJI ADAPTIVNI UPLOAD

  if (
      millis() -
      lastFirebaseUpload
      >= firebaseInterval
     )
  {
    if (
        measurementCount > 0 &&
        lastTemperatureAvailable
       )
    {
      performFirebaseUpload();
    }

    else
    {
      Serial.println();
      Serial.println(
        "Cas za upload, vendar ni novih LoRa meritev."
      );

      lastFirebaseUpload =
          millis();

      Radio.Rx(0);
    }
  }
}