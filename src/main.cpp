#include <Arduino.h>
#include <avr/interrupt.h>
#include <avr/sleep.h>

#define MOTOR_SLEEP_PIN 13
#define MOTOR_STEP_PIN 12
#define MOTOR_FAULT_PIN 11
#define SMOKER_PIN 10
#define SMOKER_FAN_PIN 9
#define STATUS_LED_PIN 2

#define DEGREES_PER_STEP 7.5  // motor resolution when fullstepping
#define STEPS_FOR_360 360 / 7.5
#define PUFF_DELAY 5000       // milliseconds between puffs

#define ADC_NUM_SAMPLES 5
#define BATTERY_MINIMUM 6.2     // 3.0V per cell is lowest recommendation for Li-Ion cells (2 x 3.0 = 6V)

uint8_t steps = 1;
unsigned long last_puff = millis();

void setup() {
  pinMode(MOTOR_SLEEP_PIN, OUTPUT);
  digitalWrite(MOTOR_SLEEP_PIN, LOW);

  pinMode(MOTOR_STEP_PIN, OUTPUT);
  digitalWrite(MOTOR_STEP_PIN, LOW);

  pinMode(MOTOR_FAULT_PIN, INPUT);

  pinMode(SMOKER_PIN, OUTPUT);
  digitalWrite(SMOKER_PIN, LOW);

  pinMode(SMOKER_FAN_PIN, OUTPUT);
  digitalWrite(SMOKER_FAN_PIN, LOW);

  pinMode(STATUS_LED_PIN, OUTPUT);
  digitalWrite(STATUS_LED_PIN, LOW);  // Show that we are on, and LED is working

  Serial.begin(9600);
  Serial.println("Setup done, wait a while before we start.");

  delay(5000);

  digitalWrite(STATUS_LED_PIN, HIGH);  // Turn off LED
}

void step_motor() {
  Serial.print("Step motor, count: "); Serial.print(steps);
  digitalWrite(MOTOR_SLEEP_PIN, HIGH);  // activate motor controller
  delay(2);                             // 1.7ms until controller is booted and ready for pulse
  digitalWrite(MOTOR_STEP_PIN, HIGH);   // trigger single step
  delayMicroseconds(2000);
  digitalWrite(MOTOR_STEP_PIN, LOW);
  delay(2);                             // let coils energize before we cut power
  digitalWrite(MOTOR_SLEEP_PIN, LOW);   // deactivate motor controller, save some battery
}

void motor_do_360() {
  Serial.println("Motor do 360 degrees.");
  digitalWrite(MOTOR_SLEEP_PIN, HIGH);  // activate motor controller
  delay(2);                             // 1.7ms until controller is booted and ready for pulse

  for (auto i = 0; i < STEPS_FOR_360 * 3; i++) {
    digitalWrite(MOTOR_STEP_PIN, HIGH);  // trigger single step
    delayMicroseconds(16000); // alt. 16000
    digitalWrite(MOTOR_STEP_PIN, LOW);
    delayMicroseconds(16000); // alt. 16000
  }

  delay(2);                             // let coils energize before we cut power
  digitalWrite(MOTOR_SLEEP_PIN, LOW);   // deactivate motor controller, save some battery
}

void puff_smoke() {
    Serial.print(", Puff smoke");
    digitalWrite(SMOKER_FAN_PIN, HIGH);  // start smoke fan for a short while to exhaust a smoke puff
    delay(100);
    digitalWrite(SMOKER_FAN_PIN, LOW);
}

void check_battery() {
  auto sum = 0;
  // take a number of analog samples and add them up
  for (auto count = 0; count < ADC_NUM_SAMPLES; count++) {
      sum += analogRead(A2);
      delay(5);
  }
  auto voltage = ((float)sum / (float)ADC_NUM_SAMPLES * 5.0) / 1024.0 * 2;  // 2 = voltage divider of equal resistant
  if (voltage < BATTERY_MINIMUM) {
    Serial.print(", battery voltage too low! ");
    Serial.print(voltage);
    Serial.println("V. Shutting down!");

    digitalWrite(SMOKER_PIN, LOW);
    digitalWrite(SMOKER_FAN_PIN, LOW);
    digitalWrite(MOTOR_SLEEP_PIN, LOW);

    // flash LED to show battery is too low
    for (auto i = 0; i < 10; i++) {
      digitalWrite(STATUS_LED_PIN, !digitalRead(STATUS_LED_PIN));
      delay(500);
    }

    set_sleep_mode(SLEEP_MODE_PWR_DOWN);
    cli();  // Disable interrupts
    sleep_mode();
    exit(1);
  } else {
    Serial.print(", battery voltage: ");
    Serial.print(voltage);
    Serial.println("V");
  }
}

void loop() {

  auto currentMillis = millis();

  check_battery();

  digitalWrite(SMOKER_PIN, HIGH);  // activate smoker, I know we keep setting this over and over again, but so what?

  if (steps > STEPS_FOR_360) {
    motor_do_360();
    steps = 0;
  } else {
    step_motor();
    steps++;
  }

  if (currentMillis - last_puff > PUFF_DELAY) {
    puff_smoke();
    last_puff = millis();
  }

  delay(1000);
}