#include <Arduino.h>

#include <WiFi.h>
#include <ArduinoOTA.h>
#include <FastLED.h>
#include <config.h>
#include <FS.h>
#include <LittleFS.h>
#include "Can-Bus IDs.h"
#include "driver/twai.h"
#include <esp_task_wdt.h> // *** WATCHDOG
#include <esp_system.h>   // esp_reset_reason()

#define WDT_TIMEOUT_SEC 10 // *** WATCHDOG

#include <SerialBridge.h>
#define Serial SerialBridge
#include "solar_data_receiver.h"
#include <FileSystem.h>
#include <CAN_SUBs.h>
#include <Wlan_Config.h>
#include <CAN_INPUT_OTA.h>
#include <VE_HEX.h>
#include <SystemTests.h>
#include <CanCommandHandler.h>
#include <SensorManager.h>
#include <debug_menu.h>
#include <UnixTime.h>

// Globale Variablen für LED-Blinken
CRGB leds[NUM_LEDS];
unsigned long ledBlinkStart = 0;
bool ledBlinking = false;

// Debug-Modus
bool debugMode = false;
bool liveMode = false;

bool batteryLeftKnown = false;
bool batteryRightKnown = false;
bool batteryLeftOn = false;
bool batteryRightOn = false;
bool batteryLeftEmergency = false;
bool batteryRightEmergency = false;
bool batteryLeftError = false;
bool batteryRightError = false;
unsigned long batteryLeftLastSeen = 0;
unsigned long batteryRightLastSeen = 0;
bool batteryStartupCheckDone = false;
static constexpr uint8_t BATTERY_STATUS_ON_BIT = 0x10;
static constexpr uint8_t BATTERY_STATUS_ERROR_BIT = 0x40;
static constexpr uint8_t BATTERY_STATUS_EMERGENCY_BIT = 0x80;
static constexpr unsigned long BATTERY_STATUS_TIMEOUT_MS = 5000;
bool manualOffActive = false;

// Globale Variablen für CAN (falls später benötigt)
gpio_num_t s_txPin;
gpio_num_t s_rxPin;
uint8_t s_rxQueueLen = 20;

// VE.Direct Stillstand-Erkennung
unsigned long lastSolarDataTime = 0;
const unsigned long SOLAR_TIMEOUT_MS = 30000; // 30s ohne Daten → Serial2 reset

// WiFi-Reconnect
unsigned long lastWifiRetryTime = 0;
const unsigned long WIFI_RETRY_INTERVAL_MS = 60000; // 60s

// CAN-Schutz: Setzbefehle nur in zeitlich begrenztem Freigabe-Fenster erlauben
bool canWriteUnlocked = false;
unsigned long canWriteUnlockStart = 0; // millis() bei Freigabe (überlaufsicher)
const unsigned long CAN_WRITE_UNLOCK_WINDOW_MS = 60000; // 60s
const uint8_t CAN_WRITE_UNLOCK_KEY1 = 0xA5;
const uint8_t CAN_WRITE_UNLOCK_KEY2 = 0x5A;

