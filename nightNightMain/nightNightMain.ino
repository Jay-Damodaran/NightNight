// ============================================================
//  Actuator firmware — XIAO ESP32-C3
//  Wakes at 12 AM via DS3231, extends then retracts a
//  Kitronik linear actuator (FS90 servo base).
//  Checks battery after task. If below 15% threshold,
//  enters low-battery alert loop (flash LED every 10 min)
//  using light sleep to preserve RAM state.
// ============================================================
//
//  Pin assignments
//    GPIO3  — DS3231 SQW/INT  (ext0 deep-sleep wakeup, active LOW)
//    GPIO5  — Servo PWM signal (ESP32Servo)
//    A0     — Battery voltage via 100k+100k divider
//    D8    — RED LED + 330Ω resistor to GND (low battery alert)
//    GPIO4  — GREEN LED + 330Ω resistor to GND (battery full, RTC GPIO)
//
//  I2C (DS3231)
//    GPIO6  — SDA
//    GPIO7  — SCL
//
//  Battery threshold
//    Real cutoff : 3.265 V  (15% of 3.7V LiPo range)
//    At divider  : 1632.5 mV → compare at 1633 mV
//    analogReadMilliVolts() returns the divided voltage directly
//
//  Servo travel
//    write(0)   = fully retracted
//    write(180) = fully extended (20 mm)
//    Tune EXTEND_DELAY_MS / RETRACT_DELAY_MS on real hardware
//
//  Low-battery cycle (light sleep, RAM preserved)
//    400 ms LED on  → light sleep
//    600 ms LED off → light sleep
//    × 120 cycles   = 2 min flash window
//    8 min light sleep
//    Repeat forever (10 min total cycle)
// ============================================================

#include <Wire.h>
#include <RTClib.h>          // Adafruit RTClib
#include <ESP32Servo.h>      // ESP32-specific servo library
#include "esp_sleep.h"
#include "driver/adc.h"
#include "driver/gpio.h"
#include "WiFi.h"
#include "esp_bt.h"

// ── Pin definitions ─────────────────────────────────────────
#define RTC_INT_PIN       3   // DS3231 SQW → ext0 wakeup
#define SERVO_PIN         D3            // GPIO5
#define BATTERY_PIN       A0           // GPIO0
#define GLED_PIN          D8          // GPIO10
#define RLED_PIN          D2          // GPIO4 (RTC GPIO, holds state in deep sleep)

// ── Servo timing (ms) — tune after physical testing ─────────
#define EXTEND_DELAY_MS   800
#define RETRACT_DELAY_MS  800

// ── Battery threshold ────────────────────────────────────────
// 3.265 V real / 2 (divider) = 1632.5 mV → round up to 1633
#define BATT_LOW_MV       1633
#define BATT_FULL_MV      2050

// ── Low-battery flash parameters ────────────────────────────
#define FLASH_ON_US       400000ULL    //  400 ms in microseconds
#define FLASH_OFF_US      600000ULL    //  600 ms in microseconds
#define FLASH_CYCLES      120          //  120 × 1s = 2 min window
#define SLEEP_8MIN_US     480000000ULL //  8 min in microseconds

// ── Alarm time ───────────────────────────────────────────────
#define ALARM_HOUR        0            //  12 AM
#define ALARM_MINUTE      0
#define ALARM_SECOND      0

// ── Objects ──────────────────────────────────────────────────
RTC_DS3231 rtc;
Servo      actuatorServo;

// -- Battery Level Enum
enum BatteryLvls {BATTLOW, BATTMED, BATTHIGH};

// ============================================================
//  Forward declarations
// ============================================================
void     disableUnusedPeripherals();
void     initRTC();
void     setNextAlarm();
void     clearAlarmFlag();
void     runActuatorTask();
uint8_t     batteryLevel();
void     enterDeepSleep();
void     lightSleepUs(uint64_t us);
void     runBatteryLoop();

// ============================================================
//  setup() — re-runs on every boot AND every deep sleep wake
// ============================================================
void setup() {
    disableUnusedPeripherals();

    // I2C for DS3231
    initRTC();

    // Configure output pins
    pinMode(RLED_PIN, OUTPUT);
    digitalWrite(RLED_PIN, LOW);
    gpio_deep_sleep_hold_dis();
    gpio_hold_dis((gpio_num_t)GLED_PIN);
    pinMode(GLED_PIN, OUTPUT);
    digitalWrite(GLED_PIN, LOW);

    // ── Check why we woke up ─────────────────────────────────
    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();

    if (cause != ESP_SLEEP_WAKEUP_EXT0) {
        // First boot or unexpected reset — set alarm and sleep
        setNextAlarm();
        enterDeepSleep();
        return;  // never reached, but satisfies compiler
    }

    // ── Woken by DS3231 at 12 AM ─────────────────────────────
    clearAlarmFlag();
    runActuatorTask();

    // Run low-battery alert loop
    runBatteryLoop();

    // Normal path: re-arm alarm and deep sleep until next 12 AM
    if (batteryLevel() == BATTHIGH) {
        setNextAlarm();
        gpio_hold_en((gpio_num_t)GLED_PIN);
        gpio_deep_sleep_hold_en();
        enterDeepSleep();
    }
    else {
        setNextAlarm();
        enterDeepSleep();
    }
}

