#ifndef SYSTEM_TESTS_H
#define SYSTEM_TESTS_H

#include <Arduino.h>
#include <WiFi.h>
#include "driver/twai.h"

#include "solar_data_receiver.h"
#include "Can-Bus IDs.h"
#include "CAN_SUBs.h"
#include "FileSystem.h"
#include "Wlan_Config.h"
#include "SensorManager.h"

extern unsigned long lastSolarDataTime;
extern const unsigned long SOLAR_TIMEOUT_MS;
extern bool canWriteUnlocked;
extern unsigned long canWriteUnlockUntil;

inline void runSystemSelfTest()
{
  Serial.write(0x00);
  Serial.println("----- Systemtest -----");

  uint8_t okCount = 0;
  uint8_t warnCount = 0;
  uint8_t failCount = 0;

  auto report = [&](const char *name, uint8_t level, const String &msg)
  {
    if (level == 0)
    {
      okCount++;
      Serial.printf("[OK]   %-14s %s\n", name, msg.c_str());
    }
    else if (level == 1)
    {
      warnCount++;
      Serial.printf("[WARN] %-14s %s\n", name, msg.c_str());
    }
    else
    {
      failCount++;
      Serial.printf("[FAIL] %-14s %s\n", name, msg.c_str());
    }
  };

  uint32_t fw = getFirmwareVersion();
  uint32_t boots = getBootCount();
  if (boots == 0)
  {
    report("LittleFS", 1, "Boot-Zaehler ist 0 (frisches System oder FS-Reset)");
  }
  else
  {
    report("LittleFS", 0, "Boot=" + String(boots) + ", FW=" + String(fw));
  }

  twai_status_info_t canStatus = {};
  esp_err_t canRes = twai_get_status_info(&canStatus);
  if (canRes != ESP_OK)
  {
    report("CAN", 2, "Statusabfrage fehlgeschlagen");
    ErrorLogAdd("Systemtest FAIL: CAN Statusabfrage fehlgeschlagen");
  }
  else if (canStatus.state == TWAI_STATE_BUS_OFF)
  {
    report("CAN", 2, "BUS_OFF erkannt");
    ErrorLogAdd("Systemtest FAIL: CAN BUS_OFF");
  }
  else if (canStatus.tx_error_counter > 96 || canStatus.rx_error_counter > 96)
  {
    report("CAN", 1, "Hoher Error Counter (TX=" + String(canStatus.tx_error_counter) +
                         ", RX=" + String(canStatus.rx_error_counter) + ")");
  }
  else
  {
    report("CAN", 0, "State=" + String((int)canStatus.state) +
                     ", TXerr=" + String(canStatus.tx_error_counter) +
                     ", RXerr=" + String(canStatus.rx_error_counter));
  }

  if (lastSolarDataTime == 0)
  {
    report("VE.Direct", 1, "Noch keine Daten seit Boot empfangen");
  }
  else
  {
    unsigned long age = millis() - lastSolarDataTime;
    if (age > SOLAR_TIMEOUT_MS)
    {
      report("VE.Direct", 1, "Daten veraltet: " + String(age) + " ms");
    }
    else
    {
      report("VE.Direct", 0, "Daten aktuell: " + String(age) + " ms alt");
    }
  }

  if (solarData.batteryVoltage < 8.0f || solarData.batteryVoltage > 40.0f)
  {
    report("Messwerte", 1, "Battery V unplausibel: " + String(solarData.batteryVoltage, 2) + " V");
  }
  else
  {
    report("Messwerte", 0, "Battery=" + String(solarData.batteryVoltage, 2) + " V, Panel=" +
                           String(solarData.panelVoltage, 2) + " V, Power=" +
                           String(solarData.panelPower, 1) + " W");
  }

  if (OTA_On == 1 && WiFi.status() != WL_CONNECTED)
  {
    report("OTA/WiFi", 2, "OTA aktiv, aber WiFi nicht verbunden");
    ErrorLogAdd("Systemtest FAIL: OTA aktiv ohne WiFi");
  }
  else if (OTA_On == 0)
  {
    report("OTA/WiFi", 0, "OTA inaktiv (erwartet im Normalbetrieb)");
  }
  else
  {
    report("OTA/WiFi", 0, "OTA aktiv und WiFi verbunden");
  }

  if (canWriteUnlocked)
  {
    unsigned long rem = (canWriteUnlockUntil > millis()) ? (canWriteUnlockUntil - millis()) : 0;
    report("CAN-Lock", 1, "Schreibfenster offen, Rest " + String(rem / 1000UL) + " s");
  }
  else
  {
    report("CAN-Lock", 0, "Schreibzugriff gesperrt");
  }

  const AmbientSensorData &sensorData = Sensors_GetData();
  if (sensorData.lastUpdateMs == 0)
  {
    report("DHT22", 1, "Noch keine Sensorwerte vorhanden");
    report("DS18B20", 1, "Noch keine Sensorwerte vorhanden");
  }
  else
  {
    if (sensorData.dhtValid)
    {
      report("DHT22", 0, "Temp=" + String(sensorData.dhtTempC, 1) + "C, RH=" +
                         String(sensorData.dhtHumidity, 1) + "%");
    }
    else
    {
      report("DHT22", 2, "Ungueltiger Messwert");
      ErrorLogAdd("Systemtest FAIL: DHT22 ungueltig");
    }

    if (sensorData.ds18Valid)
    {
      report("DS18B20", 0, "Temp=" + String(sensorData.ds18TempC, 1) + "C");
    }
    else
    {
      report("DS18B20", 2, "Ungueltiger Messwert");
      ErrorLogAdd("Systemtest FAIL: DS18B20 ungueltig");
    }
  }

  Serial.println("----------------------");
  Serial.printf("Ergebnis: %u OK, %u WARN, %u FAIL\n", okCount, warnCount, failCount);

  if (failCount > 0)
  {
    ErrorLogAdd("Systemtest abgeschlossen: FAIL=" + String(failCount) + ", WARN=" + String(warnCount));
  }
  else if (warnCount > 0)
  {
    Serial.println("Hinweis: Warnungen vorhanden, aber keine harten Fehler.");
  }
  else
  {
    Serial.println("Systemtest erfolgreich: alle geprueften Punkte OK.");
  }
}

