#ifndef __CAN_SUBs_H__
#define __CAN_SUBs_H__

#include <cstdarg>
#include "driver/twai.h"



// Forward-Deklaration aus FileSystem.h
void ErrorLogAdd(String message);
void ErrorLogResetSuppression();

// TWAI-Nachrichten-Strukturen
twai_message_t rx_frame; // NUR zum Empfangen
twai_message_t tx;       // NUR zum Senden

// CAN_Init Funktion für TWAI
bool CAN_Init(gpio_num_t txPin, gpio_num_t rxPin, uint8_t rxQueueLen)
{
  twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(txPin, rxPin, TWAI_MODE_NORMAL);
  g_config.rx_queue_len = rxQueueLen;

  twai_timing_config_t t_config = TWAI_TIMING_CONFIG_250KBITS();
  twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

  esp_err_t ret = twai_driver_install(&g_config, &t_config, &f_config);
  if (ret != ESP_OK)
  {
    Serial.printf("TWAI install fehlgeschlagen: 0x%X\n", ret);
    return false;
  }

  ret = twai_start();
  if (ret != ESP_OK)
  {
    Serial.printf("TWAI start fehlgeschlagen: 0x%X\n", ret);
    return false;
  }

  uint32_t alertsToEnable = TWAI_ALERT_BUS_OFF | TWAI_ALERT_BUS_RECOVERED | TWAI_ALERT_ERR_PASS | TWAI_ALERT_ABOVE_ERR_WARN;
  twai_reconfigure_alerts(alertsToEnable, NULL);

  Serial.println("TWAI (CAN) Treiber initialisiert – 250kBit/s, Alerts aktiv");
  return true;
}

// Prüft CAN-Bus-Alerts und führt bei Bus-Off einen Neustart des TWAI-Treibers durch
// Bei wiederholtem Fehlschlag: kontrollierter ESP-Neustart
static uint8_t canRecoveryFailCount = 0;
static bool s_canTxErrorReported = false;      // TX-Fehler nur einmal pro Fehlerphase melden
static bool s_canBusAlertReported = false;     // Bus-Alert nur einmal pro Fehlerphase melden
static bool s_wasBusOff = false;              // Zustand für Bus-Recovery-Meldung


// Mindestabstand zwischen CAN-Paketen damit andere Teilnehmer senden können
#define CAN_INTER_MSG_DELAY_MS 10
static uint16_t s_canInterMsgDelayMs = CAN_INTER_MSG_DELAY_MS;
static unsigned long _lastCanSendMs = 0;
static uint8_t _canTxErrCount = 0;   // Zählt aufeinanderfolgende TX-Fehler
#define CAN_TX_ERR_MAX 50             // Nach N Fehlern: TWAI-Neustart

inline uint16_t CAN_GetInterMsgDelayMs()
{
  return s_canInterMsgDelayMs;
}

inline void CAN_SetInterMsgDelayMs(uint16_t delayMs)
{
  s_canInterMsgDelayMs = delayMs;
}

inline void CAN_InterMsgDelay()
{
  if (s_canInterMsgDelayMs == 0)
  {
    _lastCanSendMs = millis();
    return;
  }

  if (_lastCanSendMs != 0)
  {
    unsigned long elapsed = millis() - _lastCanSendMs;
    if (elapsed < s_canInterMsgDelayMs)
    {
      vTaskDelay(pdMS_TO_TICKS(s_canInterMsgDelayMs - elapsed));
    }
  }
  _lastCanSendMs = millis();
}

