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
#include <debug_menu.h>

// Globale Variablen für LED-Blinken
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

void setup()
{

  Serial.begin(115200);

  Serial.println("------------------------------------------------");
  Serial.println();
  Serial.println("Bootvorgang gestartet...");

  incrementBootCount();
  Serial.printf("Firmware-Version: %lu, Boot-Zähler: %lu\n", getFirmwareVersion(), getBootCount());

  FastLED.addLeds<WS2812, LED_PIN, GRB>(leds, NUM_LEDS);
  FastLED.setBrightness(13);
  leds[0] = CRGB::Red;
  FastLED.show();

  Serial2.begin(BAUD_RATE, SERIAL_8N1, RX_PIN, TX_PIN);

  pinMode(CAN_SE_PIN, OUTPUT);
  digitalWrite(CAN_SE_PIN, LOW);
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
  CAN_SendEx(true, 1, IP_Send_to_CAN, 0x03);

  esp_task_wdt_init(WDT_TIMEOUT_SEC, true);
  esp_task_wdt_add(NULL);
  Serial.println("Watchdog aktiviert (10s Timeout)");

  //OTA_Start();
}

void loop()
{
  esp_task_wdt_reset();

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
      CanInputOTA();

      // VE.Direct Ladespannungen lesen/setzen via CAN
      if (rx_frame.identifier == Can_Solar_Cmd && rx_frame.data_length_code >= 1)
      {
        uint8_t cmd = rx_frame.data[0];
        if (cmd == 0x10 || cmd == 0x12) {
          // Lesen: Absorption (0x10) oder Float (0x12)
          suppressTextMode();
          float v = (cmd == 0x10) ? getAbsorptionVoltage() : getFloatVoltage();
          uint16_t raw = (v > 0.0f) ? (uint16_t)(v * 100.0f) : 0;
          uint8_t ok = (v > 0.0f) ? 0x00 : 0x01;
          CAN_SendEx(true, 4, Can_Solar_Resp,
                     cmd, ok, (raw >> 8) & 0xFF, raw & 0xFF);
        } else if ((cmd == 0x11 || cmd == 0x13) && rx_frame.data_length_code >= 3) {
          // Setzen: Absorption (0x11) oder Float (0x13)
          uint16_t raw = ((uint16_t)rx_frame.data[1] << 8) | rx_frame.data[2];
          float v = raw / 100.0f;
          if (v >= 20.0f && v <= 35.0f) {
            suppressTextMode();
            bool ok;
            if (cmd == 0x11) { setAbsorptionVoltage(v); ok = true; }
            else             { setFloatVoltage(v);       ok = true; }
            CAN_SendEx(true, 2, Can_Solar_Resp, cmd, ok ? 0x00 : 0x01);
          } else {
            CAN_SendEx(true, 2, Can_Solar_Resp, cmd, 0x01); // Spannung außerhalb Bereich
          }
        }
      }

      if (rx_frame.identifier == 0x25A)
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

  if (liveMode && dataUpdated)
  {
    SolarDatenAusgabe();
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