# CAN BUS Smart Solar

ESP32-basiertes Gateway zwischen Victron SmartSolar MPPT und einem CAN-Bus-Netzwerk (250 kbit/s).  
Liest Solardaten per VE.Direct (TEXT-Protokoll, 19200 Baud) und sendet sie zyklisch als CAN-Pakete.  
Ladespannungen (Absorption / Float) können zusätzlich per VE.Direct HEX-Protokoll gelesen und gesetzt werden – auch über CAN.

---

## Hardware

| Komponente | Beschreibung |
|---|---|
| ESP32 | LilyGO T-CAN485 (ESP32, CAN-Transceiver + RS485 onboard) |
| CAN-Transceiver | WeAct ISOCANFD V1 (CA-IS2062A, galvanisch getrennt) – ersetzt den onboard Transceiver |
| Solar-Regler | Victron SmartSolar MPPT (getestet: 75/15, kompatibel: 100/50) |
| Spannungsversorgung | 5–36 V (z.B. AJ39 DC/DC für 24V-Fahrzeugeinsatz) |
| LED | 1× WS2812 (Status-Anzeige) |

### Pin-Belegung

| Funktion | GPIO |
|---|---|
| CAN TX | 27 |
| CAN RX | 26 |
| CAN SE (Transceiver Enable) | 23 |
| VE.Direct RX (Serial2) | 25 |
| VE.Direct TX (Serial2) | 18 |
| WS2812 LED | 4 |

---

## Build-Konfiguration (PlatformIO)

Es gibt zwei Environments:

| Environment | Build-Flag | OTA-IP |
|---|---|---|
| `solar_links` | `-DSOLAR_LINKS` | 172.24.114.197 |
| `solar_rechts` | `-DSOLAR_RECHTS` | 172.24.114.198 |

Upload erfolgt per **OTA** (ArduinoOTA).

---

## CAN-Protokoll (250 kbit/s, Extended Frames)

Alle Pakete werden bei jedem vollständigen VE.Direct TEXT-Frame gesendet (~1×/Sekunde).

### Messdaten

| CAN-ID | solar_links | solar_rechts | DLC | Inhalt |
|---|---|---|---|---|
| `MessageBasisID` | 0x4E2 | 0x500 | 6 | Byte 0-1: Batteriespannung [mV] · Byte 2-3: Ladestrom [0,1A] · Byte 4-5: Panel-Leistung [W] |
| `MessageBasisID1` | 0x4E3 | 0x501 | 6 | Byte 0-1: Panel-Spannung [mV] · Byte 2-3: Panel-Strom (PPV/VPV, berechnet) [0,1A] · Byte 4-5: Tageszähler HSDS |

### Heartbeat

| CAN-ID | solar_links | solar_rechts | DLC | Inhalt |
|---|---|---|---|---|
| `IP_Send_to_CAN+1` | 0x4ED | 0x50B | 1 | Byte 0: 0xAA (alle 30 s) |

### Ladespannungen lesen/setzen

Eingehend auf `Can_Solar_Cmd`, Antwort auf `Can_Solar_Resp`:

| CAN-ID | solar_links | solar_rechts |
|---|---|---|
| `Can_Solar_Cmd` | 0x4E4 | 0x502 |
| `Can_Solar_Resp` | 0x4E5 | 0x503 |

**Befehlsformat (Can_Solar_Cmd):**

| Byte 0 | Byte 1-2 | Funktion |
|---|---|---|
| 0x10 | – | Absorption-Spannung lesen |
| 0x11 | Spannung × 100, uint16 BE | Absorption-Spannung setzen |
| 0x12 | – | Float-Spannung lesen |
| 0x13 | Spannung × 100, uint16 BE | Float-Spannung setzen |

**Antwortformat (Can_Solar_Resp):**

| Byte 0 | Byte 1 | Byte 2-3 |
|---|---|---|
| Cmd-Echo | 0x00 = OK / 0x01 = Fehler | Spannung × 100, uint16 BE (nur bei Lese-Antwort) |

Beispiel: Float auf 27,2 V setzen → `0x13 0x0A 0xA0` (2720 = 0x0AA0)

> **Hinweis:** Beim Setzen von Ladespannungen wird intern automatisch der Battery-Typ auf „User defined" (Register 0xEDF1 = 0xFF) gesetzt, sonst quittiert der Regler mit Error 119.

---

## VE.Direct HEX-Register (BlueSolar/SmartSolar MPPT)

| Register | Funktion | Skala |
|---|---|---|
| 0xEDF7 | Absorption-Spannung | 0,01 V (un16) |
| 0xEDF6 | Float-Spannung | 0,01 V (un16) |
| 0xEDF1 | Battery type (0xFF = User defined) | – |
| 0xEDF0 | Max. Ladestrom | 0,1 A (un16) |

> Panel-Strom (0xEDBD) ist bei 10/15/20A-Reglern laut Victron nicht verfügbar (Note 1). Panel-Strom wird daher aus PPV/VPV berechnet.

---

## Serielles Debug-Menü

Über Serial (115200 Baud) steht ein Menü zur Verfügung. Eingabe von `?` oder `help` zeigt alle Befehle.

Wichtige Befehle:

| Befehl | Funktion |
|---|---|
| `live` | Live-Datenausgabe ein/aus |
| `abs` | Absorption-Spannung lesen |
| `abs 28.8` | Absorption-Spannung auf 28,8 V setzen |
| `float` | Float-Spannung lesen |
| `float 27.2` | Float-Spannung auf 27,2 V setzen |
| `current <A>` | Max. Ladestrom setzen |
| `yield` | Ertrag heute und gestern |
| `status` | Regler-Status |

---

## OTA-Update

OTA wird über CAN gestartet/gestoppt:

| CAN-ID | Byte 0 | Byte 1 | Funktion |
|---|---|---|---|
| 0x25A | 0x01 | 0x01 | OTA starten |
| 0x25A | 0x01 | 0x00 | OTA stoppen |

Nach dem ersten erfolgreichen OTA-Start wird automatischer WiFi-Reconnect aktiviert (alle 60 s).

---

## LED-Status

| Farbe | Bedeutung |
|---|---|
| Rot | Boot / Initialisierung |
| Grün | Normalbetrieb |
| Schwarz (aus) | OTA aktiv |
| Grün blinkend | Neue Solardaten empfangen |
