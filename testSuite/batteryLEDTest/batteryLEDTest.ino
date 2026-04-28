// ============================================================
//  Battery LED Flash Test — XIAO ESP32-C3
//  Forces low-battery condition to verify LED flashes for
//  2 minutes (400ms on / 600ms off × 120) then rests for
//  8 minutes using light sleep, exactly as nightNightMain.
//
//  Pin assignments
//    D10  — LED + 220Ω resistor to GND (low battery alert)
// ============================================================

#include "esp_sleep.h"
#include "WiFi.h"
#include "esp_bt.h"

// ── Pin definitions ─────────────────────────────────────────
#define LED_PIN           D10          // GPIO10

// ── Low-battery flash parameters ────────────────────────────
#define FLASH_ON_US       400000ULL    //  400 ms in microseconds
#define FLASH_OFF_US      600000ULL    //  600 ms in microseconds
#define FLASH_CYCLES      120          //  120 × 1s = 2 min window
#define SLEEP_8MIN_US     480000000ULL //  8 min in microseconds

// ── Forward declarations ─────────────────────────────────────
void disableUnusedPeripherals();
void lightSleepUs(uint64_t us);
bool isBatteryLow();
void runLowBatteryLoop();

// ============================================================
//  setup()
// ============================================================
void setup() {
    disableUnusedPeripherals();

    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);

    // Run one complete low-battery cycle to verify LED behavior
    runLowBatteryLoop();
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
//  Battery check — forced true for exactly one loop iteration
// ============================================================
bool isBatteryLow() {
    // Returns true on the first call (triggers one full cycle),
    // false on the second call (exits the loop cleanly).
    static bool ran = false;
    if (!ran) {
        ran = true;
        return true;
    }
    return false;
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
void runLowBatteryLoop() {
    while (isBatteryLow()) {
        // ── 2-minute flash window ────────────────────────────
        for (int cycle = 0; cycle < FLASH_CYCLES; cycle++) {
            // LED on — GPIO state persists through light sleep
            digitalWrite(LED_PIN, HIGH);
            lightSleepUs(FLASH_ON_US);

            // LED off
            digitalWrite(LED_PIN, LOW);
            lightSleepUs(FLASH_OFF_US);
        }

        // ── 8-minute rest ────────────────────────────────────
        lightSleepUs(SLEEP_8MIN_US);
    }
}
