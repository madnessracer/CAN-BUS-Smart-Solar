#include <Arduino.h>
#include <DHT.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <config.h>

// Forward-Deklaration aus FileSystem.h
void ErrorLogAdd(String message);

#include "SensorManager.h"

namespace
{
  constexpr uint8_t DHT_TYPE = DHT22;
  constexpr unsigned long SENSOR_READ_INTERVAL_MS = 2500;
  constexpr uint8_t SENSOR_CAN_INVALID = 0xFF;

  constexpr float DS18_CAN_OFFSET_C = 50.0f;
  constexpr float DHT_CAN_OFFSET_C = 50.0f;

  DHT dht(DHT22_PIN, DHT_TYPE);
  OneWire oneWire(DS18B20_PIN);
  DallasTemperature ds18(&oneWire);

  AmbientSensorData sensorData = {NAN, NAN, NAN, false, false, 0};
  unsigned long lastReadAttemptMs = 0;
  bool bootDiagPrintedDhtInvalid = false;
  bool bootDiagPrintedDsInvalid = false;
  uint8_t dhtValidStreak = 0;  // Anzahl aufeinanderfolgender gültiger DHT22-Messungen
  uint8_t dsValidStreak = 0;   // Anzahl aufeinanderfolgender gültiger DS18B20-Messungen

  uint8_t clampToByte(int v)
  {
    if (v < 0)
      return 0;
    if (v > 254)
      return 254;
    return (uint8_t)v;
  }

  uint8_t encodeDs18CanByte(float tempC, bool valid)
  {
    if (!valid)
      return SENSOR_CAN_INVALID;
    int raw = (int)roundf(tempC + DS18_CAN_OFFSET_C);
    return clampToByte(raw);
  }

  uint8_t encodeDhtTempCanByte(float tempC, bool valid)
  {
    if (!valid)
      return SENSOR_CAN_INVALID;
    int raw = (int)roundf(tempC + DHT_CAN_OFFSET_C);
    return clampToByte(raw);
  }

  uint8_t encodeDhtHumidityCanByte(float humidity, bool valid)
  {
    if (!valid)
      return SENSOR_CAN_INVALID;
    int raw = (int)roundf(humidity);
    if (raw < 0)
      raw = 0;
    if (raw > 100)
      raw = 100;
    return (uint8_t)raw;
  }

  bool readSensorsNow()
  {
    float dhtTemp = dht.readTemperature();
    float dhtHumidity = dht.readHumidity();

    ds18.requestTemperatures();
    float dsTemp = ds18.getTempCByIndex(0);

    bool dhtTempOk = !isnan(dhtTemp) && dhtTemp >= -40.0f && dhtTemp <= 85.0f;
    bool dhtHumOk = !isnan(dhtHumidity) && dhtHumidity >= 0.0f && dhtHumidity <= 100.0f;
    bool dhtOk = dhtTempOk && dhtHumOk;

    bool dsOk = (dsTemp != DEVICE_DISCONNECTED_C) && (dsTemp >= -55.0f) && (dsTemp <= 125.0f);

    sensorData.dhtTempC = dhtTemp;
    sensorData.dhtHumidity = dhtHumidity;
    sensorData.ds18TempC = dsTemp;
    sensorData.dhtValid = dhtOk;
    sensorData.ds18Valid = dsOk;
    sensorData.lastUpdateMs = millis();

    if (dhtOk)
    {
      if (++dhtValidStreak >= 3)
        bootDiagPrintedDhtInvalid = false; // erst nach 3x OK darf Fehler neu geloggt werden
    }
    else
    {
      dhtValidStreak = 0;
      if (!bootDiagPrintedDhtInvalid)
      {
        Serial.println("[Sensor] DHT22 ungueltig/nicht vorhanden");
        ErrorLogAdd("[Sensor] DHT22 ungueltig/nicht vorhanden");
        bootDiagPrintedDhtInvalid = true;
      }
    }

    if (dsOk)
    {
      if (++dsValidStreak >= 3)
        bootDiagPrintedDsInvalid = false; // erst nach 3x OK darf Fehler neu geloggt werden
    }
    else
    {
      dsValidStreak = 0;
      if (!bootDiagPrintedDsInvalid)
      {
        Serial.println("[Sensor] DS18B20 ungueltig/nicht vorhanden");
        ErrorLogAdd("[Sensor] DS18B20 ungueltig/nicht vorhanden");
        bootDiagPrintedDsInvalid = true;
      }
    }

    return true;
  }
}

void Sensors_Init()
{
  dht.begin();
  ds18.begin();
  ds18.setResolution(9);
  ds18.setWaitForConversion(true);
  sensorData = {NAN, NAN, NAN, false, false, 0};
  lastReadAttemptMs = 0;
  bootDiagPrintedDhtInvalid = false;
  bootDiagPrintedDsInvalid = false;

  Serial.printf("Sensoren initialisiert: DHT22 GPIO%d, DS18B20 GPIO%d\n", DHT22_PIN, DS18B20_PIN);
}

bool Sensors_Update()
{
  if (millis() - lastReadAttemptMs < SENSOR_READ_INTERVAL_MS)
  {
    return false;
  }

  lastReadAttemptMs = millis();
  return readSensorsNow();
}

bool Sensors_ForceUpdate()
{
  lastReadAttemptMs = millis();
  return readSensorsNow();
}

const AmbientSensorData &Sensors_GetData()
{
  return sensorData;
}

bool Sensors_GetCanBytes(uint8_t &ds18TempByte, uint8_t &dhtTempByte, uint8_t &dhtHumidityByte)
{
  const AmbientSensorData &s = Sensors_GetData();
  ds18TempByte = encodeDs18CanByte(s.ds18TempC, s.ds18Valid);
  dhtTempByte = encodeDhtTempCanByte(s.dhtTempC, s.dhtValid);
  dhtHumidityByte = encodeDhtHumidityCanByte(s.dhtHumidity, s.dhtValid);
  return s.ds18Valid || s.dhtValid;
}

void Sensors_Print()
{
  const AmbientSensorData &s = Sensors_GetData();
  if (s.lastUpdateMs == 0)
  {
    Serial.println("Sensoren: noch keine Daten");
    return;
  }

  Serial.print("Sensoren: ");

  if (s.dhtValid)
  {
    Serial.printf("DHT22=%.1fC / %.1f%%", s.dhtTempC, s.dhtHumidity);
  }
  else
  {
    Serial.print("DHT22=ungueltig");
  }

  Serial.print(" | ");

  if (s.ds18Valid)
  {
    Serial.printf("DS18B20=%.1fC", s.ds18TempC);
  }
  else
  {
    Serial.print("DS18B20=ungueltig");
  }

  Serial.printf(" | age=%lums\n", millis() - s.lastUpdateMs);
}
