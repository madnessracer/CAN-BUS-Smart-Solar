#ifndef DEBUG_MENU_H
#define DEBUG_MENU_H

#include <Arduino.h>
#include <WiFi.h>
#include "VE_HEX.h"
#include "solar_data_receiver.h"
#include "FileSystem.h"
#include "Wlan_Config.h"

extern bool debugMode;
extern bool liveMode;

// WiFi-Untermenü Status
static bool wifiMode = false;
static int wifiScanCount = 0;
static String wifiSelectedSSID = "";

enum WifiStep { WIFI_MENU, WIFI_AWAIT_CHOICE, WIFI_AWAIT_PASSWORD };
static WifiStep wifiStep = WIFI_MENU;

// Forward-Deklaration
void handleDebugMenu(String input);

void handleWifiMenu(String input) {
  if (input == "wifi help") {
    Serial.write(0x00);
    Serial.println("----- WiFi-Menü -----");
    Serial.println("ssid      - Gespeicherte SSID anzeigen");
    Serial.println("pw        - Gespeichertes Passwort anzeigen");
    Serial.println("scan      - Verfügbare SSIDs scannen und auswählen");
    Serial.println("back      - Zurück zum Debug-Menü");
    Serial.println("---------------------");
    return;
  }

  if (wifiStep == WIFI_AWAIT_CHOICE) {
    int nr = input.toInt();
    if (nr >= 1 && nr <= wifiScanCount) {
      wifiSelectedSSID = WiFi.SSID(nr - 1);
      Serial.printf("Gewählt: %s\n", wifiSelectedSSID.c_str());
      Serial.println("Passwort eingeben:");
      wifiStep = WIFI_AWAIT_PASSWORD;
    } else {
      Serial.println("Ungültige Auswahl. Erneut eingeben oder 'back':");
    }
    return;
  }

  if (wifiStep == WIFI_AWAIT_PASSWORD) {
    String pw = input;
    pw.trim();
    SSID_Schreiben(wifiSelectedSSID);
    PASSWORD_Schreiben(pw);
    Serial.printf("SSID '%s' und Passwort gespeichert.\n", wifiSelectedSSID.c_str());
    wifiStep = WIFI_MENU;
    handleWifiMenu("wifi help");
    return;
  }

  // WIFI_MENU-Befehle
  if (input == "ssid") {
    Serial.print("Gespeicherte SSID: ");
    Serial.println(SSID_Lesen());
  } else if (input == "pw") {
    Serial.print("Gespeichertes Passwort: ");
    Serial.println(PASSWORD_Lesen());
  } else if (input == "scan") {
    Serial.println("Scanne SSIDs...");
    wifiScanCount = WiFi.scanNetworks();
    if (wifiScanCount <= 0) {
      Serial.println("Keine SSIDs gefunden.");
    } else {
      for (int i = 0; i < wifiScanCount; i++) {
        Serial.printf("%d: %s (%d dBm)%s\n", i + 1, WiFi.SSID(i).c_str(), WiFi.RSSI(i),
                      WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "" : " [gesichert]");
      }
      Serial.println("Nummer eingeben:");
      wifiStep = WIFI_AWAIT_CHOICE;
    }
  } else if (input == "back") {
    wifiMode = false;
    wifiStep = WIFI_MENU;
    handleDebugMenu("help");
  } else {
    Serial.println("Unbekannter Befehl. 'back' zum Zurück.");
  }
}

