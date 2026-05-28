#ifndef SENSOR_MANAGER_H
#define SENSOR_MANAGER_H

#include <Arduino.h>

struct AmbientSensorData
{
  float dhtTempC;
  float dhtHumidity;
  float ds18TempC;
  bool dhtValid;
  bool ds18Valid;
  unsigned long lastUpdateMs;
};

void Sensors_Init();
bool Sensors_Update();
bool Sensors_ForceUpdate();
const AmbientSensorData &Sensors_GetData();
bool Sensors_GetCanBytes(uint8_t &ds18TempByte, uint8_t &dhtTempByte, uint8_t &dhtHumidityByte);
void Sensors_Print();

#endif
