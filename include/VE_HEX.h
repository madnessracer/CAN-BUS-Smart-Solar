#ifndef VE_HEX_H
#define VE_HEX_H

#include <Arduino.h>
#include <esp_task_wdt.h>
#include "solar_data_receiver.h"

constexpr uint8_t VE_HEX_MAX_RETRIES = 3;
constexpr uint16_t VE_HEX_TIMEOUT_MS = 500;
constexpr size_t VE_HEX_RAW_LIMIT = 192;

// VE.Direct Checksumme berechnen (Bytes als Hex-Paare summieren)
uint8_t calculateChecksum(String commandBody) {
  if (commandBody.length() % 2 != 0) {
    commandBody = "0" + commandBody;
  }
  uint16_t sum = 0;
  for (size_t i = 0; i < commandBody.length(); i += 2) {
    char byteStr[3] = { commandBody[i], commandBody[i+1], '\0' };
    sum += (uint8_t)strtol(byteStr, NULL, 16);
  }
  return (uint8_t)((0x55 - (sum % 256) + 256) % 256);
}

// Ladestrom am Regler einstellen (SET)
void setChargeCurrent(float amper) {
  amper = round(amper);
  uint16_t value = (uint16_t)(amper * 10);
  char hexValue[5];
  sprintf(hexValue, "%02X%02X", value & 0xFF, (value >> 8) & 0xFF);

  String commandBody = "8F0ED00" + String(hexValue);
  uint8_t checksum = calculateChecksum(commandBody);
  char checksumStr[3];
  sprintf(checksumStr, "%02X", checksum);
  String command = ":" + commandBody + checksumStr + "\r\n";

  while (Serial2.available()) Serial2.read();
  Serial2.print(command);

  unsigned long timeout = millis();
  String raw = "";
  while (millis() - timeout < 500) {
    if (Serial2.available()) raw += (char)Serial2.read();
  }

  String expected = ":" + commandBody + checksumStr;
  if (raw.indexOf(expected) >= 0) {
    Serial.printf("Ladestrom erfolgreich auf %.1f A gesetzt.\n", amper);
  } else {
    Serial.println("Fehler beim Setzen. Antwort: " + raw);
  }
}

// Aktuellen Ladestrom vom Regler abfragen (GET)
void getChargeCurrent() {
  String commandBody = "7F0ED00";
  uint8_t checksum = calculateChecksum(commandBody);
  char checksumStr[3];
  sprintf(checksumStr, "%02X", checksum);
  String command = ":" + commandBody + checksumStr + "\r\n";

  while (Serial2.available()) Serial2.read();
  Serial2.print(command);

  unsigned long timeout = millis();
  String raw = "";
  while (millis() - timeout < 500) {
    if (Serial2.available()) raw += (char)Serial2.read();
  }

  int idx = raw.indexOf(":7F0ED00");
  if (idx >= 0 && raw.length() >= (size_t)(idx + 14)) {
    String response = raw.substring(idx, idx + 14);
    Serial.println("Regler-Antwort: " + response);
    String valStr = response.substring(8, 12);
    char lo[3] = { valStr[0], valStr[1], '\0' };
    char hi[3] = { valStr[2], valStr[3], '\0' };
    uint16_t value = (uint8_t)strtol(hi, NULL, 16) << 8 | (uint8_t)strtol(lo, NULL, 16);
    float amper = value / 10.0;
    Serial.printf("Eingestellte A: %.1f A\n", amper);
  } else {
    Serial.println("Keine Antwort vom Regler. Raw: " + raw);
  }
}

// TEXT-Modus unterdrücken: gültigen HEX-Befehl senden damit Regler pausiert
void suppressTextMode() {
  // Warte bis aktueller TEXT-Mode Frame abgeschlossen ist (Checksum-Zeile)
  unsigned long wait = millis();
  while (millis() - wait < 2000) {
    esp_task_wdt_reset();
    while (Serial2.available()) {
      String line = Serial2.readStringUntil('\n');
      if (line.startsWith("Checksum")) {
        // Frame abgeschlossen – jetzt kurz spülen und HEX senden
        while (Serial2.available()) Serial2.read();
        delay(10);
        goto frame_done;
      }
    }
  }
  frame_done:
  // Product ID GET schicken um TEXT-Mode zu unterdrücken
  String commandBody = "7000100";
  uint8_t checksum = calculateChecksum(commandBody);
  char checksumStr[3];
  sprintf(checksumStr, "%02X", checksum);
  Serial2.print(":" + commandBody + checksumStr + "\r\n");
  delay(200);
  while (Serial2.available()) Serial2.read();
}

