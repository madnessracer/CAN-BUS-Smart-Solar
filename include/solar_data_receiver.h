#ifndef SOLAR_DATA_RECEIVER_H
#define SOLAR_DATA_RECEIVER_H

#include <Arduino.h>

bool dataUpdated = false;

// Struktur für Solardaten
struct SolarData
{
  float batteryVoltage = 0.0;    // V (mV)
  float batteryCurrent = 0.0;    // I (mA)
  float panelVoltage = 0.0;      // VPV (mV)
  float panelPower = 0.0;        // PPV (W)
  int stateOfOperation = 0;      // CS
  int trackerMode = 0;           // MPPT
  int offReason = 0;             // OR
  int errorCode = 0;             // ERR
  float yieldTotal = 0.0;        // H19 (kWh * 100)
  float yieldToday = 0.0;        // H20 (kWh * 100)
  float maxPowerToday = 0.0;     // H21 (W)
  float yieldYesterday = 0.0;    // H22 (kWh * 100)
  float maxPowerYesterday = 0.0; // H23 (W)
  int daySequence = 0;           // HSDS
};

// Globale Instanz der Solardaten
SolarData solarData;

// Flags für empfangene Labels
bool vReceived = false;
bool iReceived = false;
bool vpvReceived = false;
bool ppvReceived = false;
bool csReceived = false;
bool mpptReceived = false;
bool orReceived = false;
bool errReceived = false;
bool h19Received = false;
bool h20Received = false;
bool h21Received = false;
bool h22Received = false;
bool h23Received = false;
bool hsdsReceived = false;

// Inline-Funktion zum Zurücksetzen der Flags
inline void resetFlags()
{
  vReceived = false;
  iReceived = false;
  vpvReceived = false;
  ppvReceived = false;
  csReceived = false;
  mpptReceived = false;
  orReceived = false;
  errReceived = false;
  h19Received = false;
  h20Received = false;
  h21Received = false;
  h22Received = false;
  h23Received = false;
  hsdsReceived = false;
}

String getOffReasonText(int reason)
{
  switch (reason)
  {
  case 0:
    return "Nicht aus";
  case 1:
    return "Keine Eingangsleistung";
  case 2:
    return "Ausgeschaltet (Netzschalter)";
  case 3:
    return "Ausgeschaltet (Gerätemodusregister)";
  case 4:
    return "Fernsteuerung";
  case 5:
    return "Schutz aktiv";
  case 6:
    return "Paygo";
  case 7:
    return "BMS";
  case 8:
    return "Fernsteuerung aus";
  case 9:
    return "Fernsteuerung aus";
  default:
    return "Unbekannt (" + String(reason) + ")";
  }
}

// Inline-Funktion zum Parsen der Solardaten aus Serial2
// Gibt true zurück, wenn alle Daten empfangen wurden
inline bool parseSolarData()
{
  dataUpdated = false; // Jedes Mal zurücksetzen – true nur wenn kompletter Frame empfangen
  if (Serial2.available())
  {
    String line = Serial2.readStringUntil('\n');
    line.trim();
    if (line.length() > 0)
    {
      int tabIndex = line.indexOf('\t');
      if (tabIndex > 0)
      {
        String label = line.substring(0, tabIndex);
        String valueStr = line.substring(tabIndex + 1);
        float value = valueStr.toFloat();

        if (label == "V")
        {
          solarData.batteryVoltage = value / 1000.0;
          vReceived = true;
        }
        else if (label == "I")
        {
          solarData.batteryCurrent = value / 1000.0;
          iReceived = true;
        }
        else if (label == "VPV")
        {
          solarData.panelVoltage = value / 1000.0;
          vpvReceived = true;
        }
        else if (label == "PPV")
        {
          solarData.panelPower = value;
          ppvReceived = true;
        }
        else if (label == "CS")
        {
          solarData.stateOfOperation = (int)value;
          csReceived = true;
        }
        else if (label == "MPPT")
        {
          solarData.trackerMode = (int)value;
          mpptReceived = true;
        }
        else if (label == "OR")
        {
          solarData.offReason = (int)value;
          orReceived = true;
        }
        else if (label == "ERR")
        {
          solarData.errorCode = (int)value;
          errReceived = true;
        }
        else if (label == "H19")
        {
          solarData.yieldTotal = value / 100.0;
          h19Received = true;
        }
        else if (label == "H20")
        {
          solarData.yieldToday = value / 100.0;
          h20Received = true;
        }
        else if (label == "H21")
        {
          solarData.maxPowerToday = value;
          h21Received = true;
        }
        else if (label == "H22")
        {
          solarData.yieldYesterday = value / 100.0;
          h22Received = true;
        }
        else if (label == "H23")
        {
          solarData.maxPowerYesterday = value;
          h23Received = true;
        }
        else if (label == "HSDS")
        {
          solarData.daySequence = (int)value;
          hsdsReceived = true;
        }
      }
    }
  }

  // Prüfe, ob alle Pflicht-Labels empfangen wurden.
  // LOAD und IL sind optional (SmartSolar 100/50 hat keinen Load-Ausgang).
  if (vReceived && iReceived && vpvReceived && ppvReceived && csReceived && mpptReceived && orReceived && errReceived && h19Received && h20Received && h21Received && h22Received && h23Received && hsdsReceived)
  {
    dataUpdated = true;
    resetFlags();
  }

  return dataUpdated;
}

void SolarDatenAusgabe()
{
  if (dataUpdated)
  {
    Serial.write(0x00); // Null-Byte zum Löschen in Hterm
    Serial.println("--- Smart Solar Daten (alle empfangen) ---");
    Serial.print("Battery-Spannung: ");
    Serial.print(solarData.batteryVoltage);
    Serial.println(" V");
    Serial.print("Battery-Strom: ");
    Serial.print(solarData.batteryCurrent);
    Serial.println(" A");
    Serial.print("Panel-Spannung: ");
    Serial.print(solarData.panelVoltage);
    Serial.println(" V");
    Serial.print("Panel-Leistung: ");
    Serial.print(solarData.panelPower);
    Serial.println(" W");
    Serial.print("Betriebszustand: ");
    Serial.println(solarData.stateOfOperation);
    Serial.print("Tracker-Modus: ");
    Serial.println(solarData.trackerMode);
    Serial.print("Ausschalt-Grund: ");
    Serial.println(getOffReasonText(solarData.offReason));
    Serial.print("Fehlercode: ");
    Serial.println(solarData.errorCode);
    Serial.print("Gesamtertrag: ");
    Serial.print(solarData.yieldTotal);
    Serial.println(" kWh");
    Serial.print("Tagesertrag: ");
    Serial.print(solarData.yieldToday);
    Serial.println(" kWh");
    Serial.print("Max-Leistung heute: ");
    Serial.print(solarData.maxPowerToday);
    Serial.println(" W");
    Serial.print("Ertrag gestern: ");
    Serial.print(solarData.yieldYesterday);
    Serial.println(" kWh");
    Serial.print("Max-Leistung gestern: ");
    Serial.print(solarData.maxPowerYesterday);
    Serial.println(" W");
    Serial.print("Tagessequenz: ");
    Serial.println(solarData.daySequence);
    dataUpdated = false;
  }
}

#endif // SOLAR_DATA_RECEIVER_H