void CAN_CheckAndRecoverBusOff()
{
  uint32_t alerts = 0;
  bool busOffDetected = false;
  bool busRecovered = false;
  twai_status_info_t status = {};
  twai_get_status_info(&status);

  if (status.state == TWAI_STATE_BUS_OFF)
  {
    s_wasBusOff = true;
  }
  else if (s_wasBusOff)
  {
    busRecovered = true;
    s_wasBusOff = false;
  }

  if (twai_read_alerts(&alerts, 0) == ESP_OK && alerts != 0)
  {
    if (alerts & TWAI_ALERT_BUS_OFF)
    {
      busOffDetected = true;
      if (!s_canBusAlertReported)
      {
        ErrorLogAdd("[CAN] Bus-Off erkannt (Alert)");
        Serial.println("[CAN] Bus-Off erkannt (Alert)");
        s_canBusAlertReported = true;
      }
    }
    if (alerts & TWAI_ALERT_ERR_PASS)
    {
      if (!s_canBusAlertReported)
      {
        ErrorLogAdd("[CAN] Error-Passive – kein CAN-Bus angeschlossen?");
        Serial.println("[CAN] Error-Passive – kein CAN-Bus angeschlossen?");
        s_canBusAlertReported = true;
      }
    }
    if (alerts & TWAI_ALERT_ABOVE_ERR_WARN)
    {
      if (!s_canBusAlertReported)
      {
        ErrorLogAdd("[CAN] Error-Warnung");
        Serial.println("[CAN] Error-Warnung");
        s_canBusAlertReported = true;
      }
    }
    if (alerts & TWAI_ALERT_BUS_RECOVERED)
    {
      _canTxErrCount = 0;
      s_canBusAlertReported = false;
      ErrorLogResetSuppression();
      busRecovered = false; // Alert hat bereits geloggt – Zustandsmaschine überspringen
      Serial.println("[CAN] Bus wiederhergestellt");
      ErrorLogAdd("[CAN] Bus wiederhergestellt");
    }
  }

  // Zusätzliche Status-basierte Fehlererkennung, falls Alerts noch nicht ausgelöst wurden.
  if (!s_canBusAlertReported &&
      (status.state == TWAI_STATE_BUS_OFF ||
       status.tx_error_counter > 0 || status.rx_error_counter > 0))
  {
    if (status.state == TWAI_STATE_BUS_OFF)
    {
      Serial.printf("[CAN] Bus-Off erkannt (Status): state=%d tx_err=%u rx_err=%u\n", status.state, status.tx_error_counter, status.rx_error_counter);
      ErrorLogAdd("[CAN] Bus-Off erkannt (Status)");
      busOffDetected = true;
    }
    else
    {
      // Nur Fehlerzähler erhöht – keine separate Meldung, CAN_SendEx TX Fehler reicht
    }
    s_canBusAlertReported = true;
  }

  if (busRecovered)
  {
    _canTxErrCount = 0;
    s_canBusAlertReported = false;
    ErrorLogResetSuppression();
    Serial.println("[CAN] Bus wiederhergestellt");
    ErrorLogAdd("[CAN] Bus wiederhergestellt");
  }

  if (!busOffDetected && status.state == TWAI_STATE_BUS_OFF)
  {
    busOffDetected = true;
    if (!s_canBusAlertReported)
    {
      ErrorLogAdd("[CAN] Bus-Off erkannt (Status)");
      Serial.println("[CAN] Bus-Off erkannt (Status)");
      s_canBusAlertReported = true;
    }
  }

  if (busOffDetected)
  {
    twai_stop();
    twai_driver_uninstall();
    delay(200);
    twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT((gpio_num_t)CAN_TX_PIN, (gpio_num_t)CAN_RX_PIN, TWAI_MODE_NORMAL);
    g_config.rx_queue_len = 200;
    twai_timing_config_t t_config = TWAI_TIMING_CONFIG_250KBITS();
    twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();
    if (twai_driver_install(&g_config, &t_config, &f_config) == ESP_OK && twai_start() == ESP_OK)
    {
      uint32_t alertsToEnable = TWAI_ALERT_BUS_OFF | TWAI_ALERT_BUS_RECOVERED | TWAI_ALERT_ERR_PASS | TWAI_ALERT_ABOVE_ERR_WARN;
      twai_reconfigure_alerts(alertsToEnable, NULL);
      canRecoveryFailCount = 0;
      _canTxErrCount = 0;
      s_canTxErrorReported = false;
      s_canBusAlertReported = false;
      s_wasBusOff = false;
      ErrorLogResetSuppression();
      Serial.println("[CAN] CAN-Treiber neu gestartet");
      ErrorLogAdd("[CAN] CAN-Treiber neu gestartet");
    }
    else
    {
      canRecoveryFailCount++;
      if (canRecoveryFailCount >= 3)
      {
        delay(500);
        esp_restart();
      }
    }
  }

}

