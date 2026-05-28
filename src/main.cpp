#include <Arduino.h>

#include <WiFi.h>
#include <ArduinoOTA.h>
#include <FastLED.h>
#include <config.h>
#include "solar_data_receiver.h"
#include <FS.h>
#include <LittleFS.h>
#include "Can-Bus IDs.h"
#include "driver/twai.h"
#include <esp_task_wdt.h> // *** WATCHDOG

#define WDT_TIMEOUT_SEC 10 // *** WATCHDOG

#include <FileSystem.h>
#include <CAN_SUBs.h>
#include <Wlan_Config.h>
#include <CAN_INPUT_OTA.h>
#include <VE_HEX.h>
#include <SystemTests.h>
#include <CanCommandHandler.h>
#include <SensorManager.h>
#include <debug_menu.h>

// Globale Variablen für LED-Blinken
CRGB leds[NUM_LEDS];
unsigned long ledBlinkStart = 0;
bool ledBlinking = false;

// Debug-Modus
bool debugMode = false;
bool liveMode = false;

// Globale Variablen für CAN (falls später benötigt)
gpio_num_t s_txPin;
gpio_num_t s_rxPin;
uint8_t s_rxQueueLen = 50; // rxQueueLen = 50 (anpassbar)

// VE.Direct Stillstand-Erkennung
unsigned long lastSolarDataTime = 0;
const unsigned long SOLAR_TIMEOUT_MS = 30000; // 30s ohne Daten → Serial2 reset

// WiFi-Reconnect
unsigned long lastWifiRetryTime = 0;
const unsigned long WIFI_RETRY_INTERVAL_MS = 60000; // 60s

// CAN-Schutz: Setzbefehle nur in zeitlich begrenztem Freigabe-Fenster erlauben
bool canWriteUnlocked = false;
unsigned long canWriteUnlockUntil = 0;
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

  incrementBootCount();
  Serial.printf("Firmware-Version: %lu, Boot-Zähler: %lu\n", getFirmwareVersion(), getBootCount());
  ErrorLogInit();

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

  // Serial.print("CAN SPEED :");
  // Serial.println(CAN_cfg.speed);
  delay(500); // Kurz warten damit andere CAN-Teilnehmer bereit sind
  CAN_SendEx(true, 1, IP_Send_to_CAN, 0x03);

  esp_task_wdt_init(WDT_TIMEOUT_SEC, true);
  esp_task_wdt_add(NULL);
  Serial.println("Watchdog aktiviert (10s Timeout)");

  // OTA_Start();
}

void loop()
{
  esp_task_wdt_reset();

  if (canWriteUnlocked && millis() > canWriteUnlockUntil)
  {
    canWriteUnlocked = false;
  }

  // CAN Bus-Off Recovery prüfen
  CAN_CheckAndRecoverBusOff();

  if (OTA_On == 1)
  {
    ArduinoOTA.handle();
    // OTA-Timeout prüfen
    if (otaTimeoutMs > 0 && millis() - otaStartTime >= otaTimeoutMs)
    {
      Serial.println("[OTA] Timeout – OTA wird automatisch beendet");
      OTA_Stop();
    }
  }

  // Empfange CAN-Nachricht mit Timeout
  if (twai_receive(&rx_frame, pdMS_TO_TICKS(3)) == ESP_OK)
  {
    if (rx_frame.extd) // Prüfe auf Extended Frame
    {
      CanInputOTA();

      // VE.Direct Ladespannungen lesen/setzen via CAN
      if (handleSolarCanCommand(rx_frame,
                                canWriteUnlocked,
                                canWriteUnlockUntil,
                                CAN_WRITE_UNLOCK_WINDOW_MS,
                                CAN_WRITE_UNLOCK_KEY1,
                                CAN_WRITE_UNLOCK_KEY2))
      {
        return;
      }

      if (rx_frame.identifier == OTA_On_Off && rx_frame.data_length_code == 2)
      {
        if (rx_frame.data[0] == 0x01 && rx_frame.data[1] == 0x00)
        {
          OTA_Stop();
        }
        if (rx_frame.data[0] == 0x01 && rx_frame.data[1] == 0x01)
        {
          OTA_Start();
        }
      }
    }
  }

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
  if (liveMode && !dataUpdated && sensorUpdated)
  {
    Sensors_Print();
  }

  // CAN: Batteriespannung (mV), Ladestrom (A*10) und Leistung (W) senden bei neuen Reglerdaten
  if (dataUpdated)
  {
    int16_t batV = (int16_t)(solarData.batteryVoltage * 1000.0f); // mV
    int16_t ladeA = (int16_t)(solarData.batteryCurrent * 10.0f);  // A → 0,1A Schritte
    int16_t ladeW = (int16_t)solarData.panelPower;                // W
    CAN_SendEx(true, 6, MessageBasisID,
               (batV >> 8) & 0xFF, batV & 0xFF,
               (ladeA >> 8) & 0xFF, ladeA & 0xFF,
               (ladeW >> 8) & 0xFF, ladeW & 0xFF);

    // Solar-Panel: Spannung (mV), berechneter Strom (PPV/VPV, 0.1A), Tageszähler (HSDS)
    int16_t solV = (int16_t)(solarData.panelVoltage * 1000.0f); // V → mV
    int16_t solA = (solarData.panelVoltage > 0.1f)
                       ? (int16_t)(solarData.panelPower * 10.0f / solarData.panelVoltage)
                       : 0;                        // 0.1A Schritte
    int16_t hsds = (int16_t)solarData.daySequence; // Tageszähler (HSDS)
    CAN_SendEx(true, 6, MessageBasisID1,
               (solV >> 8) & 0xFF, solV & 0xFF,
               (solA >> 8) & 0xFF, solA & 0xFF,
               (hsds >> 8) & 0xFF, hsds & 0xFF);
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