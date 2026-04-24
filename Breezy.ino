/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Project: Breezy                                                                                                         //
// Author: Jeffrey Bednar                                                                                                  //
// Copyright (c) Illusion Interactive, 2011 - 2026.                                                                        //
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Educational Use Notice:                                                                                                 //
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// This project is provided for educational and learning purposes only. You are welcome to read, study, and experiment     //
// with this software and/or hardware. It is not intended for commercial use. This software and/or hardware is provided    //
// "as is", without warranty of any kind. The author assumes no responsibility for any damages or issues resulting from    //
// its use.                                                                                                                //
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Date: Tuesday, April 20th, 2026
// Description: A temperature controlled fan activator.
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Fold all: Ctrl + K + 0
// Unfold all: Ctrl + K + J
// Show file explorer: Ctrl + Shift + E
// Auto format: Ctrl + T
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Firmware version:
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
const uint8_t FW_VERSION_MAJOR = 1;
const uint8_t FW_VERSION_MINOR = 0;
const uint8_t FW_VERSION_PATCH = 0;
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Program constants:
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
const uint8_t PIN_INPUT_THERMISTOR_SENSE = A0;
const uint8_t PIN_OUTPUT_RELAY_ENABLE = 6;
const uint8_t PIN_OUTPUT_ACTIVITY = 7;
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
const float TEMP_ZERO_KELVIN = 273.15;
const uint8_t THERMISTOR_NOMINAL_TEMP = 25;            // Almost always 25 degrees C; check datasheet.
const uint8_t THERMISTOR_READ_SAMPLES = 16;            // How many times the voltage is read before deciding an average value.
const uint16_t THERMISTOR_NOMINAL_RESISTANCE = 10000;  // Ohms
const uint16_t THERMISTOR_BETA = 3950;                 // Datasheet
const uint16_t THERMISTOR_REFERENCE = 10000;           // Ohms; resistor in the voltage divider.
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
const float RELAY_ON_TEMP = 15.5;   // Typical vent air is 13 to 18 degrees C.
const float RELAY_OFF_TEMP = 20.5;  // AC off; should be less than normal room temperature.
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Main program initialization.
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void setup(void) {
  pinMode(PIN_INPUT_THERMISTOR_SENSE, INPUT);
  pinMode(PIN_OUTPUT_RELAY_ENABLE, OUTPUT);
  pinMode(PIN_OUTPUT_ACTIVITY, OUTPUT);
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Main program loop.
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void loop(void) {
  digitalWrite(PIN_OUTPUT_ACTIVITY, true);

  float currentTemp = handleThermistor();

  if (currentTemp <= RELAY_ON_TEMP) {
    digitalWrite(PIN_OUTPUT_RELAY_ENABLE, true);
  } else if (currentTemp >= RELAY_OFF_TEMP) {
    digitalWrite(PIN_OUTPUT_RELAY_ENABLE, false);
  }

  delayInternal(750, waitHandler);
  digitalWrite(PIN_OUTPUT_ACTIVITY, false);
  delayInternal(250, waitHandler);
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Check on interrupts or other states while performing a delay.
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void waitHandler(void) {
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Delay and also check on tasks while waiting.
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void delayInternal(uint16_t delayMs, void (*waitHandler)(void)) {
  unsigned long ulMillisStart = millis();
  unsigned long ulMillisNow = 0;

  while ((ulMillisNow = millis() - ulMillisStart) < delayMs) {
    if (waitHandler)
      waitHandler();
  }
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Returns temperature in Celcius.
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
float handleThermistor(void) {
  float tempAverage = 0;
  unsigned long tempAccumulatedSamples = 0;

  // Ensure we're on the default ADC.
  if ((ADMUX & ((1 << REFS1) | (1 << REFS0))) != (1 << REFS0)) {
    analogReference(DEFAULT);

    // Dummy reads.
    for (uint8_t i = 0; i < 5; i++) {
      analogRead(PIN_INPUT_THERMISTOR_SENSE);
      delay(5);
    }
  }

  tempAccumulatedSamples = 0;
  for (uint8_t i = 0; i < THERMISTOR_READ_SAMPLES; i++) {
    tempAccumulatedSamples += analogRead(PIN_INPUT_THERMISTOR_SENSE);
    delay(5);
  }

  tempAverage = tempAccumulatedSamples / (float)THERMISTOR_READ_SAMPLES;

  // Calculate NTC resistance.
  tempAverage = 1023 / tempAverage - 1;
  tempAverage = THERMISTOR_REFERENCE / tempAverage;

  float tempNow = tempAverage / THERMISTOR_NOMINAL_RESISTANCE;    // (R / Ro)
  tempNow = log(tempNow);                                         // ln(R / Ro)
  tempNow /= THERMISTOR_BETA;                                     // 1 / B * ln(R / Ro)
  tempNow += 1.0 / (THERMISTOR_NOMINAL_TEMP + TEMP_ZERO_KELVIN);  // + (1 / To)
  tempNow = 1.0 / tempNow;                                        // Invert
  tempNow -= TEMP_ZERO_KELVIN;                                    // Convert absolute temperature to Celcius.

  return tempNow;
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////