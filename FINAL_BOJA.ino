#include <Wire.h>
#include <Adafruit_ADS1X15.h>

#include <OneWire.h>
#include <DallasTemperature.h>

#include "LoRaWan_APP.h"

// PINI

#define SDA_PIN 4
#define SCL_PIN 3

#define ONE_WIRE_BUS 6

#define PH_ADC_CHANNEL 0
#define TURBIDITY_ADC_CHANNEL 1

 
// pH KALIBRACIJA podatki
 
float slope = 0.0132887;
float intercept = -19.0982;

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

// SENZORJI
 
Adafruit_ADS1115 ads;

OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature waterTempSensor(&oneWire);

// Ali ADS1115 deluje
bool adsAvailable = false;

// LORA

static RadioEvents_t RadioEvents;

bool txDone = false;

void OnTxDone()
{
  Serial.println("LoRa paket uspesno poslan.");
  txDone = true;
}

void OnTxTimeout()
{
  Serial.println("LoRa TX timeout.");
  txDone = true;
}

// TEMPERATURA

float readWaterTemperature()
{
  waterTempSensor.requestTemperatures();

  return waterTempSensor.getTempCByIndex(0);
}
 
// pH

float readPH()
{
  int16_t adc =
      ads.readADC_SingleEnded(PH_ADC_CHANNEL);

  float voltage =
      adc * 0.125;

  float pH =
      slope * voltage + intercept;

  return pH;
}

// TURBIDITY

float readTurbidityVoltage()
{
  int16_t adc =
      ads.readADC_SingleEnded(TURBIDITY_ADC_CHANNEL);

  float adcVoltage =
      (adc * 0.125) / 1000.0;

  float sensorVoltage =
      adcVoltage * 1.5;

  return sensorVoltage;
}

// LORA SEND - VSI SENZORJI

void sendLoRaFull(
  float temperature,
  float turbidity,
  float pH)
{
  char packet[64];

  snprintf(
    packet,
    sizeof(packet),
    "%.2f,%.3f,%.2f",
    temperature,
    turbidity,
    pH
  );

  Serial.println();
  Serial.println("----- LORA TX -----");

  Serial.print("Paket: ");
  Serial.println(packet);

  txDone = false;

  Radio.Send(
    (uint8_t*)packet,
    strlen(packet)
  );

  unsigned long start =
      millis();

  while (!txDone)
  {
    Radio.IrqProcess();

    if (millis() - start > 10000)
    {
      Serial.println(
        "Napaka: LoRa TX predolgo traja."
      );

      break;
    }
  }
}

// LORA SEND - SAMO TEMPERATURA

void sendLoRaTemperature(
  float temperature)
{
  char packet[32];

  snprintf(
    packet,
    sizeof(packet),
    "%.2f",
    temperature
  );

  Serial.println();
  Serial.println(
    "----- LORA TX - SAMO TEMP -----"
  );

  Serial.print("Paket: ");
  Serial.println(packet);

  txDone = false;

  Radio.Send(
    (uint8_t*)packet,
    strlen(packet)
  );

  unsigned long start =
      millis();

  while (!txDone)
  {
    Radio.IrqProcess();

    if (millis() - start > 10000)
    {
      Serial.println(
        "Napaka: LoRa TX predolgo traja."
      );

      break;
    }
  }
}

void setup()
{
  Serial.begin(115200);

  Serial.println();
  Serial.println("Zagon..");

  // HELTEC
  Mcu.begin(
    HELTEC_BOARD,
    SLOW_CLK_TPYE
  );

  // I2C

  Wire.begin(
    SDA_PIN,
    SCL_PIN
  );

  // ADS1115

  if (ads.begin())
  {
    adsAvailable = true;

    ads.setGain(GAIN_ONE);

    Serial.println(
      "ADS1115 zaznan."
    );
  }
  else
  {
    adsAvailable = false;

    Serial.println();
    Serial.println(
      "OPOZORILO: ADS1115 NI zaznan!"
    );

    Serial.println(
      "Program bo uporabljal samo DS18B20."
    );

    Serial.println(
      "LoRa bo posiljal samo temperaturo."
    );
  }

  // DS18B20

  waterTempSensor.begin();

  // LORA

  RadioEvents.TxDone =
      OnTxDone;

  RadioEvents.TxTimeout =
      OnTxTimeout;

  Radio.Init(
    &RadioEvents
  );

  Radio.SetChannel(
    RF_FREQUENCY
  );

  Radio.SetTxConfig(
    MODEM_LORA,
    TX_OUTPUT_POWER,
    0,
    LORA_BANDWIDTH,
    LORA_SPREADING_FACTOR,
    LORA_CODINGRATE,
    LORA_PREAMBLE_LENGTH,
    LORA_FIX_LENGTH_PAYLOAD_ON,
    true,
    0,
    0,
    LORA_IQ_INVERSION_ON,
    3000
  );

  Serial.println(
    "LoRa inicializiran."
  );
}

void loop()
{
  // TEMPERATURA

  float waterTemperature =
      readWaterTemperature();

  Serial.println();
  Serial.println(
    "----- MERITVE -----"
  );

  Serial.print(
    "Temperatura: "
  );

  Serial.print(
    waterTemperature,
    2
  );

  Serial.println(" C");

  // PREVERI DS18B20

  if (
      waterTemperature == DEVICE_DISCONNECTED_C ||
      waterTemperature < -20 ||
      waterTemperature > 60
     )
  {
    Serial.println(
      "NAPAKA: DS18B20 ni zaznan!"
    );

    delay(1000);

    return;
  }

  // ali ADS1115 DELA

  if (adsAvailable)
  {
    float turbidityVoltage =
        readTurbidityVoltage();

    float pH =
        readPH();

    Serial.print(
      "Turbidity: "
    );

    Serial.print(
      turbidityVoltage,
      3
    );

    Serial.println(" V");

    Serial.print(
      "pH: "
    );

    Serial.println(
      pH,
      2
    );

    // Pošlji vse 3 meritve
    sendLoRaFull(
      waterTemperature,
      turbidityVoltage,
      pH
    );
  }

   
  // če ADS1115 NE DELA
   

  else
  {
    Serial.println(
      "ADS1115 ni na voljo."
    );

    Serial.println(
      "Posiljam samo temperaturo."
    );

    sendLoRaTemperature(
      waterTemperature
    );
  }

  delay(1000);
}