void setup()
{

  Serial.begin(115200);
  Serial.write(0x00);
  Serial.println("------------------------------------------------");
  Serial.println();
  Serial.println("Bootvorgang gestartet... " + String(WIFI_Name));

  Serial.println("OTA-Standardzugangsdaten setzen");
  SSID_Schreiben("ota");
  PASSWORD_Schreiben("x756iklx756ikl");

  incrementBootCount();
  Serial.printf("Firmware-Version: %lu, Boot-Zähler: %lu\n", getFirmwareVersion(), getBootCount());

  // Gespeicherten Fehlerlog laden
  ErrorLogLoad();

  // Reset-Grund ermitteln und ins Log schreiben
  {
    esp_reset_reason_t reason = esp_reset_reason();
    const char *reasonStr = "Unbekannt";
    bool suppressLog = false;
    switch (reason) {
      case ESP_RST_POWERON:   reasonStr = "Neustart: Einschalten (Power-On)"; break;
      case ESP_RST_SW:
        if (OTA_UpdatePendingReset())
        {
          reasonStr = "Neustart: OTA Update";
          suppressLog = true;
        }
        else
        {
          reasonStr = "Neustart: Software-Reset";
        }
        break;
      case ESP_RST_PANIC:     reasonStr = "Neustart: Crash/Panic"; break;
      case ESP_RST_INT_WDT:   reasonStr = "Neustart: Interrupt-Watchdog"; break;
      case ESP_RST_TASK_WDT:  reasonStr = "Neustart: Task-Watchdog (Haenger)"; break;
      case ESP_RST_WDT:       reasonStr = "Neustart: Watchdog"; break;
      case ESP_RST_DEEPSLEEP: reasonStr = "Neustart: Deep-Sleep Wake"; break;
      case ESP_RST_BROWNOUT:  reasonStr = "Neustart: Unterspannung (Brownout)"; break;
      case ESP_RST_SDIO:      reasonStr = "Neustart: SDIO"; break;
      default: break;
    }
    if (suppressLog)
    {
      OTA_ClearPendingReset();
    }
    Serial.printf("[Reset-Grund] %s\n", reasonStr);
    if (!suppressLog)
    {
      ErrorLogAdd(reasonStr);
    }
  }

  FastLED.addLeds<WS2812, LED_PIN, GRB>(leds, NUM_LEDS);
  FastLED.setBrightness(13);
  leds[0] = CRGB::Red;
  FastLED.show();

  Serial2.begin(BAUD_RATE, SERIAL_8N1, RX_PIN, TX_PIN);
  Sensors_Init();

  pinMode(CAN_SE_PIN, OUTPUT);
  digitalWrite(CAN_SE_PIN, LOW);
  pinMode(PIN_5V_EN, OUTPUT);
  digitalWrite(PIN_5V_EN, HIGH);
  
  delay(100); // Kurze Verzögerung, um sicherzustellen, dass der Transceiver bereit ist
  Serial.println("CAN SE (GPIO23) als OUTPUT gesetzt (Can Transceiver aktiv)");

  // CAN-Bus initialisieren mit 250 kbit/s
  if (!CAN_Init((gpio_num_t)CAN_TX_PIN, (gpio_num_t)CAN_RX_PIN, s_rxQueueLen))
  {
    Serial.println("Fehler: TWAI Initialisierung fehlgeschlagen");
  }
  else
  {
    Serial.println("TWAI (CAN) initialisiert mit 250 kbit/s");
  }

  CAN_SendEx(true, 1, IP_Send_to_CAN, 0x03);

  SerialBridge.begin(115200);
  Serial.println("SerialBridge initialisiert");

  esp_task_wdt_init(WDT_TIMEOUT_SEC, true);
  esp_task_wdt_add(NULL);
  Serial.println("Watchdog aktiviert (10s Timeout)");

  // OTA_Start();
}

