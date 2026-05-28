#ifndef CONFIG_H
#define CONFIG_H

#include <FastLED.h>

// PIN
#define PIN_5V_EN 16

#define CAN_TX_PIN 27
#define CAN_RX_PIN 26
#define CAN_SE_PIN 23

#define RS485_EN_PIN 17 // 17 /RE
#define RS485_TX_PIN 22 // 21
#define RS485_RX_PIN 21 // 22
#define RS485_SE_PIN 19 // 22 /SHDN

#define SD_MISO_PIN 2
#define SD_MOSI_PIN 15
#define SD_SCLK_PIN 14
#define SD_CS_PIN 13

#define LED_PIN 4
#define NUM_LEDS 1
extern CRGB leds[NUM_LEDS];

#define RX_PIN 25 // UART RX
#define TX_PIN 18 // UART TX

#define DHT22_PIN 32
#define DS18B20_PIN 33

#define BAUD_RATE 19200

#endif