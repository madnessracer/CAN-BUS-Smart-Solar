#ifndef CAN_COMMAND_HANDLER_H
#define CAN_COMMAND_HANDLER_H

#include <Arduino.h>
#include "driver/twai.h"

#include "Can-Bus IDs.h"
#include "CAN_SUBs.h"
#include "FileSystem.h"
#include "VE_HEX.h"

inline bool handleSolarCanCommand(const twai_message_t &frame,
                                  bool &canWriteUnlocked,
                                  unsigned long &canWriteUnlockUntil,
                                  unsigned long unlockWindowMs,
                                  uint8_t unlockKey1,
                                  uint8_t unlockKey2)
{
  if (frame.identifier != Can_Solar_Cmd || frame.data_length_code < 1)
  {
    return false;
  }

  uint8_t cmd = frame.data[0];

  if (cmd == 0x7E && frame.data_length_code >= 3)
  {
    // Schreibzugriff für ein Zeitfenster freigeben
    if (frame.data[1] == unlockKey1 && frame.data[2] == unlockKey2)
    {
      canWriteUnlocked = true;
      canWriteUnlockUntil = millis() + unlockWindowMs;
      CAN_SendEx(true, 2, Can_Solar_Resp, cmd, 0x00);
    }
    else
    {
      ErrorLogAdd("CAN Unlock fehlgeschlagen: falscher Key");
      CAN_SendEx(true, 2, Can_Solar_Resp, cmd, 0x01);
    }
    return false;
  }

  if (cmd == 0x10 || cmd == 0x12)
  {
    // Lesen: Absorption (0x10) oder Float (0x12)
    suppressTextMode();
    float v = (cmd == 0x10) ? getAbsorptionVoltage() : getFloatVoltage();
    uint16_t raw = (v > 0.0f) ? (uint16_t)(v * 100.0f) : 0;
    uint8_t ok = (v > 0.0f) ? 0x00 : 0x01;
    if (!ok)
    {
      ErrorLogAdd(cmd == 0x10 ? "Absorption lesen fehlgeschlagen" : "Float lesen fehlgeschlagen");
    }
    CAN_SendEx(true, 4, Can_Solar_Resp,
               cmd, ok, (raw >> 8) & 0xFF, raw & 0xFF);
    return false;
  }

  if (cmd == 0x14 || cmd == 0x15)
  {
    // Regler EIN/AUS schalten
    if (!canWriteUnlocked)
    {
      ErrorLogAdd("Regler schalten gesperrt: CAN Unlock fehlt");
      CAN_SendEx(true, 2, Can_Solar_Resp, cmd, 0x02);
      return true;
    }

    suppressTextMode();
    bool ok = setChargerOnOff(cmd == 0x14);
    if (!ok)
    {
      ErrorLogAdd(cmd == 0x14 ? "Regler EIN fehlgeschlagen" : "Regler AUS fehlgeschlagen");
    }
    CAN_SendEx(true, 2, Can_Solar_Resp, cmd, ok ? 0x00 : 0x01);
    return false;
  }

  if ((cmd == 0x11 || cmd == 0x13) && frame.data_length_code >= 3)
  {
    // Setzen: Absorption (0x11) oder Float (0x13)
    if (!canWriteUnlocked)
    {
      ErrorLogAdd("Spannung setzen gesperrt: CAN Unlock fehlt");
      CAN_SendEx(true, 2, Can_Solar_Resp, cmd, 0x02);
      return true;
    }

    uint16_t raw = ((uint16_t)frame.data[1] << 8) | frame.data[2];
    float v = raw / 100.0f;
    if (v >= 20.0f && v <= 35.0f)
    {
      suppressTextMode();
      bool ok;
      if (cmd == 0x11)
      {
        ok = setAbsorptionVoltage(v);
      }
      else
      {
        ok = setFloatVoltage(v);
      }
      if (!ok)
      {
        ErrorLogAdd(cmd == 0x11 ? "Absorption setzen fehlgeschlagen" : "Float setzen fehlgeschlagen");
      }
      CAN_SendEx(true, 2, Can_Solar_Resp, cmd, ok ? 0x00 : 0x01);
    }
    else
    {
      ErrorLogAdd("Spannung setzen verworfen: Wert ausserhalb 20.0-35.0V");
      CAN_SendEx(true, 2, Can_Solar_Resp, cmd, 0x03);
    }
    return false;
  }

  ErrorLogAdd("CAN Solar Cmd unbekannt");
  CAN_SendEx(true, 2, Can_Solar_Resp, cmd, 0x01);
  return false;
}

#endif
