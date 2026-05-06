#ifndef __Wlan_Config_H__
#define __Wlan_Config_H__

#include <esp_task_wdt.h>

const char *ssid;
const char *password;

static char ssid_buf[33] = {0};
static char password_buf[33] = {0};

byte OTA_On = 0;
int WiFi_Error = 0;
bool wifiReconnectEnabled = false; // true erst nach erstem erfolgreichem OTA_Start()
byte SSID_Speicher[50][33];
byte SSID_Byte_arrey[33];
String NeueSSID;

void OTA_Stop()
{
  wifiReconnectEnabled = false; // Kein automatischer Reconnect nach manuellem Stop
  if (OTA_On == 1) // OTA aus
  {
    OTA_On = 0;
    ArduinoOTA.end();
    WiFi.disconnect();
    WiFi.mode(WIFI_OFF);
    CAN_SendEx(true, 1, IP_Send_to_CAN, 0x03);
  }
}

void OTA_Start()
{
  CAN_SendEx(true, 1, IP_Send_to_CAN, 0x01);

  if (OTA_On == 1) // Wenn schon an war und auf eine andere IP Adresse umgeschalten wird
  {
    OTA_On = 0;
    ArduinoOTA.end();
    WiFi.disconnect();
  }

  // SSID vom FS lesen und in persistenten Puffer kopieren
  String SSID_str = SSID_Lesen();
  SSID_str.toCharArray(ssid_buf, sizeof(ssid_buf));
  ssid = ssid_buf;

  // PASSWORD vom FS lesen und in persistenten Puffer kopieren
  String PASSWORD_str = PASSWORD_Lesen();
  PASSWORD_str.toCharArray(password_buf, sizeof(password_buf));
  password = password_buf;

  WiFi.hostname(WIFI_Name);

  WiFi.begin(ssid, password);
  WiFi.setTxPower(WIFI_POWER_19_5dBm);
  unsigned long wifiStart = millis();
  const unsigned long wifiTimeout = 20000; // 20s
  while (WiFi.status() != WL_CONNECTED && millis() - wifiStart < wifiTimeout)
  {
    delay(100);
  }
  if (WiFi.status() != WL_CONNECTED)
  {
    Serial.println("WiFi connect failed");
    CAN_SendEx(true, 1, IP_Send_to_CAN, 0x04);
    WiFi.disconnect();
    WiFi.mode(WIFI_OFF);
    return;
  }

  ArduinoOTA.begin();

  // Callbacks NACH begin() setzen
    ArduinoOTA.onStart([]() {
    Serial.println("OTA Start: Watchdog auf 60s erhöhen");
    CAN_SendEx(true, 1, IP_Send_to_CAN, 0x05);
    esp_task_wdt_init(60, true);
    esp_task_wdt_add(NULL);
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    // LED blau blinken lassen
    static unsigned long lastBlink = 0;
    if (millis() - lastBlink > 100) {
      leds[0] = (leds[0] == CRGB::Black) ? CRGB::Blue : CRGB::Black;
      FastLED.show();
      lastBlink = millis();
    }
    esp_task_wdt_reset();
  });

  ArduinoOTA.onEnd([]() {
    Serial.println("OTA Ende: Watchdog zurück auf 10s");
    incrementFirmwareVersion();
    CAN_SendEx(true, 1, IP_Send_to_CAN, 0x06);
    esp_task_wdt_init(10, true);
    esp_task_wdt_add(NULL);
  });

  ArduinoOTA.onError([](ota_error_t error)
                     {
    Serial.printf("OTA Fehler [%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
    CAN_SendEx(true, 1, IP_Send_to_CAN, 0x07);
    esp_task_wdt_init(10, true);
    esp_task_wdt_add(NULL); });

  OTA_On = 1;
  wifiReconnectEnabled = true; // Reconnect wieder erlaubt nach erfolgreichem Start
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  IPAddress ip = WiFi.localIP();
  CAN_SendEx(true, 5, IP_Send_to_CAN, 0x02, ip[0], ip[1], ip[2], ip[3]);
}

void WifiScan()
{
  Serial.println("Scan start");

  CAN_Send(Can_Input_Wifi, 0x01);

  byte n = WiFi.scanNetworks();
  Serial.println("Scan done");

  if (n == 0)
  {
    Serial.println("no networks found");
    CAN_Send(Can_Output_Wifi_Scan, 0x02);
  }
  else
  {
    if (n > 10)
    {
      n = 10;
    }

    Serial.print(n);

    CAN_Send(Can_Output_Wifi_Scan, 0x03, n);

    String SSID_gefunden;

    Serial.println(" networks found");
    Serial.println("SSID");
    for (int i = 0; i < n; ++i)
    {
      SSID_gefunden = WiFi.SSID(i);

      for (int a = SSID_gefunden.length(); a < 32; ++a)
      {
        SSID_gefunden += " ";
      }

      String message = SSID_gefunden;
      byte plain[message.length()];
      message.getBytes(plain, message.length() + 1);

      for (int b = 0; b < 32; ++b)
      {
        SSID_Speicher[i][b] = plain[b];
      }

      delay(100);
      CAN_Send(Can_Output_wifi_SSID_Scan_Daten1, i + 1, plain[0], plain[1], plain[2], plain[3], plain[4], plain[5], plain[6]);
      delay(100);
      CAN_Send(Can_Output_wifi_SSID_Scan_Daten2, i + 1, plain[7], plain[8], plain[9], plain[10], plain[11], plain[12], plain[13]);
      delay(100);
      CAN_Send(Can_Output_wifi_SSID_Scan_Daten3, i + 1, plain[14], plain[15], plain[16], plain[17], plain[18], plain[19], plain[20]);
      delay(100);
      CAN_Send(Can_Output_wifi_SSID_Scan_Daten4, i + 1, plain[21], plain[22], plain[23], plain[24], plain[25], plain[26], plain[27]);
      delay(100);
      CAN_Send(Can_Output_wifi_SSID_Scan_Daten5, i + 1, plain[28], plain[29], plain[30], plain[31]);

      Serial.println(SSID_gefunden);

      delay(10);
    }
  }

  WiFi.scanDelete();
  WiFi.disconnect();
  WiFi.mode(WIFI_OFF);
}

void WifiSsidAuswahl(byte wert)
{
  for (int b = 0; b < 32; ++b)
  {
    SSID_Byte_arrey[b] = SSID_Speicher[wert][b];
  }

  NeueSSID = String((char *)SSID_Byte_arrey);

  NeueSSID.trim();
  SSID_Schreiben(NeueSSID);
}

void Aktuelle_SSID_Senden()
{
  String SSID_gespeichert = SSID_Lesen();

  for (int a = SSID_gespeichert.length(); a < 32; ++a)
  {
    SSID_gespeichert += " ";
  }

  String message = SSID_gespeichert;
  byte plain[message.length()];
  message.getBytes(plain, message.length() + 1);

  delay(500);
  CAN_Send(Can_Output_wifi_SSID_Daten1, plain[0], plain[1], plain[2], plain[3], plain[4], plain[5], plain[6], plain[7]);
  delay(100);
  CAN_Send(Can_Output_wifi_SSID_Daten2, plain[8], plain[9], plain[10], plain[11], plain[12], plain[13], plain[14], plain[15]);
  delay(100);
  CAN_Send(Can_Output_wifi_SSID_Daten3, plain[16], plain[17], plain[18], plain[19], plain[20], plain[21], plain[22], plain[23]);
  delay(100);
  CAN_Send(Can_Output_wifi_SSID_Daten4, plain[24], plain[25], plain[26], plain[27], plain[28], plain[29], plain[30], plain[31]);
}

void Aktuelle_PASSWORT_Senden()
{
  String SSID_gespeichert = PASSWORD_Lesen();

  for (int a = SSID_gespeichert.length(); a < 32; ++a)
  {
    SSID_gespeichert += " ";
  }

  String message = SSID_gespeichert;
  byte plain[message.length()];
  message.getBytes(plain, message.length() + 1);

  delay(500);
  CAN_Send(Can_Output_wifi_Passwort_Daten1, plain[0], plain[1], plain[2], plain[3], plain[4], plain[5], plain[6], plain[7]);
  delay(100);
  CAN_Send(Can_Output_wifi_Passwort_Daten2, plain[8], plain[9], plain[10], plain[11], plain[12], plain[13], plain[14], plain[15]);
  delay(100);
  CAN_Send(Can_Output_wifi_Passwort_Daten3, plain[16], plain[17], plain[18], plain[19], plain[20], plain[21], plain[22], plain[23]);
  delay(100);
  CAN_Send(Can_Output_wifi_Passwort_Daten4, plain[24], plain[25], plain[26], plain[27], plain[28], plain[29], plain[30], plain[31]);
}

#endif