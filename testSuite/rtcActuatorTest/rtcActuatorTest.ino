// RTC Actuator Test — XIAO ESP32-C3
// Sets DS3231 to compile time, fires actuator 2 minutes later. No sleep.
//
// Pin assignments
//   GPIO5 — Servo PWM (ESP32Servo)
//   GPIO6 — SDA
//   GPIO7 — SCL

#include <Wire.h>
#include <RTClib.h>
#include <ESP32Servo.h>

#define SERVO_PIN        D3
#define EXTEND_DELAY_MS  800
#define RETRACT_DELAY_MS 800

RTC_DS3231 rtc;
Servo      actuatorServo;
DateTime   target;
bool       taskDone = false;

void setup() {
    Serial.begin(9600);
    delay(1000);
    pinMode(D8, OUTPUT);
    digitalWrite(D8, LOW);

    if (!rtc.begin()) {
        Serial.println("Failed to connect");
        while (true) { delay(1000); }
    }

    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    target = rtc.now() + TimeSpan(0, 0, 0, 15);

    char buf[10];
    strcpy(buf, "hh:mm:ss");
    rtc.now().toString(buf);
    Serial.print("Now:    "); Serial.println(buf);

    strcpy(buf, "hh:mm:ss");
    target.toString(buf);
    Serial.print("Target: "); Serial.println(buf);
}

void loop() {
    if (taskDone) return;

    if (rtc.now() >= target) {
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
