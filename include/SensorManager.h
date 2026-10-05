#ifndef SENSOR_MANAGER_H
#define SENSOR_MANAGER_H

#include <Arduino.h>
#include <DHT.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <config.h>

// Forward-Deklaration aus FileSystem.h
void ErrorLogAdd(String message);

struct AmbientSensorData
{
  float dhtTempC;
  float dhtHumidity;
  float ds18TempC;
  bool dhtValid;
  bool ds18Valid;
  unsigned long lastUpdateMs;
};

namespace SensorManagerInternal
{
  constexpr uint8_t DHT_TYPE = DHT22;
  constexpr unsigned long SENSOR_READ_INTERVAL_MS = 5000;
  constexpr uint8_t SENSOR_CAN_INVALID = 0xFF;
  constexpr float DS18_CAN_OFFSET_C = 50.0f;
  constexpr float DHT_CAN_OFFSET_C = 50.0f;

  inline DHT &dht()
  {
    static DHT instance(DHT22_PIN, DHT_TYPE);
    return instance;
  }

  inline OneWire &oneWire()
  {
    static OneWire instance(DS18B20_PIN);
    return instance;
  }

  inline DallasTemperature &ds18()
  {
    static DallasTemperature instance(&oneWire());
    return instance;
  }

  inline AmbientSensorData &sensorData()
  {
    static AmbientSensorData data = {NAN, NAN, NAN, false, false, 0};
    return data;
  }

  inline unsigned long &lastReadAttemptMs()
  {
    static unsigned long value = 0;
    return value;
  }

  inline bool &bootDiagPrintedDhtInvalid()
  {
    static bool value = false;
    return value;
  }

  inline bool &bootDiagPrintedDsInvalid()
  {
    static bool value = false;
    return value;
  }

  inline uint8_t &dhtValidStreak()
  {
    static uint8_t value = 0;
    return value;
  }

  inline uint8_t &dhtErrorStreak()
  {
    static uint8_t value = 0;
    return value;
  }

  inline uint8_t &dsValidStreak()
  {
    static uint8_t value = 0;
    return value;
  }

  inline uint8_t clampToByte(int v)
  {
    if (v < 0)
      return 0;
    if (v > 254)
      return 254;
    return (uint8_t)v;
  }

  inline uint8_t encodeDs18CanByte(float tempC, bool valid)
  {
    if (!valid)
      return SENSOR_CAN_INVALID;
    int raw = (int)roundf(tempC + DS18_CAN_OFFSET_C);
    return clampToByte(raw);
  }

  inline uint8_t encodeDhtTempCanByte(float tempC, bool valid)
  {
    if (!valid)
      return SENSOR_CAN_INVALID;
    int raw = (int)roundf(tempC + DHT_CAN_OFFSET_C);
    return clampToByte(raw);
  }

  inline uint8_t encodeDhtHumidityCanByte(float humidity, bool valid)
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

  inline bool readSensorsNow()
  {
    float dhtTemp = dht().readTemperature();
    float dhtHumidity = dht().readHumidity();

    ds18().requestTemperatures();
    float dsTemp = ds18().getTempCByIndex(0);

    bool dhtTempOk = !isnan(dhtTemp) && dhtTemp >= -40.0f && dhtTemp <= 85.0f;
    bool dhtHumOk = !isnan(dhtHumidity) && dhtHumidity >= 0.0f && dhtHumidity <= 100.0f;
    bool dhtOk = dhtTempOk && dhtHumOk;

    bool dsOk = (dsTemp != DEVICE_DISCONNECTED_C) && (dsTemp >= -55.0f) && (dsTemp <= 125.0f);

    AmbientSensorData &data = sensorData();
    data.dhtTempC = dhtTemp;
    data.dhtHumidity = dhtHumidity;
    data.ds18TempC = dsTemp;
    data.dhtValid = dhtOk;
    data.ds18Valid = dsOk;
    data.lastUpdateMs = millis();

    if (dhtOk)
    {
      dhtErrorStreak() = 0;
      if (++dhtValidStreak() >= 3)
        bootDiagPrintedDhtInvalid() = false;
    }
    else
    {
      dhtValidStreak() = 0;
      if (dhtErrorStreak() < 5)
        dhtErrorStreak()++;
      if (dhtErrorStreak() == 3)
      {
        dht().begin(); // Stiller Neuversuch nach 3 Fehlern
        Serial.println("[Sensor] DHT22: Neuinitialisierung (3 Fehler)");
      }
      if (dhtErrorStreak() >= 5 && !bootDiagPrintedDhtInvalid())
      {
        Serial.println("[Sensor] DHT22 ungueltig/nicht vorhanden");
        bootDiagPrintedDhtInvalid() = true;
      }
    }

    if (dsOk)
    {
      if (++dsValidStreak() >= 3)
        bootDiagPrintedDsInvalid() = false;
    }
    else
    {
      dsValidStreak() = 0;
      if (!bootDiagPrintedDsInvalid())
      {
        Serial.println("[Sensor] DS18B20 ungueltig/nicht vorhanden");
        ErrorLogAdd("[Sensor] DS18B20 ungueltig/nicht vorhanden");
        bootDiagPrintedDsInvalid() = true;
      }
    }

    return true;
  }
}

inline void Sensors_Init()
{
  SensorManagerInternal::dht().begin();
  SensorManagerInternal::ds18().begin();
  SensorManagerInternal::ds18().setResolution(9);
  SensorManagerInternal::ds18().setWaitForConversion(true);
  SensorManagerInternal::sensorData() = {NAN, NAN, NAN, false, false, 0};
  SensorManagerInternal::lastReadAttemptMs() = 0;
  SensorManagerInternal::bootDiagPrintedDhtInvalid() = false;
  SensorManagerInternal::bootDiagPrintedDsInvalid() = false;

  Serial.printf("Sensoren initialisiert: DHT22 GPIO%d, DS18B20 GPIO%d\n", DHT22_PIN, DS18B20_PIN);
}

inline bool Sensors_Update()
{
  if (millis() - SensorManagerInternal::lastReadAttemptMs() < SensorManagerInternal::SENSOR_READ_INTERVAL_MS)
  {
    return false;
  }

  SensorManagerInternal::lastReadAttemptMs() = millis();
  return SensorManagerInternal::readSensorsNow();
}

inline bool Sensors_ForceUpdate()
{
  SensorManagerInternal::lastReadAttemptMs() = millis();
  return SensorManagerInternal::readSensorsNow();
}

inline const AmbientSensorData &Sensors_GetData()
{
  return SensorManagerInternal::sensorData();
}

inline bool Sensors_GetCanBytes(uint8_t &ds18TempByte, uint8_t &dhtTempByte, uint8_t &dhtHumidityByte)
{
  const AmbientSensorData &s = Sensors_GetData();
  ds18TempByte = SensorManagerInternal::encodeDs18CanByte(s.ds18TempC, s.ds18Valid);
  dhtTempByte = SensorManagerInternal::encodeDhtTempCanByte(s.dhtTempC, s.dhtValid);
  dhtHumidityByte = SensorManagerInternal::encodeDhtHumidityCanByte(s.dhtHumidity, s.dhtValid);
  return s.ds18Valid || s.dhtValid;
}

inline void Sensors_Print()
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

#endif
