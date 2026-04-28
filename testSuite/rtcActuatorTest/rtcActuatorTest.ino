// RTC Interrupt Actuator Test — XIAO ESP32-C3
// Fires actuator once via DS3231 alarm interrupt, 30s after compile time.
//
// Pin assignments
//   D1 (GPIO3) — DS3231 SQW/INT
//   D3 (GPIO5) — Servo PWM
//   D8 (GPIO8) — LED indicator (on = waiting, off = done)
//   GPIO6      — SDA
//   GPIO7      — SCL

#include <RTClib.h>
#include <ESP32Servo.h>

#define RTC_INT_PIN      D1
#define SERVO_PIN        D3
#define EXTEND_DELAY_MS  800
#define RETRACT_DELAY_MS 800

RTC_DS3231 rtc;
Servo      actuatorServo;

volatile bool alarmFired = false;
bool          taskDone   = false;

void IRAM_ATTR onAlarm() {
    alarmFired = true;
}

void setup() {
    Serial.begin(9600);
    delay(1000);

    pinMode(D8, OUTPUT);
    digitalWrite(D8, LOW);

    if (!rtc.begin()) {
        Serial.println("Couldn't find RTC!");
        while (true) { delay(100); }
    }

    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));

    rtc.disable32K();
    rtc.clearAlarm(1);
    rtc.clearAlarm(2);
    rtc.writeSqwPinMode(DS3231_OFF);
    rtc.disableAlarm(2);

    pinMode(RTC_INT_PIN, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(RTC_INT_PIN), onAlarm, FALLING);

    if (!rtc.setAlarm1(rtc.now() + TimeSpan(30), DS3231_A1_Second)) {
        Serial.println("Error, alarm wasn't set!");
    } else {
        char buf[10];
        strcpy(buf, "hh:mm:ss");
        rtc.now().toString(buf);
        Serial.print("Now:    "); Serial.println(buf);

        strcpy(buf, "hh:mm:ss");
        rtc.getAlarm1().toString(buf);
        Serial.print("Target: "); Serial.println(buf);
    }
}

void loop() {
    if (taskDone) return;

    if (alarmFired) {
        alarmFired = false;
        rtc.clearAlarm(1);
        rtc.disableAlarm(1);

        actuatorServo.attach(SERVO_PIN);
        delay(200);
        actuatorServo.write(180);
        delay(EXTEND_DELAY_MS);
        actuatorServo.write(0);
        delay(RETRACT_DELAY_MS);
        actuatorServo.detach();

        digitalWrite(D8, HIGH);
        taskDone = true;
    }
}