// loop() is never reached in normal operation
void loop() {}

// ============================================================
//  Peripheral management
// ============================================================
void disableUnusedPeripherals() {
    // Disable WiFi
    WiFi.mode(WIFI_OFF);

    // Disable Bluetooth
    btStop();
    esp_bt_controller_disable();

    // Disable ADC interrupt / ULP (not using them)
    // GPIO and I2C remain active as needed
}

// ============================================================
//  RTC initialisation
// ============================================================
void initRTC() {
    if (!rtc.begin()) {
        // RTC not found — nothing we can do without it.
        // In production consider flashing LED SOS here.
        while (true) { delay(1000); }
    }

    if (rtc.lostPower()) {
        // Set RTC to compile time — flash within a minute
        // of compiling for best accuracy.
        rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    }

    // Ensure both alarms are cleared on fresh start
    rtc.clearAlarm(1);
    rtc.clearAlarm(2);
    rtc.disableAlarm(2);  // only using alarm 1
}

// ============================================================
//  Alarm management
// ============================================================
void setNextAlarm() {
    DateTime now  = rtc.now();
    DateTime next = DateTime(
        now.year(), now.month(), now.day(),
        ALARM_HOUR, ALARM_MINUTE, ALARM_SECOND
    );

    // If 12 AM today has already passed, target tomorrow
    if (now >= next) {
        next = next + TimeSpan(1, 0, 0, 0);
    }

    // Alarm 1: match hours, minutes, seconds (daily repeat)
    rtc.setAlarm1(next, DS3231_A1_Hour);
}

void clearAlarmFlag() {
    rtc.clearAlarm(1);
    // SQW/INT pin releases HIGH after clear — safe to sleep again
}

// ============================================================
//  Actuator task — extend then retract
// ============================================================
void runActuatorTask() {
    actuatorServo.attach(SERVO_PIN);

    // Extend fully
    actuatorServo.write(180);
    delay(EXTEND_DELAY_MS);

    // Retract fully
    actuatorServo.write(0);
    delay(RETRACT_DELAY_MS);

    // Detach — stops PWM signal, servo goes passive
    // Prevents current draw fighting any backdrive load
    actuatorServo.detach();
}

// ============================================================
//  Battery check
// ============================================================
uint8_t batteryLevel() {
    // analogReadMilliVolts uses factory eFuse calibration.
    // Voltage divider (10k + 10k) halves real LiPo voltage.
    // Real threshold 3265 mV -> 1633 mV at ADC pin.
    uint32_t mv = 0;
    for(uint8_t i = 0; i < 5; i++) {
        mv += analogReadMilliVolts(BATTERY_PIN);
    }
    mv /= 5;
    if (mv < BATT_LOW_MV) {
        return BATTLOW;
    }
    else if (mv >= BATT_FULL_MV) {
        return BATTHIGH;
    }
    return BATTMED;
}

// ============================================================
//  Deep sleep — full reboot on wake, setup() re-runs
// ============================================================
void enterDeepSleep() {
    // DS3231 SQW pulls LOW on alarm → wake on falling edge
    esp_deep_sleep_enable_gpio_wakeup(1ULL << RTC_INT_PIN, ESP_GPIO_WAKEUP_GPIO_LOW);
    esp_deep_sleep_start();
}

// ============================================================
//  Light sleep helper — RAM, GPIO, and variables preserved
// ============================================================
void lightSleepUs(uint64_t us) {
    esp_sleep_enable_timer_wakeup(us);
    esp_light_sleep_start();
}

// ============================================================
//  Low-battery alert loop
//  10-minute cycle:
//    2 min  — flash LED (400ms on / 600ms off × 120)
//    8 min  — light sleep
// ============================================================
void runBatteryLoop() {
    while (batteryLevel() == BATTLOW) {
        // ── 2-minute flash window ────────────────────────────
        for (int cycle = 0; cycle < FLASH_CYCLES; cycle++) {
            // LED on — GPIO state persists through light sleep
            digitalWrite(RLED_PIN, HIGH);
            lightSleepUs(FLASH_ON_US);

            // LED off
            digitalWrite(RLED_PIN, LOW);
            lightSleepUs(FLASH_OFF_US);
        }

        // ── 8-minute rest ────────────────────────────────────
        lightSleepUs(SLEEP_8MIN_US);
    }
    if (batteryLevel() == BATTHIGH) {
        digitalWrite(GLED_PIN, HIGH);
    }
}