// Solar-Ertrag für einen Tag abfragen (tag=0=heute, tag=1=gestern, ..., max 29)
// Gibt false zurück wenn kein Eintrag vorhanden
bool getYield(int tag) {
  if (tag < 0 || tag > 29) {
    Serial.println("Ungültiger Tag. Bereich: 0 (heute) bis 29.");
    return false;
  }
  uint16_t reg = 0x1050 + tag;
  uint8_t lo = reg & 0xFF;
  uint8_t hi = (reg >> 8) & 0xFF;
  char loStr[3], hiStr[3];
  sprintf(loStr, "%02X", lo);
  sprintf(hiStr, "%02X", hi);

  String commandBody = "7" + String(loStr) + String(hiStr) + "00";
  uint8_t checksum = calculateChecksum(commandBody);
  char checksumStr[3];
  sprintf(checksumStr, "%02X", checksum);
  String command = ":" + commandBody + checksumStr + "\r\n";

  while (Serial2.available()) Serial2.read();
  Serial2.print(command);

  // Prefix OHNE Flags suchen (":7" + loStr + hiStr = 6 Zeichen)
  String searchPrefix = ":7" + String(loStr) + String(hiStr);

  unsigned long timeout = millis();
  String raw = "";
  int idx = -1;
  while (millis() - timeout < 1500) {
    esp_task_wdt_reset();
    if (Serial2.available()) raw += (char)Serial2.read();
    if (idx < 0 && raw.length() >= 10) {
      int found = raw.indexOf(searchPrefix);
      if (found >= 0 && raw.length() >= (size_t)(found + 10)) {
        idx = found;
      }
    }
    if (idx >= 0 && raw.length() >= (size_t)(idx + 10)) {
      if (millis() - timeout > 300) break;
    }
  }

  String tagName = (tag == 0) ? "Heute" : (tag == 1) ? "Gestern" : ("vor " + String(tag) + " Tagen");

  if (idx < 0) {
    Serial.println(tagName + ": Keine Antwort vom Regler.");
    return false;
  }

  // Flags: idx+6, idx+7 (nach ":7" + loStr + hiStr = 6 Zeichen)
  char flagStr[3] = { raw[idx + 6], raw[idx + 7], '\0' };
  uint8_t flags = (uint8_t)strtol(flagStr, NULL, 16);
  if (flags & 0x04) {
    return false;
  }

  // Payload-Bytes ab idx+8
  auto readByte = [&](int payloadByte) -> uint8_t {
    int pos = idx + 8 + payloadByte * 2;
    if (raw.length() < (size_t)(pos + 2)) return 0;
    char s[3] = { raw[pos], raw[pos + 1], '\0' };
    return (uint8_t)strtol(s, NULL, 16);
  };

  if (raw.length() < (size_t)(idx + 8 + 34 * 2)) {
    Serial.println(tagName + ": Antwort unvollständig.");
    return false;
  }

  // Yield: Payload-Byte 1-4 (LE un32, 0.01 kWh)
  uint32_t yieldRaw = (uint32_t)readByte(1) | ((uint32_t)readByte(2) << 8) |
                      ((uint32_t)readByte(3) << 16) | ((uint32_t)readByte(4) << 24);
  float yieldKwh = yieldRaw * 0.01f;

  // Max. Leistung: Payload-Byte 24-27 (LE un32, 1W)
  uint32_t powerRaw = (uint32_t)readByte(24) | ((uint32_t)readByte(25) << 8) |
                      ((uint32_t)readByte(26) << 16) | ((uint32_t)readByte(27) << 24);

  // Akkuspannung max: Payload-Byte 9-10 (LE un16, 0.01V)
  uint16_t vBatMax = (uint16_t)readByte(9) | ((uint16_t)readByte(10) << 8);

  // Akkuspannung min: Payload-Byte 11-12 (LE un16, 0.01V)
  uint16_t vBatMin = (uint16_t)readByte(11) | ((uint16_t)readByte(12) << 8);

  // Solarspannung max: Payload-Byte 30-31 (LE un16, 0.01V)
  uint16_t vPanelMax = (uint16_t)readByte(30) | ((uint16_t)readByte(31) << 8);

  Serial.printf("%s: %.2f kWh, Max. %lu W, Solar max. %.2f V, Akku max. %.2f V, Akku min. %.2f V\n",
    tagName.c_str(), yieldKwh, (unsigned long)powerRaw, vPanelMax * 0.01f, vBatMax * 0.01f, vBatMin * 0.01f);
  return true;
}

