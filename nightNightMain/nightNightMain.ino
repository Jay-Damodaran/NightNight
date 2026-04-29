// NightNight Automatic Lights off firmware for XIAO ESP32-C3 MCU
// Wakes at 12 AM via DS3231, extends then retracts servo
// BATTLOW: flash red LED every 10 min using light sleep
// BATTHIGH: hold green LED on via RTC GPIO through deep sleep when device is fully charged
// Wiring shown in KiCAD schematic

#include <RTClib.h>
#include <ESP32Servo.h>
#include "esp_sleep.h"
#include "driver/gpio.h"
#include "WiFi.h"
#include "esp_bt.h"

// gpio pin definitions
#define RTC_INT_PIN 3
#define SERVO_PIN D3
#define BATTERY_PIN A0
#define GLED_PIN D8
#define RLED_PIN D2

// actuator timing macros for extension and retraction
#define EXTEND_DELAY_MS 800
#define RETRACT_DELAY_MS 800

// 3.265V real / 2 (divider) = 1632.5mV
#define BATT_LOW_MV 1633
#define BATT_FULL_MV 2050

// macros defining low battery led flash timing
#define FLASH_ON_US 400000ULL
#define FLASH_OFF_US 600000ULL
#define FLASH_CYCLES 120
#define SLEEP_8MIN_US 480000000ULL

// 12 AM alarm 
#define ALARM_HOUR 0
#define ALARM_MINUTE 0
#define ALARM_SECOND 0

// define rtc and servo/actuator
RTC_DS3231 rtc;
Servo actuatorServo;

// enum for tracking battery level
enum BatteryLvls { BATTLOW, BATTMED, BATTHIGH };

// function prototypes for all functions used
void disableUnusedPeripherals();
void initRTC();
void setNextAlarm();
void clearAlarmFlag();
void runActuatorTask();
uint8_t batteryLevel();
void enterDeepSleep();
void lightSleepUs(uint64_t us);
void runBatteryLoop();

// Runs on every boot and every deep sleep wake
void setup() {
    // shuf off wifi and bluetooth
    disableUnusedPeripherals();
    // configure RTC
    initRTC();

    // configure led pins, turn off, and stop any holds that could have persisted through deep sleep
    pinMode(RLED_PIN, OUTPUT);
    digitalWrite(RLED_PIN, LOW);
    gpio_deep_sleep_hold_dis();
    gpio_hold_dis((gpio_num_t)GLED_PIN);
    pinMode(GLED_PIN, OUTPUT);
    digitalWrite(GLED_PIN, LOW);

    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();

    // go back to sleep if wake didn't happend from RTC interrupt
    if (cause != ESP_SLEEP_WAKEUP_EXT0) {
        setNextAlarm();
        enterDeepSleep();
        return;
    }

    // acknowledge alarm
    clearAlarmFlag();
    // extend and retract actuator to turn off light switch
    runActuatorTask();
    // assess battery level and flash red led if battery low and turn on green if battery ful
    runBatteryLoop();
    // set RTC alarm based on the alarm timing macros
    setNextAlarm();
    // hold green led on through deep sleep if battery level is high, so user can uplug device
    if (batteryLevel() == BATTHIGH) {
        gpio_hold_en((gpio_num_t)GLED_PIN);
        gpio_deep_sleep_hold_en();
    }
    // deep sleep for power conservation
    enterDeepSleep();
}

void loop() {}

void disableUnusedPeripherals() {
    WiFi.mode(WIFI_OFF);
    btStop();
    esp_bt_controller_disable();
}

void initRTC() {
    if (!rtc.begin()) {
        while (true) { delay(1000); }
    }
    if (rtc.lostPower()) {
        rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    }
    rtc.disable32K(); // pin not used
    rtc.clearAlarm(1);
    rtc.clearAlarm(2);
    rtc.writeSqwPinMode(DS3231_OFF); // required before setAlarm1 to stop oscillations in this line
    rtc.disableAlarm(2);
}

void setNextAlarm() {
    DateTime now = rtc.now();
    DateTime next = DateTime(
        now.year(), now.month(), now.day(),
        ALARM_HOUR, ALARM_MINUTE, ALARM_SECOND
    );
    if (now >= next) { // set alarm for next day if time has already passed
        next = next + TimeSpan(1, 0, 0, 0); 
    }
    rtc.setAlarm1(next, DS3231_A1_Hour); 
}

void clearAlarmFlag() {
    rtc.clearAlarm(1);
}

// extend and retract servo
void runActuatorTask() {
    actuatorServo.attach(SERVO_PIN);
    delay(200);
    actuatorServo.write(180);
    delay(EXTEND_DELAY_MS);
    actuatorServo.write(0);
    delay(RETRACT_DELAY_MS);
    actuatorServo.detach();
}

// battery level gauged from average of 5 samples read from voltage divider connected to A0
uint8_t batteryLevel() {
    uint32_t mv = 0;
    for (uint8_t i = 0; i < 5; i++) {
        mv += analogReadMilliVolts(BATTERY_PIN);
    }
    mv /= 5;
    if (mv < BATT_LOW_MV) {
        return BATTLOW;
    }
    if (mv >= BATT_FULL_MV) {
        return BATTHIGH;
    }
    return BATTMED;
}

void enterDeepSleep() {
    esp_deep_sleep_enable_gpio_wakeup(1ULL << RTC_INT_PIN, ESP_GPIO_WAKEUP_GPIO_LOW);
    esp_deep_sleep_start();
}

void lightSleepUs(uint64_t us) {
    esp_sleep_enable_timer_wakeup(us);
    esp_light_sleep_start();
}

// 10-min cycle: flash red LED for 2 min, light sleep 8 min
// turn on green led if battery level is high
void runBatteryLoop() {
    while (batteryLevel() == BATTLOW) {
        // each cycle is 1s, so 120 loop iterations to reach 2 min
        for (int cycle = 0; cycle < FLASH_CYCLES; cycle++) {
            digitalWrite(RLED_PIN, HIGH);
            lightSleepUs(FLASH_ON_US);
            digitalWrite(RLED_PIN, LOW);
            lightSleepUs(FLASH_OFF_US);
        }
        lightSleepUs(SLEEP_8MIN_US);
    }
    if (batteryLevel() == BATTHIGH) {
        digitalWrite(GLED_PIN, HIGH);
    }
}
