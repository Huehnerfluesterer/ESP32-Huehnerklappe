#pragma once

// ==================================================
// PINS – Hühnerklappe ESP32-S3 DevKitC-1 N16R8
// ==================================================
// Gesperrte GPIOs auf ESP32-S3 N16R8:
//   GPIO 22–25  → existieren NICHT im Chip
//   GPIO 26–32  → interner Quad-SPI Flash
//   GPIO 33–37  → interner Octal-SPI PSRAM (OPI)
//   GPIO 19, 20 → Native USB D+/D–
// ==================================================

// Motor (L298N)
#define MOTOR_IN1       15   // war ESP32: 25 (existiert nicht!)
#define MOTOR_IN2        5   // war 11, Boot-Glitch   // war ESP32: 26 (Flash-Pin)
#define MOTOR_ENA       13   // war ESP32: 27 (Flash-Pin) – PWM via LEDC

// Relais
#define RELAIS_PIN           18   // Locklicht-Relais (LOW-aktiv)
#define STALLLIGHT_RELAY_PIN 10   // war ESP32: 19 (USB D–)

// Taster
#define BUTTON_PIN       39   // war ESP32: 33 – INPUT_PULLUP
#define STALL_BUTTON_PIN 41   // war ESP32: 32 – INPUT_PULLUP
#define RED_BUTTON_PIN   40   // war ESP32: 35 – INPUT_PULLUP

// ACS712 Stromsensor
#define ACS712_PIN          7    // war ESP32: 34 (Flash-Pin) – ADC1_CH6
#define ACS712_MV_PER_A     185
#define ACS712_VCC          5.0f
#define ACS712_ZERO_V       2.5f
#define BLOCKADE_THRESHOLD_A  2.0f
#define ACS712_HAS_DIVIDER  0

// I2C – GPIO 22–25 existieren auf dem ESP32-S3 NICHT!
#define I2C_SDA         8    // war ESP32: 21
#define I2C_SCL         9    // war ESP32: 22

// Endschalter
#define LIMIT_OPEN_PIN  14
#define LIMIT_CLOSE_PIN 12

// RGB LED-Streifen (12V, N-Kanal MOSFETs)
#define RGB_PIN_R       4
#define RGB_PIN_G      16
#define RGB_PIN_B      17
#define RGB_PIN_W      21    // war ESP32: 23 (existiert nicht!)
#define RGB_FREQ     1000
#define RGB_BITS        8

// Relais-Logik (aktiv LOW)
#define RELAY_ON  LOW
#define RELAY_OFF HIGH