// Anzahl der verfügbaren Ertragstage zählen (max. 30)
int countYieldDays() {
  int count = 0;
  for (int i = 0; i < 30; i++) {
    uint16_t reg = 0x1050 + i;
    uint8_t lo = reg & 0xFF;
    uint8_t hi = (reg >> 8) & 0xFF;
    char loStr[3], hiStr[3];
    sprintf(loStr, "%02X", lo);
    sprintf(hiStr, "%02X", hi);

    String commandBody = "7" + String(loStr) + String(hiStr) + "00";
    uint8_t checksum = calculateChecksum(commandBody);
    char checksumStr[3];
    sprintf(checksumStr, "%02X", checksum);
    String command = ":" + commandBody + checksumStr + "\r\n";

    while (Serial2.available()) Serial2.read();
    Serial2.print(command);

    String searchPrefix = ":7" + String(loStr) + String(hiStr);
    unsigned long timeout = millis();
    String raw = "";
    int idx = -1;
    while (millis() - timeout < 1500) {
      esp_task_wdt_reset();
      if (Serial2.available()) raw += (char)Serial2.read();
      if (idx < 0 && raw.length() >= 10) {
        int found = raw.indexOf(searchPrefix);
        if (found >= 0 && raw.length() >= (size_t)(found + 10)) idx = found;
      }
      if (idx >= 0 && millis() - timeout > 300) break;
    }
    if (idx < 0) break;
    char flagStr[3] = { raw[idx + 6], raw[idx + 7], '\0' };
    uint8_t flags = (uint8_t)strtol(flagStr, NULL, 16);
    if (flags & 0x04) break;
    count++;
  }
  return count;
}

// Gesamtertrag Lebensdauer direkt aus letztem TEXT-Mode Frame (H19)
void getYieldTotal() {
  Serial.printf("Gesamtertrag (Lebensdauer): %.2f kWh\n", solarData.yieldTotal);
}

// Ertrag seit User-Reset (Register 0x010E, un32, 0.01 kWh)
// Vor Aufruf suppressTextMode() aufrufen!
void getYieldSinceReset() {
  String commandBody = "70E0100";
  uint8_t checksum = calculateChecksum(commandBody);
  char checksumStr[3];
  sprintf(checksumStr, "%02X", checksum);
  String command = ":" + commandBody + checksumStr + "\r\n";

  while (Serial2.available()) Serial2.read();
  Serial2.print(command);

  unsigned long timeout = millis();
  String raw = "";
  int idx = -1;
  while (millis() - timeout < 1500) {
    esp_task_wdt_reset();
    if (Serial2.available()) raw += (char)Serial2.read();
    if (idx < 0) {
      int found = raw.indexOf(":70E01");
      if (found >= 0 && raw.length() >= (size_t)(found + 10)) idx = found;
    }
    if (idx >= 0 && millis() - timeout > 300) break;
  }

  if (idx < 0) { Serial.println("Kein Ertrag seit Reset empfangen."); return; }
  char flagStr[3] = { raw[idx+6], raw[idx+7], '\0' };
  uint8_t flags = (uint8_t)strtol(flagStr, NULL, 16);
  if (flags & 0x04) { Serial.println("Ertrag seit Reset nicht vorhanden."); return; }

  // Payload bis Zeilenende lesen, letzte 2 Zeichen (Checksum) weglassen
  String payloadAndCs = "";
  for (int i = idx + 8; i < (int)raw.length(); i++) {
    char c = raw[i];
    if (c == '\r' || c == '\n') break;
    payloadAndCs += c;
  }
  String payloadStr = payloadAndCs.length() > 2 ? payloadAndCs.substring(0, payloadAndCs.length() - 2) : "00000000";

  // Bis zu 4 Bytes little-endian lesen
  uint32_t val = 0;
  int bytes = min((int)payloadStr.length() / 2, 4);
  for (int i = 0; i < bytes; i++) {
    char s[3] = { payloadStr[i*2], payloadStr[i*2+1], '\0' };
    val |= (uint32_t)strtol(s, NULL, 16) << (i * 8);
  }
  if (val == 0) {
    Serial.printf("Ertrag seit Reset: %.2f kWh (kein Reset, zeige Gesamtertrag)\n", solarData.yieldTotal);
  } else {
    Serial.printf("Ertrag seit Reset: %.2f kWh\n", val * 0.01f);
  }
}

