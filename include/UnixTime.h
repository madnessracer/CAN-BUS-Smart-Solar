#ifndef UNIX_TIME_H
#define UNIX_TIME_H

/*
 * UnixTime.h – Unabhängige Unix-Zeitquelle für ESP32
 *
 * Verwendet den eingebauten ESP32-SNTP-Client (läuft als eigener
 * FreeRTOS-Task, wird durch keinen anderen Code beeinflusst).
 *
 * Nutzung:
 *   UnixTime::begin();          // einmalig aufrufen, sobald WiFi verbunden
 *   time_t t = UnixTime::now(); // aktueller Unix-Timestamp (UTC)
 *   String s = UnixTime::nowString(); // "YYYY-MM-DD HH:MM:SS" (lokale Zeit)
 *   bool ok = UnixTime::isSynced();   // true sobald NTP geantwortet hat
 */

#include <Arduino.h>
#include <time.h>
#include <esp_sntp.h>

namespace UnixTime
{
  // NTP-Server und Zeitzone – Zeitzone kann als POSIX-String angepasst werden.
  // "CET-1CEST,M3.5.0,M10.5.0/3" = Mitteleuropa mit Sommerzeit
  static constexpr const char *NTP_SERVER1  = "pool.ntp.org";
  static constexpr const char *NTP_SERVER2  = "time.google.com";
  static constexpr const char *TZ_POSIX     = "CET-1CEST,M3.5.0,M10.5.0/3";

  // Interner Sync-Status (wird per SNTP-Callback gesetzt)
  static volatile bool _synced = false;

  // Letzter CAN-Empfang
  static volatile time_t  _lastCanTime = 0;  // empfangene Zeit als time_t
  static volatile uint32_t _lastCanMs  = 0;  // millis() des letzten Empfangs

  inline time_t   lastCanTime() { return _lastCanTime; }
  inline uint32_t lastCanMs()   { return _lastCanMs; }
  inline bool     hasCanTime()  { return _lastCanTime != 0; }

  // SNTP-Synchronisations-Callback (läuft im SNTP-Task)
  static void _ntpCallback(struct timeval *tv)
  {
    (void)tv;
    _synced = true;
    Serial.println("[UnixTime] NTP synchronisiert");
  }

  /**
   * Startet den NTP-Client. Einmalig aufrufen sobald WiFi verbunden ist.
   * Der SNTP-Task läuft danach vollständig im Hintergrund und
   * synchronisiert die interne RTC des ESP32 automatisch (alle ~1h).
   */
  inline void begin()
  {
    sntp_set_sync_mode(SNTP_SYNC_MODE_IMMED);      // sofort setzen, nicht schrittweise
    sntp_set_time_sync_notification_cb(_ntpCallback);
    configTzTime(TZ_POSIX, NTP_SERVER1, NTP_SERVER2);
    Serial.println("[UnixTime] NTP-Client gestartet");
  }

  /**
   * Gibt den aktuellen Unix-Timestamp (UTC, Sekunden seit 1970-01-01) zurück.
   * Liefert 0 solange noch keine Synchronisation stattgefunden hat.
   */
  inline time_t now()
  {
    return time(nullptr);
  }

  /**
   * Gibt true zurück, sobald mindestens eine NTP-Synchronisation
   * erfolgreich war.
   */
  inline bool isSynced()
  {
    return _synced;
  }

  /**
   * Gibt Datum und Uhrzeit als String zurück (lokale Zeit, abhängig von TZ_POSIX).
   * Format: "YYYY-MM-DD HH:MM:SS"
   * Gibt "--nicht synchronisiert--" zurück solange kein NTP-Sync vorhanden.
   */
  inline String nowString()
  {
    if (!_synced)
      return "--nicht synchronisiert--";

    time_t t = time(nullptr);
    struct tm ti;
    localtime_r(&t, &ti);

    char buf[20];
    snprintf(buf, sizeof(buf), "%02d.%02d.%04d %02d:%02d:%02d",
             ti.tm_mday, ti.tm_mon + 1, ti.tm_year + 1900,
             ti.tm_hour, ti.tm_min, ti.tm_sec);
    return String(buf);
  }

  /**
   * Liest Datum/Uhrzeit aus einem CAN-Paket mit ID 0x258 und DLC=6.
   * Bit-Format der ersten 4 Bytes (uint32_t little-endian):
   *   Bit  0- 5 : Jahr - 2000
   *   Bit  6- 9 : Monat
   *   Bit 10-14 : Tag
   *   Bit 15-19 : Stunde
   *   Bit 20-25 : Minute
   *   Bit 26-31 : Sekunde
   *
   * Gibt true zurück wenn die Systemzeit erfolgreich gesetzt wurde.
   * Muss für jeden empfangenen CAN-Frame aufgerufen werden:
   *   UnixTime::syncFromCAN(rx_frame.identifier, rx_frame.data_length_code, rx_frame.data);
   */
  inline bool syncFromCAN(uint32_t canId, uint8_t dlc, const uint8_t *data)
  {
    if (canId != 0x258U || dlc != 6)
      return false;

    // 4 Bytes little-endian zusammensetzen
    uint32_t val = (uint32_t)data[0]
                 | ((uint32_t)data[1] << 8)
                 | ((uint32_t)data[2] << 16)
                 | ((uint32_t)data[3] << 24);

    uint16_t year   = ((val >> 0)  & 0x3F) + 2000;
    uint8_t  month  =  (val >> 6)  & 0x0F;
    uint8_t  day    =  (val >> 10) & 0x1F;
    uint8_t  hour   =  (val >> 15) & 0x1F;
    uint8_t  minute =  (val >> 20) & 0x3F;
    uint8_t  second =  (val >> 26) & 0x3F;

    // Plausibilitätsprüfung
    if (year < 2020 || year > 2099 || month < 1 || month > 12 ||
        day < 1 || day > 31 || hour > 23 || minute > 59 || second > 59)
      return false;

    struct tm ti = {};
    ti.tm_year  = year - 1900;
    ti.tm_mon   = month - 1;
    ti.tm_mday  = day;
    ti.tm_hour  = hour;
    ti.tm_min   = minute;
    ti.tm_sec   = second;
    ti.tm_isdst = -1; // Sommerzeit automatisch ermitteln

    // Als lokale Zeit interpretieren und Systemzeit setzen
    time_t t = mktime(&ti);
    if (t == (time_t)-1)
      return false;

    struct timeval tv = { .tv_sec = t, .tv_usec = 0 };
    settimeofday(&tv, nullptr);

    _lastCanTime = t;
    _lastCanMs   = millis();

    if (!_synced)
    {
      _synced = true;
      Serial.println("[UnixTime] Zeit per CAN (0x258) synchronisiert");
    }
    return true;
  }

} // namespace UnixTime

#endif // UNIX_TIME_H
