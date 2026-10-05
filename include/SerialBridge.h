#ifndef SERIAL_BRIDGE_H
#define SERIAL_BRIDGE_H

#include <Arduino.h>
#include <HardwareSerial.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>

extern int WiFi_Error;
extern const char WIFI_Name[];
extern String SSID_Lesen();
extern String PASSWORD_Lesen();

constexpr uint16_t BRIDGE_PORT = 2323;

class SerialBridgeClass : public Stream
{
public:
  static constexpr unsigned long AUTO_LOGOFF_MS = 5UL * 60UL * 1000UL; // 5 Minuten

  SerialBridgeClass()
    : hwSerial(::Serial), server(BRIDGE_PORT), active(false), lastClientMillis(0)
  {
  }

  void begin(unsigned long baud)
  {
    hwSerial.begin(baud);
  }

  bool bridgeActive() const
  {
    return active;
  }

  bool clientConnected()
  {
    return client.connected();
  }

  bool beginBridge()
  {
    if (active)
      return true;

    String ssid = SSID_Lesen();
    String pass = PASSWORD_Lesen();
    if (ssid.length() == 0)
    {
      hwSerial.println("[BRIDGE] Keine SSID konfiguriert.");
      return false;
    }

    char ssidBuf[33] = {0};
    char passBuf[33] = {0};
    ssid.toCharArray(ssidBuf, sizeof(ssidBuf));
    pass.toCharArray(passBuf, sizeof(passBuf));

    WiFi.mode(WIFI_STA);
    WiFi.setHostname(WIFI_Name);
    WiFi.disconnect(true);
    delay(10);
    WiFi.begin(ssidBuf, passBuf);

    WiFi_Error = 0;
    while (WiFi.status() != WL_CONNECTED && WiFi_Error < 50)
    {
      esp_task_wdt_reset();
      delay(100);
      WiFi_Error++;
    }

    if (WiFi.status() != WL_CONNECTED)
    {
      hwSerial.println("[BRIDGE] WLAN-Verbindung fehlgeschlagen.");
      WiFi.disconnect();
      WiFi.mode(WIFI_OFF);
      return false;
    }

    WiFi.setSleep(false);
    server.begin();
    active = true;
    lastClientMillis = millis();
    hwSerial.printf("[BRIDGE] aktiv, IP=%s, Port=%u\n", WiFi.localIP().toString().c_str(), BRIDGE_PORT);
    return true;
  }

  void stopBridge()
  {
    if (!active)
      return;

    if (client && client.connected())
      client.stop();

    server.end();
    WiFi.disconnect();
    WiFi.mode(WIFI_OFF);
    active = false;
    hwSerial.println("[BRIDGE] deaktiviert.");
  }

  void handleBridge()
  {
    if (!active)
      return;

    if (client && !client.connected())
      client.stop();

    if ((!client || !client.connected()) && server.hasClient())
    {
      if (client && client.connected())
        client.stop();

      client = server.available();
      if (client)
      {
        client.setNoDelay(true);
        hwSerial.println("[BRIDGE] Verbindung hergestellt.");
        client.println("ESP Debug-Bridge verbunden.");
      }
    }

    if (clientConnected())
    {
      lastClientMillis = millis();
    }
    else if (millis() - lastClientMillis >= AUTO_LOGOFF_MS)
    {
      hwSerial.println("[BRIDGE] Kein Client mehr, auto logoff.");
      stopBridge();
    }
  }

  virtual int available() override
  {
    if (hwSerial.available())
      return hwSerial.available();
    if (client && client.connected())
      return client.available();
    return 0;
  }

  virtual int read() override
  {
    if (hwSerial.available())
      return hwSerial.read();
    if (client && client.connected())
      return client.read();
    return -1;
  }

  virtual int peek() override
  {
    if (hwSerial.available())
      return hwSerial.peek();
    if (client && client.connected())
      return client.peek();
    return -1;
  }

  virtual void flush() override
  {
    hwSerial.flush();
    if (client && client.connected())
      client.flush();
  }

  virtual size_t write(int c)
  {
    return write((uint8_t)c);
  }

  virtual size_t write(uint8_t c) override
  {
    if (client && client.connected())
      return client.write(c);
    return hwSerial.write(c);
  }

  virtual size_t write(const uint8_t *buffer, size_t size) override
  {
    if (client && client.connected())
      return client.write(buffer, size);
    return hwSerial.write(buffer, size);
  }

  using Print::write;

  int printf(const char *format, ...)
  {
    char tmp[256];
    va_list args;
    va_start(args, format);
    int len = vsnprintf(tmp, sizeof(tmp), format, args);
    va_end(args);
    if (len > 0)
      write((const uint8_t *)tmp, (size_t)len);
    return len;
  }

private:
  HardwareSerial &hwSerial;
  WiFiServer server;
  WiFiClient client;
  bool active;
  unsigned long lastClientMillis;
};

inline SerialBridgeClass SerialBridge;

#endif // SERIAL_BRIDGE_H
