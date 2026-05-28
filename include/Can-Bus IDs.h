
#ifndef CAN_IDS_H
#define CAN_IDS_H

// =============================================================================
// CAN-Pakete Solar (gesendet bei jedem vollständigen VE.Direct TEXT-Frame):
//
// MessageBasisID  (solar_links: 0x4E2 | solar_rechts: 0x500)  DLC=6
//   Byte 0-1 : Batteriespannung    [mV]      int16  (z.B. 12800 = 12,8V)
//   Byte 2-3 : Ladestrom           [0,1A]    int16  (z.B.  15  =  1,5A)
//   Byte 4-5 : Panel-Leistung      [W]       int16  (z.B. 120  = 120W)
//
// MessageBasisID1 (solar_links: 0x4E3 | solar_rechts: 0x501)  DLC=6
//   Byte 0-1 : Panel-Spannung      [mV]      int16  (z.B. 17500 = 17,5V)
//   Byte 2-3 : Panel-Strom (PPV/VPV) [0,1A]  int16  (berechnet, da kein direktes IL-Feld)
//   Byte 4-5 : Tageszähler HSDS    [-]       int16  (Tage seit Inbetriebnahme)
//
// MessageSensorID (solar_links: 0x4E6 | solar_rechts: 0x504) DLC=3
//   Byte 0 : DS18B20 Temp          [(T[degC] + 50)]       uint8, 0xFF = ungültig
//   Byte 1 : DHT22 Temp            [(T[degC] + 50)]       uint8, 0xFF = ungültig
//   Byte 2 : DHT22 Feuchte         [0..100 %]             uint8, 0xFF = ungültig
//
// Can_Solar_Cmd  (solar_links: 0x4E4 | solar_rechts: 0x502)  DLC=1-3  (Eingehend)
//   Byte 0 = 0x10 : Absorption-Spannung lesen
//   Byte 0 = 0x11 : Absorption-Spannung setzen  (Byte 1-2 = Spannung * 100, uint16 BE)
//   Byte 0 = 0x12 : Float-Spannung lesen
//   Byte 0 = 0x13 : Float-Spannung setzen        (Byte 1-2 = Spannung * 100, uint16 BE)
//   Byte 0 = 0x14 : Regler einschalten
//   Byte 0 = 0x15 : Regler ausschalten
//   Byte 0 = 0x7E : Schreibzugriff freigeben (Byte 1=0xA5, Byte 2=0x5A; 60s gültig)
//
// Can_Solar_Resp (solar_links: 0x4E5 | solar_rechts: 0x503)  DLC=2-4  (Ausgehend)
//   Byte 0     : Cmd-Echo (gespiegelter Befehl)
//   Byte 1     : 0x00 = OK / 0x01 = Fehler / 0x02 = gesperrt / 0x03 = ungültiger Wert
//   Byte 2-3   : Spannung * 100, uint16 BE  (nur bei Lese-Antwort, 0x10 / 0x12)
//
// Heartbeat alle 30s:
//   IP_Send_to_CAN+1               DLC=1     Byte 0: 0xAA
// =============================================================================

#if defined(SOLAR_LINKS)

constexpr char WIFI_Name[] = "Solar links";
constexpr uint8_t OtaDevieID = 0x0B;        // 11 dezimal

constexpr uint16_t MessageBasisID = 0x4E2;  // 1250 dezimal
constexpr uint16_t MessageBasisID1 = 0x4E3; // 1251 dezimal
constexpr uint16_t Can_Solar_Cmd = 0x4E4;   // 1252 dezimal
constexpr uint16_t Can_Solar_Resp = 0x4E5;  // 1253 dezimal
constexpr uint16_t MessageSensorID = 0x4E6; // 1254 dezimal
constexpr uint16_t OTA_On_Off = 0x4EB;      // 1259 dezimal
constexpr uint16_t IP_Send_to_CAN = 0x4EC;  // 1260 dezimal

#elif defined(SOLAR_RECHTS)

constexpr char WIFI_Name[] = "Solar rechts";
constexpr uint8_t OtaDevieID = 0x0C;        // 12 dezimal

constexpr uint16_t MessageBasisID = 0x500;  // 1280 dezimal
constexpr uint16_t MessageBasisID1 = 0x501; // 1281 dezimal
constexpr uint16_t Can_Solar_Cmd = 0x502;   // 1282 dezimal
constexpr uint16_t Can_Solar_Resp = 0x503;  // 1283 dezimal
constexpr uint16_t MessageSensorID = 0x504; // 1284 dezimal
constexpr uint16_t OTA_On_Off = 0x509;      // 1289 dezimal
constexpr uint16_t IP_Send_to_CAN = 0x50A;  // 1290 dezimal

#else

#error "Bitte SOLAR_LINKS oder SOLAR_RECHTS als build_flag definieren!"

#endif

// Can-Bus IDs für die OTA-Übertragung von SSID und Passwort
constexpr uint16_t Can_Input_Wifi = 3000;
constexpr uint16_t Can_Output_Wifi_Scan = (Can_Input_Wifi + 1);
constexpr uint16_t Can_Output_wifi_SSID_Scan_Daten1 = (Can_Input_Wifi + 2);
constexpr uint16_t Can_Output_wifi_SSID_Scan_Daten2 = (Can_Input_Wifi + 3);
constexpr uint16_t Can_Output_wifi_SSID_Scan_Daten3 = (Can_Input_Wifi + 4);
constexpr uint16_t Can_Output_wifi_SSID_Scan_Daten4 = (Can_Input_Wifi + 5);
constexpr uint16_t Can_Output_wifi_SSID_Scan_Daten5 = (Can_Input_Wifi + 6);

constexpr uint16_t Can_Output_wifi_SSID_Daten1 = (Can_Input_Wifi + 10);
constexpr uint16_t Can_Output_wifi_SSID_Daten2 = (Can_Input_Wifi + 11);
constexpr uint16_t Can_Output_wifi_SSID_Daten3 = (Can_Input_Wifi + 12);
constexpr uint16_t Can_Output_wifi_SSID_Daten4 = (Can_Input_Wifi + 13);

constexpr uint16_t Can_Output_wifi_Passwort_Daten1 = (Can_Input_Wifi + 14);
constexpr uint16_t Can_Output_wifi_Passwort_Daten2 = (Can_Input_Wifi + 15);
constexpr uint16_t Can_Output_wifi_Passwort_Daten3 = (Can_Input_Wifi + 16);
constexpr uint16_t Can_Output_wifi_Passwort_Daten4 = (Can_Input_Wifi + 17);

#endif