// Remote-Steuerung aktivieren (Register 0x0202, Bit 1 setzen)
void enableRemoteControl() {
  String commandBody = "802020002000000";
  uint8_t checksum = calculateChecksum(commandBody);
  char checksumStr[3];
  sprintf(checksumStr, "%02X", checksum);
  String command = ":" + commandBody + checksumStr + "\r\n";

  while (Serial2.available()) Serial2.read();
  Serial2.print(command);
  delay(100);
  while (Serial2.available()) Serial2.read();
}

// Regler-Status abfragen (Register 0x0200)
void getChargerStatus() {
  String commandBody = "7000200";
  uint8_t checksum = calculateChecksum(commandBody);
  char checksumStr[3];
  sprintf(checksumStr, "%02X", checksum);
  String command = ":" + commandBody + checksumStr + "\r\n";

  while (Serial2.available()) Serial2.read();
  Serial2.print(command);

  unsigned long timeout = millis();
  String raw = "";
  while (millis() - timeout < 500) {
    if (Serial2.available()) raw += (char)Serial2.read();
  }

  // Antwort: :7000200<flags><wert_byte><checksum>
  // Prefix ":7000200" = 8 Zeichen, danach Wert bei idx+8, idx+9
  int idx = raw.indexOf(":7000200");
  if (idx >= 0 && raw.length() >= (size_t)(idx + 10)) {
    char valStr[3] = { raw[idx + 8], raw[idx + 9], '\0' };
    uint8_t val = (uint8_t)strtol(valStr, NULL, 16);
    if (val == 1) {
      Serial.println("Regler-Status: EIN (1)");
    } else if (val == 4 || val == 0) {
      Serial.println("Regler-Status: AUS (" + String(val) + ")");
    } else {
      Serial.printf("Regler-Status: unbekannt (0x%02X)\n", val);
    }
  } else {
    Serial.println("Keine Antwort vom Regler. Raw: " + raw);
  }
}

// Regler ein- oder ausschalten (Register 0x0200)
// Rückgabe: true bei bestätigter Antwort, sonst false
bool setChargerOnOff(bool on) {
  enableRemoteControl();

  String valueHex = on ? "01" : "04";
  String commandBody = "8000200" + valueHex;
  uint8_t checksum = calculateChecksum(commandBody);
  char checksumStr[3];
  sprintf(checksumStr, "%02X", checksum);
  String command = ":" + commandBody + checksumStr + "\r\n";

  while (Serial2.available()) Serial2.read();
  Serial2.print(command);

  unsigned long timeout = millis();
  String raw = "";
  while (millis() - timeout < 500) {
    if (Serial2.available()) raw += (char)Serial2.read();
  }

  String expected = ":" + commandBody + checksumStr;
  if (raw.indexOf(expected) >= 0) {
    Serial.println(on ? "Regler eingeschaltet." : "Regler ausgeschaltet.");
    return true;
  } else {
    Serial.println("Fehler beim Schalten. Antwort: " + raw);
    return false;
  }
}

// Hilfsfunktion: letzte gültige Antwort (Flags=00) suchen, Echo überspringen
static int veHexFindResponse(const String &raw, const String &prefix) {
  int idx = -1;
  int start = 0;
  while (true) {
    int found = raw.indexOf(prefix, start);
    if (found < 0) break;
    if (raw.length() >= (size_t)(found + 8)) {
      char flagStr[3] = { raw[found + 6], raw[found + 7], '\0' };
      uint8_t flags = (uint8_t)strtol(flagStr, NULL, 16);
      if (flags == 0x00) idx = found; // nur Antworten ohne Fehler-Flag
    }
    start = found + 1;
  }
  return idx;
}

// Antwort mit begrenzter Puffergröße lesen (reduziert Heap-Fragmentierung)
static void veHexReadRaw(String &raw, unsigned long timeoutMs = VE_HEX_TIMEOUT_MS) {
  raw = "";
  raw.reserve(VE_HEX_RAW_LIMIT + 16);

  unsigned long timeout = millis();
  while (millis() - timeout < timeoutMs) {
    esp_task_wdt_reset();
    while (Serial2.available()) {
      char c = (char)Serial2.read();
      if (raw.length() < VE_HEX_RAW_LIMIT) {
        raw += c;
      }
    }
  }
}