void loop()
{
  esp_task_wdt_reset();

  if (canWriteUnlocked && (millis() - canWriteUnlockStart) >= CAN_WRITE_UNLOCK_WINDOW_MS)
  {
    canWriteUnlocked = false;
  }

  // CAN Bus-Off Recovery prüfen
  CAN_CheckAndRecoverBusOff();

  if (OTA_On == 1)
  {
    ArduinoOTA.handle();
  }

  // Empfange CAN-Nachricht mit Timeout
  if (twai_receive(&rx_frame, pdMS_TO_TICKS(3)) == ESP_OK)
  {
    if (rx_frame.extd) // Prüfe auf Extended Frame
    {
      // Datum/Uhrzeit aus CAN-Paket 0x258 (Extended Frame)
      UnixTime::syncFromCAN(rx_frame.identifier, rx_frame.data_length_code, rx_frame.data);

      CanInputOTA();

      if (rx_frame.identifier == MessageBatteryStatusLeftID ||
          rx_frame.identifier == MessageBatteryStatusRightID)
      {
        if (rx_frame.data_length_code >= 8)
        {
          bool batteryOn = (rx_frame.data[7] & BATTERY_STATUS_ON_BIT) != 0;
          bool emergencyShutdown = (rx_frame.data[7] & BATTERY_STATUS_EMERGENCY_BIT) != 0;

          bool batteryError = (rx_frame.data[7] & BATTERY_STATUS_ERROR_BIT) != 0;
          if (rx_frame.identifier == MessageBatteryStatusLeftID)
          {
            batteryLeftKnown = true;
            batteryLeftOn = batteryOn;
            batteryLeftEmergency = emergencyShutdown;
            batteryLeftError = batteryError;
            batteryLeftLastSeen = millis();
          }
          else
          {
            batteryRightKnown = true;
            batteryRightOn = batteryOn;
            batteryRightEmergency = emergencyShutdown;
            batteryRightError = batteryError;
            batteryRightLastSeen = millis();
          }

          bool bothBatteriesEmergency = batteryLeftKnown && batteryRightKnown && batteryLeftEmergency && batteryRightEmergency;
          bool otherBatteryOn = (rx_frame.identifier == MessageBatteryStatusLeftID) ? batteryRightOn : batteryLeftOn;
          bool otherBatteryKnown = (rx_frame.identifier == MessageBatteryStatusLeftID) ? batteryRightKnown : batteryLeftKnown;
          bool otherBatteryOff = otherBatteryKnown && !otherBatteryOn;
          bool thisBatteryIsLastOn = batteryOn && otherBatteryOff;
          bool thisBatteryError = (rx_frame.identifier == MessageBatteryStatusLeftID) ? batteryLeftError : batteryRightError;
          bool otherBatteryError = (rx_frame.identifier == MessageBatteryStatusLeftID) ? batteryRightError : batteryLeftError;

          if (!manualOffActive)
          {
            if (thisBatteryError && thisBatteryIsLastOn)
            {
              Serial.println("[BatteryStatus] letzte Batterie meldet Fehler, Regler wird ausgeschaltet.");
              setChargerOnOff(false);
            }
            else if (bothBatteriesEmergency || (emergencyShutdown && thisBatteryIsLastOn))
            {
              Serial.println("[BatteryStatus] Notabschaltung für letzte Batterie, Regler wird ausgeschaltet.");
              setChargerOnOff(false);
            }
          }
          else if (batteryLeftKnown || batteryRightKnown)
          {
            static bool prevManualOffActive = false;
            if (prevManualOffActive != manualOffActive)
            {
              if (manualOffActive)
                Serial.println("[BatteryStatus] Manuelle Abschaltung aktiv, automatische Reglersteuerung unterdrückt.");
              else
                Serial.println("[BatteryStatus] Manuelle Abschaltung aufgehoben, automatische Reglersteuerung aktiv.");
              prevManualOffActive = manualOffActive;
            }
          }

          if (!batteryStartupCheckDone)
          {
            if (batteryLeftOn || batteryRightOn)
            {
              batteryStartupCheckDone = true;
            }
            else if (batteryLeftKnown && batteryRightKnown)
            {
              if (solarData.stateOfOperation != 0)
              {
                Serial.println("[BatteryStatus] Beim Start keine Batterie ein, Regler wird ausgeschaltet.");
                setChargerOnOff(false);
              }
              batteryStartupCheckDone = true;
            }
          }
        }
      }

      // VE.Direct Ladespannungen lesen/setzen via CAN
      handleSolarCanCommand(rx_frame,
                            canWriteUnlocked,
                            canWriteUnlockStart,
                            CAN_WRITE_UNLOCK_WINDOW_MS,
                            CAN_WRITE_UNLOCK_KEY1,
                            CAN_WRITE_UNLOCK_KEY2);

      if (rx_frame.identifier == OTA_On_Off && rx_frame.data_length_code == 2)
      {
        if (rx_frame.data[0] == 0x01 && rx_frame.data[1] == 0x00)
        {
          OTA_Stop();
        }
        else if (rx_frame.data[0] == 0x01 && rx_frame.data[1] == 0x01)
        {
          OTA_Start();
        }
        else if (rx_frame.data[0] == 0x01 && rx_frame.data[1] == 0x02)
        {
          Serial.println("SerialBridge deaktivieren");
          SerialBridge.stopBridge();
        }
        else if (rx_frame.data[0] == 0x01 && rx_frame.data[1] == 0x03)
        {
          Serial.println("SerialBridge aktivieren");
          SerialBridge.beginBridge();
        }
      }
    }
  }

  unsigned long now = millis();
  if (batteryLeftLastSeen > 0 && now - batteryLeftLastSeen > BATTERY_STATUS_TIMEOUT_MS)
  {
    batteryLeftOn = false;
    batteryLeftEmergency = false;
    batteryLeftLastSeen = now;
  }
  if (batteryRightLastSeen > 0 && now - batteryRightLastSeen > BATTERY_STATUS_TIMEOUT_MS)
  {
    batteryRightOn = false;
    batteryRightEmergency = false;
    batteryRightLastSeen = now;
  }

  // SerialBridge-Handling
  SerialBridge.handleBridge();

  // Serielles Debug-Menü
  handleSerialDebug();

  // VE.Direct Stillstand prüfen – Serial2 neu initialisieren wenn zu lange keine Daten
  if (millis() - lastSolarDataTime > SOLAR_TIMEOUT_MS && lastSolarDataTime > 0)
  {
    Serial.println("[VE.Direct] Kein Datenstrom seit 30s – Serial2 wird neu initialisiert");
    Serial2.end();
    delay(100);
    Serial2.begin(BAUD_RATE, SERIAL_8N1, RX_PIN, TX_PIN);
    lastSolarDataTime = millis(); // Cooldown
  }

  // WiFi-Reconnect wenn OTA_On war aber WiFi verloren (nicht wenn manuell deaktiviert)
  if (OTA_On == 0 && WiFi_Error == 0 && wifiReconnectEnabled &&
      millis() - lastWifiRetryTime > WIFI_RETRY_INTERVAL_MS)
  {
    lastWifiRetryTime = millis();
    if (WiFi.status() != WL_CONNECTED)
    {
      Serial.println("[WiFi] Verbindung verloren – Reconnect-Versuch");
      OTA_Start();
    }
  }

  // Solardaten parsen und prüfen, ob aktualisiert
  bool dataUpdated = parseSolarData();
  if (dataUpdated)
    lastSolarDataTime = millis();

  static unsigned long lastYieldQueryMs = 0;
  static unsigned long lastYieldSendMs = 0;
  static bool yieldValuesReady = false;
  if (!yieldValuesReady && (lastYieldQueryMs == 0 || millis() - lastYieldQueryMs > 60000))
  {
    suppressTextMode();
    if (getYield(0)) {
      yieldValuesReady = true;
      lastYieldSendMs = millis() - 6000; // erstes Senden sofort erlauben
    }
    lastYieldQueryMs = millis();
  }

  if (yieldValuesReady && millis() - lastYieldSendMs > 6000)
  {
    uint16_t yieldTodayRaw = (uint16_t)roundf(solarData.yieldToday * 10.0f);
    uint16_t maxPowerRaw = (uint16_t)roundf(solarData.maxPowerToday * 10.0f);
    uint16_t solarMaxRaw = (uint16_t)roundf(solarData.panelVoltageMax * 10.0f);
    uint16_t batMaxRaw = (uint16_t)roundf(solarData.batteryVoltageMax * 10.0f);
    CAN_SendEx(true, 8, MessageYieldSummaryID,
               (yieldTodayRaw >> 8) & 0xFF, yieldTodayRaw & 0xFF,
               (maxPowerRaw >> 8) & 0xFF, maxPowerRaw & 0xFF,
               (solarMaxRaw >> 8) & 0xFF, solarMaxRaw & 0xFF,
               (batMaxRaw >> 8) & 0xFF, batMaxRaw & 0xFF);

    uint16_t batMinRaw = (uint16_t)roundf(solarData.batteryVoltageMin * 10.0f);
    CAN_SendEx(true, 2, MessageBatteryMinID,
               (batMinRaw >> 8) & 0xFF, batMinRaw & 0xFF);

    lastYieldSendMs = millis();
  }

  bool sensorUpdated = Sensors_Update();

  if (sensorUpdated)
  {
    uint8_t ds18TempByte = 0xFF;
    uint8_t dhtTempByte = 0xFF;
    uint8_t dhtHumidityByte = 0xFF;
    Sensors_GetCanBytes(ds18TempByte, dhtTempByte, dhtHumidityByte);
    CAN_SendEx(true, 3, MessageSensorID, ds18TempByte, dhtTempByte, dhtHumidityByte);
  }

  if (liveMode && dataUpdated)
  {
    SolarDatenAusgabe();
    Sensors_Print();
  }

  // Falls gerade kein neuer Solar-Frame kam, bei Sensor-Update dennoch anzeigen.
  else if (liveMode && !dataUpdated && sensorUpdated)
  {
    Sensors_Print();
  }

  // CAN: Batteriespannung (mV), Ladestrom (A*10), Leistung (W) und Betriebszustand (CS) senden bei neuen Reglerdaten
  if (dataUpdated)
  {
    int16_t batV = (int16_t)(solarData.batteryVoltage * 1000.0f); // mV
    int16_t ladeA = (int16_t)(solarData.batteryCurrent * 10.0f);  // A → 0,1A Schritte
    int16_t ladeW = (int16_t)solarData.panelPower;                // W
    uint8_t cs = (uint8_t)solarData.stateOfOperation;             // CS Betriebszustand
    CAN_SendEx(true, 7, MessageBasisID,
               (batV >> 8) & 0xFF, batV & 0xFF,
               (ladeA >> 8) & 0xFF, ladeA & 0xFF,
               (ladeW >> 8) & 0xFF, ladeW & 0xFF,
               cs);

    // Solar-Panel: Spannung (V*100, big-endian), berechneter Strom (PPV/VPV, 0.1A), Tageszähler (HSDS), Abschaltgrund (OR)
    uint16_t solV = (uint16_t)roundf(solarData.panelVoltage * 100.0f); // V → 0.01V
    int16_t solA = (solarData.panelVoltage > 0.1f)
                       ? (int16_t)roundf(solarData.panelPower * 10.0f / solarData.panelVoltage)
                       : 0;                        // 0.1A Schritte
    int16_t hsds = (int16_t)solarData.daySequence; // Tageszähler (HSDS)
    uint8_t ds18TempByte = 0xFF;
    uint8_t dhtTempByte = 0xFF;
    uint8_t dhtHumidityByte = 0xFF;
    Sensors_GetCanBytes(ds18TempByte, dhtTempByte, dhtHumidityByte);
    CAN_SendEx(true, 8, MessageBasisID1,
               (solV >> 8) & 0xFF, solV & 0xFF,
               (solA >> 8) & 0xFF, solA & 0xFF,
               (hsds >> 8) & 0xFF, hsds & 0xFF,
               (uint8_t)solarData.offReason,
               ds18TempByte);
  }

  // Heartbeat alle 30s
  CAN_SendHeartbeat((uint16_t)(IP_Send_to_CAN + 1));

  // LED-Steuerung: Normalzustand setzen
  if (OTA_On == 1)
  {
    leds[0] = CRGB::Black; // Normalzustand bei OTA: aus (schwarz)
  }
  else
  {
    leds[0] = CRGB::Green; // Normalzustand ohne OTA: an (grün)
  }
  FastLED.show();

  // Blinken bei neuen Smart Solar Daten
  if (dataUpdated && !ledBlinking)
  {
    ledBlinkStart = millis();
    ledBlinking = true;
    if (OTA_On == 1)
    {
      leds[0] = CRGB::Green; // Bei OTA: an für 100 ms (invert blitzen)
    }
    else
    {
      leds[0] = CRGB::Black; // Ohne OTA: aus für 100 ms (normal blitzen)
    }
    FastLED.show();
  }

  if (ledBlinking && millis() - ledBlinkStart >= 100)
  {
    // Zurück zum Normalzustand nach 100 ms
    if (OTA_On == 1)
    {
      leds[0] = CRGB::Black;
    }
    else
    {
      leds[0] = CRGB::Green;
    }
    FastLED.show();
    ledBlinking = false;
  }
}