void CAN_Send(uint MesageID, byte MesageByte1 = 0, byte MesageByte2 = 0, byte MesageByte3 = 0, byte MesageByte4 = 0, byte MesageByte5 = 0, byte MesageByte6 = 0, byte MesageByte7 = 0, byte MesageByte8 = 0)
{
  tx.extd = 1; // Extended Frame (29-bit ID)
  tx.identifier = MesageID;
  tx.data_length_code = 8;
  tx.data[0] = MesageByte1;
  tx.data[1] = MesageByte2;
  tx.data[2] = MesageByte3;
  tx.data[3] = MesageByte4;
  tx.data[4] = MesageByte5;
  tx.data[5] = MesageByte6;
  tx.data[6] = MesageByte7;
  tx.data[7] = MesageByte8;

  esp_err_t ret = twai_transmit(&tx, pdMS_TO_TICKS(50));
  if (ret != ESP_OK)
  {
    if (!s_canTxErrorReported)
    {
      Serial.printf("[CAN] CAN_Send TX Fehler 0x%X id=0x%X dlc=%u\n", ret, tx.identifier, tx.data_length_code);
      ErrorLogAdd("[CAN] CAN_Send TX Fehler");
      s_canTxErrorReported = true;
    }
    _canTxErrCount++;
  }
  else
  {
    if (s_canTxErrorReported)
    {
      Serial.println("[CAN] CAN TX wieder erfolgreich");
      ErrorLogAdd("[CAN] CAN TX wieder erfolgreich");
      ErrorLogResetSuppression();
      s_canTxErrorReported = false;
    }
    _canTxErrCount = 0;
  }
  CAN_InterMsgDelay();
}

void CAN_SendEx(bool frameExtended, uint8_t dlc, uint MesageID, ...)
{
  if (dlc < 1) dlc = 1;
  if (dlc > 8) dlc = 8;

  tx.extd = frameExtended ? 1 : 0; // 1 für Extended, 0 für Standard
  tx.identifier = MesageID;
  tx.data_length_code = dlc;

  va_list args;
  va_start(args, MesageID);
  for (uint8_t i = 0; i < dlc; ++i) {
    int v = va_arg(args, int);
    tx.data[i] = (uint8_t)v;
  }
  va_end(args);

  for (uint8_t i = dlc; i < 8; ++i) tx.data[i] = 0;
  esp_err_t ret = twai_transmit(&tx, pdMS_TO_TICKS(50));
  if (ret != ESP_OK)
  {
    if (!s_canTxErrorReported)
    {
      Serial.printf("[CAN] CAN_SendEx TX Fehler 0x%X id=0x%X dlc=%u\n", ret, tx.identifier, tx.data_length_code);
      ErrorLogAdd("[CAN] CAN_SendEx TX Fehler");
      s_canTxErrorReported = true;
    }
    _canTxErrCount++;
  }
  else
  {
    if (s_canTxErrorReported)
    {
      Serial.println("[CAN] CAN TX wieder erfolgreich");
      ErrorLogAdd("[CAN] CAN TX wieder erfolgreich");
      ErrorLogResetSuppression();
      s_canTxErrorReported = false;
    }
    _canTxErrCount = 0;
  }
  CAN_InterMsgDelay();
}

// Heartbeat: alle 30s ein Lebenszeichen senden
void CAN_SendHeartbeat(uint16_t heartbeatID)
{
  static unsigned long lastHeartbeat = 0;
  if (millis() - lastHeartbeat >= 30000)
  {
    lastHeartbeat = millis();
    CAN_SendEx(true, 1, heartbeatID, 0xAA); // 0xAA = Lebenszeichen
  }
}
#endif