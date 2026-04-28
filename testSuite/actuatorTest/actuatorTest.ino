// Servo actuator test — XIAO ESP32-C3
// Extend then retract once on boot. Repeats every 5 seconds.
// GPIO5 — Servo PWM

#include <ESP32Servo.h>

#define SERVO_PIN        D3
#define EXTEND_DELAY_MS  800
#define RETRACT_DELAY_MS 800

Servo actuatorServo;

void setup() {
    Serial.begin(9600);
    delay(1000);
    Serial.println("Servo test starting");
}

void loop() {
    Serial.println("Extending...");
    actuatorServo.attach(SERVO_PIN);
    delay(200);
    actuatorServo.write(180);
    delay(EXTEND_DELAY_MS);

    Serial.println("Retracting...");
    actuatorServo.write(0);
    delay(RETRACT_DELAY_MS);

    actuatorServo.detach();
    Serial.println("Done. Waiting 5s...");
    delay(5000);
}