inline void runCanBusQuickTest(uint16_t frameCount)
{
  if (frameCount < 1)
    frameCount = 1;
  if (frameCount > 500)
    frameCount = 500;

  Serial.write(0x00);
  Serial.println("----- CAN Kurztest -----");
  Serial.printf("Testframes: %u\n", frameCount);

  twai_status_info_t before = {};
  twai_status_info_t after = {};

  if (twai_get_status_info(&before) != ESP_OK)
  {
    Serial.println("[FAIL] TWAI-Status konnte nicht gelesen werden.");
    ErrorLogAdd("CAN Kurztest FAIL: Statusabfrage vor Test fehlgeschlagen");
    return;
  }

  if (before.state == TWAI_STATE_BUS_OFF)
  {
    Serial.println("[FAIL] CAN ist BUS_OFF. Leitung/Bus aktuell nicht nutzbar.");
    ErrorLogAdd("CAN Kurztest FAIL: BUS_OFF vor Test");
    return;
  }

  Serial.printf("Vorher: state=%d, TXerr=%u, RXerr=%u\n",
                (int)before.state, before.tx_error_counter, before.rx_error_counter);

  constexpr uint32_t testId = 0x1FFFFFFF; // Höchste 29-Bit Extended-ID
  for (uint16_t i = 0; i < frameCount; i++)
  {
    uint8_t marker = (uint8_t)(i & 0xFF);
    CAN_SendEx(true, 3, testId, 0xC1, marker, 0x55);
  }

  vTaskDelay(pdMS_TO_TICKS(50));

  if (twai_get_status_info(&after) != ESP_OK)
  {
    Serial.println("[FAIL] TWAI-Status nach Test konnte nicht gelesen werden.");
    ErrorLogAdd("CAN Kurztest FAIL: Statusabfrage nach Test fehlgeschlagen");
    return;
  }

  Serial.printf("Nachher: state=%d, TXerr=%u, RXerr=%u\n",
                (int)after.state, after.tx_error_counter, after.rx_error_counter);

  int txDelta = (int)after.tx_error_counter - (int)before.tx_error_counter;
  int rxDelta = (int)after.rx_error_counter - (int)before.rx_error_counter;
  int warnThreshold = max(2, (int)frameCount / 25);
  int failThreshold = max(8, (int)frameCount / 5);

  if (after.state == TWAI_STATE_BUS_OFF)
  {
    Serial.println("[FAIL] Test hat BUS_OFF erzeugt. Leitung/Abschluss/Bus pruefen.");
    ErrorLogAdd("CAN Kurztest FAIL: BUS_OFF nach Test");
  }
  else if (txDelta >= failThreshold)
  {
    Serial.printf("[FAIL] Sehr viele TX-Fehler (Delta=%d, Schwellwert=%d).\n", txDelta, failThreshold);
    ErrorLogAdd("CAN Kurztest FAIL: sehr hoher TX-Error-Anstieg");
  }
  else if (txDelta >= warnThreshold)
  {
    Serial.printf("[WARN] Erhoehte TX-Fehler (Delta=%d, Schwellwert=%d). Leitung evtl. instabil.\n", txDelta, warnThreshold);
    ErrorLogAdd("CAN Kurztest WARN: hoher TX-Error-Anstieg");
  }
  else
  {
    Serial.println("[OK] CAN-Test ohne kritische Auffaelligkeit beendet.");
  }

  if (rxDelta > 0)
  {
    Serial.printf("Hinweis: RX-Error Delta=%d\n", rxDelta);
  }

  Serial.println("-----------------------");
}

#endif