// Battery type auf User defined setzen (0xEDF1 = 0xFF)
// Pflicht vor dem Setzen von Absorption/Float-Spannung (Note 5)
static bool setBatteryTypeUser() {
  String commandBody = "8F1ED00FF";
  uint8_t checksum = calculateChecksum(commandBody);
  char frame[24];
  snprintf(frame, sizeof(frame), ":%s%02X\r\n", commandBody.c_str(), checksum);

  for (uint8_t attempt = 0; attempt < VE_HEX_MAX_RETRIES; attempt++) {
    while (Serial2.available()) Serial2.read();
    Serial2.print(frame);

    String raw;
    veHexReadRaw(raw);
    if (raw.indexOf(":8F1ED") >= 0) {
      Serial.println("Battery type: User defined (0xFF) gesetzt.");
      return true;
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
  Serial.println("Fehler: Battery type konnte nicht gesetzt werden.");
  return false;
}

// Hilfsfunktion: uint16-Register lesen, Ergebnis in outVal
static bool veHexGetU16(const String &body, const String &prefix, uint16_t &outVal) {
  uint8_t checksum = calculateChecksum(body);
  char frame[24];
  snprintf(frame, sizeof(frame), ":%s%02X\r\n", body.c_str(), checksum);

  for (uint8_t attempt = 0; attempt < VE_HEX_MAX_RETRIES; attempt++) {
    while (Serial2.available()) Serial2.read();
    Serial2.print(frame);

    String raw;
    veHexReadRaw(raw);

    int idx = veHexFindResponse(raw, prefix);
    if (idx >= 0 && raw.length() >= (size_t)(idx + 14)) {
      char lo[3] = { raw[idx + 8], raw[idx + 9], '\0' };
      char hi[3] = { raw[idx + 10], raw[idx + 11], '\0' };
      outVal = ((uint8_t)strtol(hi, NULL, 16) << 8) | (uint8_t)strtol(lo, NULL, 16);
      return true;
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
  return false;
}

// Hilfsfunktion: uint16-Register setzen
static bool veHexSetU16(const String &body, const String &prefix, float voltageV) {
  (void)prefix;
  uint16_t value = (uint16_t)(voltageV * 100.0f);
  char hexValue[5];
  sprintf(hexValue, "%02X%02X", value & 0xFF, (value >> 8) & 0xFF);

  String commandBody = body + String(hexValue);
  uint8_t checksum = calculateChecksum(commandBody);
  char frame[28];
  snprintf(frame, sizeof(frame), ":%s%02X\r\n", commandBody.c_str(), checksum);

  for (uint8_t attempt = 0; attempt < VE_HEX_MAX_RETRIES; attempt++) {
    while (Serial2.available()) Serial2.read();
    Serial2.print(frame);

    String raw;
    veHexReadRaw(raw);
    if (raw.indexOf(":" + commandBody) >= 0) {
      return true;
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
  return false;
}

// Absorption-Spannung lesen (Register 0xEDF7, un16, 0.01V)
// Gibt Spannung in V zurück, 0.0f bei Fehler.
float getAbsorptionVoltage() {
  uint16_t val = 0;
  if (veHexGetU16("7F7ED00", ":7F7ED", val)) {
    float v = val / 100.0f;
    Serial.printf("Absorption-Spannung (0xEDF7): %.2f V\n", v);
    return v;
  }
  Serial.println("Keine Antwort (Absorption 0xEDF7).");
  return 0.0f;
}

// Float-Spannung lesen (Register 0xEDF6, un16, 0.01V)
// Gibt Spannung in V zurück, 0.0f bei Fehler.
float getFloatVoltage() {
  uint16_t val = 0;
  if (veHexGetU16("7F6ED00", ":7F6ED", val)) {
    float v = val / 100.0f;
    Serial.printf("Float-Spannung (0xEDF6): %.2f V\n", v);
    return v;
  }
  Serial.println("Keine Antwort (Float 0xEDF6).");
  return 0.0f;
}

// Absorption-Spannung setzen (voltageV in Volt, z.B. 28.8)
// Battery type wird automatisch auf User defined gesetzt
bool setAbsorptionVoltage(float voltageV) {
  if (!setBatteryTypeUser()) return false;
  if (veHexSetU16("8F7ED00", ":8F7ED", voltageV)) {
    Serial.printf("Absorption-Spannung gesetzt: %.2f V\n", voltageV);
    return true;
  } else {
    Serial.println("Fehler beim Setzen (Absorption 0xEDF7).");
    return false;
  }
}

// Float-Spannung setzen (voltageV in Volt, z.B. 27.2)
// Battery type wird automatisch auf User defined gesetzt
bool setFloatVoltage(float voltageV) {
  if (!setBatteryTypeUser()) return false;
  if (veHexSetU16("8F6ED00", ":8F6ED", voltageV)) {
    Serial.printf("Float-Spannung gesetzt: %.2f V\n", voltageV);
    return true;
  } else {
    Serial.println("Fehler beim Setzen (Float 0xEDF6).");
    return false;
  }
}

#endif // VE_HEX_H
