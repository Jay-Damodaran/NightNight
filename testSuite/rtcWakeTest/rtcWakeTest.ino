// ============================================================
//  RTC Wake Test — XIAO ESP32-C3
//  Sets DS3231 alarm to 1 minute from compile time, deep
//  sleeps, then confirms the MCU woke via EXT0 by blinking
//  the LED 3 times. No actuator involved.
//
//  Pin assignments
//    GPIO3  — DS3231 SQW/INT  (ext0 deep-sleep wakeup, active LOW)
//    D10    — LED + 220Ω resistor to GND (wake confirmation)
//
//  I2C (DS3231)
//    GPIO6  — SDA
//    GPIO7  — SCL
// ============================================================

#include <Wire.h>
#include <RTClib.h>          // Adafruit RTClib
#include "esp_sleep.h"
#include "WiFi.h"
#include "esp_bt.h"

// ── Pin definitions ─────────────────────────────────────────
#define RTC_INT_PIN       GPIO_NUM_3   // DS3231 SQW → ext0 wakeup
#define LED_PIN           D10          // GPIO10

// ── Objects ──────────────────────────────────────────────────
RTC_DS3231 rtc;

// ── Forward declarations ─────────────────────────────────────
void disableUnusedPeripherals();
void initRTC();
void setAlarmOneMinuteFromNow();
void clearAlarmFlag();
void confirmWakeWithLED();
void enterDeepSleep();

// ============================================================
//  setup() — re-runs on every boot AND every deep sleep wake
// ============================================================
void setup() {
    disableUnusedPeripherals();

    // I2C for DS3231
    Wire.begin(6, 7);  // SDA=GPIO6, SCL=GPIO7

    initRTC();

    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);

    // ── Check why we woke up ─────────────────────────────────
    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();

    if (cause != ESP_SLEEP_WAKEUP_EXT0) {
        // First boot — set alarm 1 minute from compile time and sleep
        setAlarmOneMinuteFromNow();
        enterDeepSleep();
        return;  // never reached, but satisfies compiler
    }

    // ── Woken by DS3231 alarm ────────────────────────────────
    clearAlarmFlag();
    confirmWakeWithLED();

    // Test complete — sleep without re-arming alarm
    enterDeepSleep();
}

void loop() {}

// ============================================================
//  Peripheral management
// ============================================================
void disableUnusedPeripherals() {
    WiFi.mode(WIFI_OFF);
    btStop();
    esp_bt_controller_disable();
}

// ============================================================
//  RTC initialisation
// ============================================================
void initRTC() {
    if (!rtc.begin()) {
        while (true) { delay(1000); }
    }

    if (rtc.lostPower()) {
        rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    }

    rtc.clearAlarm(1);
    rtc.clearAlarm(2);
    rtc.disableAlarm(2);  // only using alarm 1
}

// ============================================================
//  Alarm management — 1 minute from compile time
// ============================================================
void setAlarmOneMinuteFromNow() {
    DateTime now  = rtc.now();
    DateTime next = now + TimeSpan(0, 0, 1, 0);  // +1 minute

    // Alarm 1: match hours, minutes, seconds
    rtc.setAlarm1(next, DS3231_A1_Hour);
}

void clearAlarmFlag() {
    rtc.clearAlarm(1);
    // SQW/INT pin releases HIGH after clear — safe to sleep again
}

// ============================================================
//  Wake confirmation — blink LED 3 times to signal EXT0 wake
// ============================================================
void confirmWakeWithLED() {
    for (int i = 0; i < 3; i++) {
        digitalWrite(LED_PIN, HIGH);
        delay(300);
        digitalWrite(LED_PIN, LOW);
        delay(300);
    }
}

// ============================================================
//  Deep sleep — full reboot on wake, setup() re-runs
// ============================================================
void enterDeepSleep() {
    // DS3231 SQW pulls LOW on alarm → wake on falling edge
    esp_deep_sleep_enable_gpio_wakeup(1ULL << RTC_INT_PIN, ESP_GPIO_WAKEUP_GPIO_LOW);
    esp_deep_sleep_start();
}