void handleDebugMenu(String input) {
  if (input == "help") {
    Serial.write(0x00); // Seriellen Monitor löschen
    Serial.println("----- Debug-Menü -----");
    Serial.println("help                  - Zeigt diese Hilfe");
    Serial.println("A / A <wert>          - Ladestrom abfragen oder einstellen (z.B. 'A 10')");
    Serial.println("abs / abs <volt>      - Absorption-Spannung lesen oder setzen (z.B. 'abs 28.8')");
    Serial.println("float / float <volt>  - Float-Spannung lesen oder setzen (z.B. 'float 27.2')");
    Serial.println("regler ein/aus/status - Regler schalten oder Status abfragen");
    Serial.println("ertrag / ertrag <tag> - Ertrag letzte 7 Tage oder einzelnen Tag abfragen");
    Serial.println("ertrag ?              - Anzahl Ertragstage, Gesamt & seit Reset");
    Serial.println("ertrag gesamt/reset   - Gesamtertrag oder Ertrag seit Reset einzeln");
    Serial.println("wifi                  - WiFi-Untermenü (SSID/Passwort anzeigen & ändern)");
    Serial.println("ota ein/aus/status    - OTA-Update starten, beenden oder Status anzeigen");
    Serial.println("live                  - Solardaten live anzeigen (beliebige Taste beendet)");
    Serial.println("hex <cmd>             - Direkt HEX-Befehl senden (z.B. 'hex :7F0ED0071')");
    Serial.println("exit                  - Debug-Modus beenden");
    Serial.println("----------------------");
  } else if (input == "A") {
    getChargeCurrent();
  } else if (input.startsWith("A ")) {
    float amper = input.substring(2).toFloat();
    amper = round(amper);
    if (amper > 0 && amper <= 100) {
      Serial.printf("Setze Ladestrom auf %.0f A...\n", amper);
      setChargeCurrent(amper);
    } else {
      Serial.println("Ungültiger Wert. Bitte 0 < A <= 100 eingeben.");
    }
  } else if (input == "abs") {
    suppressTextMode();
    getAbsorptionVoltage();
  } else if (input.startsWith("abs ")) {
    float v = input.substring(4).toFloat();
    if (v >= 20.0f && v <= 35.0f) {
      suppressTextMode();
      setAbsorptionVoltage(v);
    } else {
      Serial.println("Ungültiger Wert. Bitte 20.0 bis 35.0 V eingeben.");
    }
  } else if (input == "float") {
    suppressTextMode();
    getFloatVoltage();
  } else if (input.startsWith("float ")) {
    float v = input.substring(6).toFloat();
    if (v >= 20.0f && v <= 35.0f) {
      suppressTextMode();
      setFloatVoltage(v);
    } else {
      Serial.println("Ungültiger Wert. Bitte 20.0 bis 35.0 V eingeben.");
    }
  } else if (input == "regler ein") {
    Serial.println("Schalte Regler ein...");
    setChargerOnOff(true);
  } else if (input == "regler aus") {
    Serial.println("Schalte Regler aus...");
    setChargerOnOff(false);
  } else if (input == "regler status") {
    getChargerStatus();
  } else if (input == "ertrag") {
    Serial.println("Ertrag letzte 7 Tage:");
    for (int i = 0; i < 7; i++) {
      suppressTextMode();
      if (!getYield(i)) {
        Serial.println("Kein weiterer Ertrag vorhanden.");
        break;
      }
    }
  } else if (input == "ertrag ?") {
    suppressTextMode();
    int tage = countYieldDays();
    Serial.printf("%d Ertragstag(e) vorhanden.\n", tage);
    getYieldTotal();
    suppressTextMode();
    getYieldSinceReset();
  } else if (input == "ertrag gesamt") {
    getYieldTotal();
  } else if (input == "ertrag reset") {
    suppressTextMode();
    getYieldSinceReset();
  } else if (input.startsWith("ertrag ")) {
    suppressTextMode();
    int tag = input.substring(7).toInt();
    if (!getYield(tag)) {
      Serial.println("Kein Eintrag vorhanden.");
    }
  } else if (input.startsWith("hex ")) {
    String cmd = input.substring(4);
    cmd.trim();
    if (cmd.startsWith(":")) {
      while (Serial2.available()) Serial2.read();
      Serial2.print(cmd + "\r\n");
      Serial.println("Gesendet: " + cmd);
      unsigned long timeout = millis();
      String raw = "";
      while (millis() - timeout < 500) {
        if (Serial2.available()) raw += (char)Serial2.read();
      }
      int start = 0;
      bool found = false;
      for (int i = 0; i < (int)raw.length(); i++) {
        if (raw[i] == ':') {
          start = i;
        } else if (raw[i] == '\n' && start >= 0) {
          String line = raw.substring(start, i);
          line.trim();
          if (line.length() > 1) {
            Serial.println("Antwort: " + line);
            found = true;
          }
          start = -1;
        }
      }
      if (!found) Serial.println("Keine Antwort. Raw: " + raw);
    } else {
      Serial.println("Befehl muss mit ':' beginnen.");
    }
  } else if (input == "wifi") {
    wifiMode = true;
    wifiStep = WIFI_MENU;
    handleWifiMenu("wifi help");
  } else if (input == "ota ein") {
    Serial.println("Starte OTA...");
    OTA_Start();
    Serial.println(OTA_On == 1 ? "OTA aktiv." : "OTA-Start fehlgeschlagen.");
  } else if (input == "ota aus") {
    OTA_Stop();
    Serial.println("OTA beendet.");
  } else if (input == "ota status") {
    Serial.printf("OTA: %s\n", OTA_On == 1 ? "aktiv" : "inaktiv");
  } else if (input == "live") {
    liveMode = true;
    Serial.println("Live-Modus aktiv. Beliebige Taste zum Beenden.");
  } else if (input == "exit") {
    debugMode = false;
    Serial.println("Debug-Modus beendet.");
  } else {
    Serial.println("Unbekannter Befehl. 'help' für Hilfe.");
  }
}

// Im loop() aufrufen - verarbeitet serielle Eingaben
void handleSerialDebug() {
  if (Serial.available()) {
    String input = Serial.readStringUntil('\n');
    input.trim();
    if (liveMode) {
      liveMode = false;
      Serial.write(0x00); // hterm löschen
      Serial.println("Live-Modus beendet.");
      handleDebugMenu("help");
    } else if (input == "debug") {
      debugMode = true;
      Serial.write(0x00); // hterm löschen
      handleDebugMenu("help");
    } else if (wifiMode) {
      handleWifiMenu(input);
    } else if (debugMode) {
      handleDebugMenu(input);
    }
  }
}

#endif // DEBUG_MENU_H
