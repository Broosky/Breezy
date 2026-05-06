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
// Date: Monday, April 20th, 2026
// Description: A temperature controlled fan activator.
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Fold all: Ctrl + K + 0
// Unfold all: Ctrl + K + J
// Show file explorer: Ctrl + Shift + E
// Auto format: Ctrl + T
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#include <EEPROM.h>  // 1024 bytes available, addresses: 0 - 1023, width: 8 bits. Writing degrades.
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Firmware version:
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
const uint8_t FW_VERSION_MAJOR = 1;
const uint8_t FW_VERSION_MINOR = 0;
const uint8_t FW_VERSION_PATCH = 0;
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// EEPROM storage: Address (Width)
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// 0 (1)                    | 8 (1)                       | >= 9
// eCurrentGlobalMode       | eCurrentAutomaticMode       | Unused
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Types:
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
typedef enum GLOBAL_MODE : uint8_t {  // Align with GLOBAL_MODE_NAMES.
  GLOBAL_MODE_UNKNOWN,                // First power up or if EEPROM is corrupted.
  GLOBAL_MODE_MANUAL,                 // Output is always enabled.
  GLOBAL_MODE_AUTOMATIC,              // Output is enabled based on thermistor measurement and the selected automatic sub=mode.
  GLOBAL_MODE_COUNT                   // Must be last; for iterating/bounds.
} GLOBAL_MODE_T;
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
typedef enum AUTOMATIC_MODE : uint8_t {  // Align with AUTOMATIC_MODE_NAMES.
  AUTOMATIC_MODE_UNKNOWN,                // First power up or if EEPROM is corrupted.
  AUTOMATIC_MODE_BASIC,                  // Uses wide fixed temperature thresholds.
  AUTOMATIC_MODE_SYNC,                   // Uses a narrow temperature change over a short time period.
  AUTOMATIC_MODE_COUNT                   // Must be last; for iterating/bounds.
} AUTOMATIC_MODE_T;
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
typedef struct BUTTON_INPUT_STATE {
  unsigned long ulPressStartMs;
  uint8_t ubButtonWasPressed;
  uint8_t ubAlreadyTriggered;
} BUTTON_INPUT_STATE_T;
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Program constants:
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
const uint8_t GLOBAL_MODE_ADDRESS = 0;           // Update chart above.
const uint8_t AUTOMATIC_MODE_ADDRESS = 8;        // Update chart above.
const uint8_t SERIAL_OUT = true;                 // Debug output.
const uint16_t LED_STATE_DURATION_MS = 100;      // Common LED durations for the automatic monitoring blink rate.
const unsigned long BUTTON_HOLD_TIME_MS = 2000;  // Common button hold duration to change modes of operation.
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
const uint8_t PIN_INPUT_THERMISTOR_SENSE = A0;
const uint8_t PIN_OUTPUT_ENABLE = 3;
const uint8_t PIN_OUTPUT_MONITORING = 2;
const uint8_t PIN_INPUT_GLOBAL_MODE_SELECT = 4;
const uint8_t PIN_INPUT_AUTOMATIC_MODE_SELECT = 5;
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
const float TEMP_ZERO_KELVIN = 273.15;
const uint8_t THERMISTOR_NOMINAL_TEMP = 25;            // Almost always 25 degrees C; check datasheet.
const uint8_t THERMISTOR_READ_SAMPLES = 16;            // How many times the voltage is read before deciding an average value.
const uint16_t THERMISTOR_NOMINAL_RESISTANCE = 10000;  // Ohms
const uint16_t THERMISTOR_BETA = 3950;                 // Datasheet
const uint16_t THERMISTOR_REFERENCE = 10000;           // Ohms; resistor in the voltage divider.
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// The AC vent drops quickly and stabilizes at around 6 degrees C. After about 4 minutes once the AC turns off, the
// temperature rises to about 16.5 degrees C with the ambient room temperature of 22 degrees C.
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
const float OUTPUT_ENABLE_TEMP = 17.0;   // Typical vent air is 13 to 18 degrees C.
const float OUTPUT_DISABLE_TEMP = 20.0;  // AC off and vent temperature is rising; should be less than normal room temperature.
const float TEMP_FLOOR = 6.0;            // The normalized temperature of the AC vent air when it's fully active.
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
const char* GLOBAL_MODE_NAMES[] = {
  // Align with GLOBAL_MODE.
  "Unknown Global Mode",
  "Global Manual Mode",
  "Global Automatic Mode",
};
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
const char* AUTOMATIC_MODE_NAMES[] = {
  // Align with AUTOMATIC_MODE.
  "Unknown Automatic Mode",
  "Automatic Basic Mode",
  "Automatic Sync Mode",
};
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Program globals:
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
uint8_t ubOutputEnabled = false;
GLOBAL_MODE_T eCurrentGlobalMode = GLOBAL_MODE_UNKNOWN;
AUTOMATIC_MODE_T eCurrentAutomaticMode = AUTOMATIC_MODE_UNKNOWN;
BUTTON_INPUT_STATE_T globalModeButtonState = { .ulPressStartMs = 0, .ubButtonWasPressed = false, .ubAlreadyTriggered = false };
BUTTON_INPUT_STATE_T automaticModeButtonState = { .ulPressStartMs = 0, .ubButtonWasPressed = false, .ubAlreadyTriggered = false };
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Main program initialization.
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void setup(void) {
  pinMode(PIN_INPUT_THERMISTOR_SENSE, INPUT);
  pinMode(PIN_INPUT_GLOBAL_MODE_SELECT, INPUT_PULLUP);
  pinMode(PIN_INPUT_AUTOMATIC_MODE_SELECT, INPUT_PULLUP);
  ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  pinMode(PIN_OUTPUT_ENABLE, OUTPUT);
  pinMode(PIN_OUTPUT_MONITORING, OUTPUT);
  ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  if (SERIAL_OUT) {
    Serial.begin(9600);
    delayInternal(1000, interruptHandler);
    Serial.println("=================");
    Serial.println("== New Session ==");
    Serial.println("=================");
  }
  ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  // Hard delay in case of DC power chatter or rapid connect/disconnect of power.
  ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  delay(3000);
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Main program loop.
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void loop(void) {
  ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  // Get the last known global and automatic modes or default them if none exist.
  ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  populateCurrentGlobalMode(&eCurrentGlobalMode);
  populateCurrentAutomaticMode(&eCurrentAutomaticMode);
  ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  // Run through the global mode logic and repeat. The global mode changes are handled in the interrupt handler.
  ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  switch (eCurrentGlobalMode) {
    case GLOBAL_MODE_MANUAL:
      {
        performGlobalManualMode();
        break;
      }
    case GLOBAL_MODE_AUTOMATIC:
      {
        performGlobalAutomaticMode();
        break;
      }
    default:
      {
        break;
      }
  }
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// All we'll do here is enable the output and check to see if the global most has changed.
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void performGlobalManualMode(void) {
  enableOutput(NULL);
  ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  if (considerModeChange(PIN_INPUT_GLOBAL_MODE_SELECT, &globalModeButtonState)) {
    advanceGlobalMode();
  }
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Enables the output automatically with two sub-modes:
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// 1. Basic: Engage/disengage the output based on fixed lower and upper wide thresholds.
// 2. Sync: Engage/disengage the output based on a narrow change in temperature over a short time period.
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void performGlobalAutomaticMode(void) {
  switch (eCurrentAutomaticMode) {
    case AUTOMATIC_MODE_BASIC:
      {
        handleAutomaticBasicMode();
        break;
      }
    case AUTOMATIC_MODE_SYNC:
      {
        handleAutomaticSyncMode();
        break;
      }
    default:
      {
        break;
      }
  }
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Handles the automatic basic mode.
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void handleAutomaticBasicMode(void) {
  ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  // Blink the selected automatic mode and let the cycle run for roughly 1 second.
  ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  uint16_t usRemainingDuration = 1000;
  blinkCurrentAutomaticMode(AUTOMATIC_MODE_BASIC, LED_STATE_DURATION_MS, &usRemainingDuration);
  ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  // Handler:
  ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  float fCurrentTemp = handleThermistor();

  if (SERIAL_OUT) {
    Serial.print("Current temperature (C): ");
    Serial.println(fCurrentTemp, 2);
  }

  if (fCurrentTemp <= OUTPUT_ENABLE_TEMP) {
    enableOutput(&fCurrentTemp);
  } else if (fCurrentTemp >= OUTPUT_DISABLE_TEMP) {
    disableOutput(&fCurrentTemp);
  }
  ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  delayInternal(usRemainingDuration, interruptHandler);
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Returns temperature in Celcius.
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
float handleThermistor(void) {
  float fTempAverage = 0;
  unsigned long ulTempAccumulatedSamples = 0;

  // Ensure we're on the default ADC.
  if ((ADMUX & ((1 << REFS1) | (1 << REFS0))) != (1 << REFS0)) {
    analogReference(DEFAULT);

    // Dummy reads.
    for (uint8_t ubI = 0; ubI < 5; ubI++) {
      analogRead(PIN_INPUT_THERMISTOR_SENSE);
      delayInternal(5, interruptHandler);
    }
  }

  ulTempAccumulatedSamples = 0;
  for (uint8_t ubI = 0; ubI < THERMISTOR_READ_SAMPLES; ubI++) {
    ulTempAccumulatedSamples += analogRead(PIN_INPUT_THERMISTOR_SENSE);
    delayInternal(5, interruptHandler);
  }

  fTempAverage = ulTempAccumulatedSamples / (float)THERMISTOR_READ_SAMPLES;

  // Calculate NTC resistance.
  fTempAverage = 1023 / fTempAverage - 1;
  fTempAverage = THERMISTOR_REFERENCE / fTempAverage;

  float fTempNow = fTempAverage / THERMISTOR_NOMINAL_RESISTANCE;   // (R / Ro)
  fTempNow = log(fTempNow);                                        // ln(R / Ro)
  fTempNow /= THERMISTOR_BETA;                                     // 1 / B * ln(R / Ro)
  fTempNow += 1.0 / (THERMISTOR_NOMINAL_TEMP + TEMP_ZERO_KELVIN);  // + (1 / To)
  fTempNow = 1.0 / fTempNow;                                       // Invert
  fTempNow -= TEMP_ZERO_KELVIN;                                    // Convert absolute temperature to Celcius.

  return fTempNow;
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Handles the automatic sync mode.
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void handleAutomaticSyncMode(void) {
  ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  // Blink the selected automatic mode and let the cycle run for roughly 1 second.
  ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  uint16_t usRemainingDuration = 1000;
  blinkCurrentAutomaticMode(AUTOMATIC_MODE_SYNC, LED_STATE_DURATION_MS, &usRemainingDuration);
  ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  // Handler: Future versions may implement this mode. For now, just disable the output.
  ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  disableOutput(NULL);
  ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  delayInternal(usRemainingDuration, interruptHandler);
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void blinkCurrentAutomaticMode(AUTOMATIC_MODE_T eCurrentAutomaticMode, uint8_t ubBlinkDurationMs, uint16_t* const p_usRemainingDuration) {
  for (uint8_t ubI = 0; ubI < (uint8_t)eCurrentAutomaticMode; ubI++) {
    digitalWrite(PIN_OUTPUT_MONITORING, true);
    delayInternal(ubBlinkDurationMs, interruptHandler);
    *p_usRemainingDuration -= ubBlinkDurationMs;

    digitalWrite(PIN_OUTPUT_MONITORING, false);
    delayInternal(ubBlinkDurationMs, interruptHandler);
    *p_usRemainingDuration -= ubBlinkDurationMs;
  }
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Gets the last known global mode or sets it initially if the last global mode is unknown.
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void populateCurrentGlobalMode(GLOBAL_MODE_T* const p_CurrentGlobalMode) {
  size_t stAddress = GLOBAL_MODE_ADDRESS;

  EEPROM.get(stAddress, *p_CurrentGlobalMode);

  // Default to automatic if the global mode is unknown.
  if (*p_CurrentGlobalMode <= GLOBAL_MODE_UNKNOWN || *p_CurrentGlobalMode >= GLOBAL_MODE_COUNT) {
    *p_CurrentGlobalMode = GLOBAL_MODE_AUTOMATIC;
    putCurrentGlobalMode(stAddress, *p_CurrentGlobalMode);
  }
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void putCurrentGlobalMode(size_t stAddress, GLOBAL_MODE_T p_CurrentGlobalMode) {
  if (SERIAL_OUT) {
    Serial.print("Global Mode EEPROM.put(): ");
    Serial.print(p_CurrentGlobalMode);
    Serial.print(", ");
    Serial.println(getGlobalModeName(p_CurrentGlobalMode));
  }
  EEPROM.put(stAddress, p_CurrentGlobalMode);
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
const char* const getGlobalModeName(GLOBAL_MODE_T eGlobalMode) {
  if (eGlobalMode >= GLOBAL_MODE_UNKNOWN && eGlobalMode < GLOBAL_MODE_COUNT) {
    return GLOBAL_MODE_NAMES[eGlobalMode];
  }
  return "UNKNOWN";
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Gets the last known automatic mode or sets it initially if the last automatic mode is unknown.
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void populateCurrentAutomaticMode(AUTOMATIC_MODE_T* const p_CurrentAutomaticMode) {
  size_t stAddress = AUTOMATIC_MODE_ADDRESS;

  EEPROM.get(stAddress, *p_CurrentAutomaticMode);

  // Default to basic if the automatic mode is unknown.
  if (*p_CurrentAutomaticMode <= AUTOMATIC_MODE_UNKNOWN || *p_CurrentAutomaticMode >= AUTOMATIC_MODE_COUNT) {
    *p_CurrentAutomaticMode = AUTOMATIC_MODE_BASIC;
    putCurrentAutomaticMode(stAddress, *p_CurrentAutomaticMode);
  }
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void putCurrentAutomaticMode(size_t stAddress, AUTOMATIC_MODE_T p_CurrentAutomaticMode) {
  if (SERIAL_OUT) {
    Serial.print("Automatic Mode EEPROM.put(): ");
    Serial.print(p_CurrentAutomaticMode);
    Serial.print(", ");
    Serial.println(getAutomaticModeName(p_CurrentAutomaticMode));
  }
  EEPROM.put(stAddress, p_CurrentAutomaticMode);
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
const char* const getAutomaticModeName(AUTOMATIC_MODE_T eAutomaticMode) {
  if (eAutomaticMode >= AUTOMATIC_MODE_UNKNOWN && eAutomaticMode < AUTOMATIC_MODE_COUNT) {
    return AUTOMATIC_MODE_NAMES[eAutomaticMode];
  }
  return "UNKNOWN";
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Internal delay handler to check on tasks while waiting.
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void delayInternal(uint16_t usDelayMs, void (*p_fnInterruptHandler)(void)) {
  unsigned long ulMillisStart = millis();
  unsigned long ulMillisNow = 0;

  while ((ulMillisNow = millis() - ulMillisStart) < usDelayMs) {
    if (p_fnInterruptHandler)
      p_fnInterruptHandler();
  }
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Check on interrupts or other states while performing an internal delay. Fast code; no delays or waiting.
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void interruptHandler(void) {
  if (considerModeChange(PIN_INPUT_GLOBAL_MODE_SELECT, &globalModeButtonState)) {
    advanceGlobalMode();
  }
  ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  if (considerModeChange(PIN_INPUT_AUTOMATIC_MODE_SELECT, &automaticModeButtonState)) {
    advanceAutomaticMode();
  }
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void advanceGlobalMode(void) {
  if (SERIAL_OUT) {
    Serial.print("Global mode going from: ");
    Serial.print(eCurrentGlobalMode);
    Serial.print(", ");
    Serial.println(getGlobalModeName(eCurrentGlobalMode));
  }

  eCurrentGlobalMode = (GLOBAL_MODE_T)(eCurrentGlobalMode + 1);

  // Wrap around.
  if (eCurrentGlobalMode >= GLOBAL_MODE_COUNT) {
    eCurrentGlobalMode = GLOBAL_MODE_MANUAL;
  }

  if (SERIAL_OUT) {
    Serial.print("Global mode going to: ");
    Serial.print(eCurrentGlobalMode);
    Serial.print(", ");
    Serial.println(getGlobalModeName(eCurrentGlobalMode));
  }

  putCurrentGlobalMode(GLOBAL_MODE_ADDRESS, eCurrentGlobalMode);
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void advanceAutomaticMode(void) {
  if (SERIAL_OUT) {
    Serial.print("Automatic mode going from: ");
    Serial.print(eCurrentAutomaticMode);
    Serial.print(", ");
    Serial.println(getAutomaticModeName(eCurrentAutomaticMode));
  }

  eCurrentAutomaticMode = (AUTOMATIC_MODE_T)(eCurrentAutomaticMode + 1);

  // Wrap around.
  if (eCurrentAutomaticMode >= AUTOMATIC_MODE_COUNT) {
    eCurrentAutomaticMode = AUTOMATIC_MODE_BASIC;
  }

  if (SERIAL_OUT) {
    Serial.print("Automatic mode going to: ");
    Serial.print(eCurrentAutomaticMode);
    Serial.print(", ");
    Serial.println(getAutomaticModeName(eCurrentAutomaticMode));
  }

  putCurrentAutomaticMode(AUTOMATIC_MODE_ADDRESS, eCurrentAutomaticMode);
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Determines if the global or automatic mode should changed based on the hold time of the mode toggle buttons.
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
uint8_t considerModeChange(uint8_t ubSwitchPin, BUTTON_INPUT_STATE_T* const p_CurrentButtonState) {
  // Input pins are pulled up internally.
  uint8_t ubPressed = (digitalRead(ubSwitchPin) == LOW);
  unsigned long ulNow = millis();

  /*
  if (SERIAL_OUT && ubPressed) {
    Serial.print("Button pressed, pin: ");
    Serial.println(ubSwitchPin);
  }
  //*/

  if (ubPressed) {
    if (!p_CurrentButtonState->ubButtonWasPressed) {

      // Button was just pressed.
      p_CurrentButtonState->ulPressStartMs = ulNow;
      p_CurrentButtonState->ubAlreadyTriggered = false;
    }

    // Button was held long enough and not already triggered.
    if (!p_CurrentButtonState->ubAlreadyTriggered && (ulNow - p_CurrentButtonState->ulPressStartMs >= BUTTON_HOLD_TIME_MS)) {
      p_CurrentButtonState->ubAlreadyTriggered = true;
      p_CurrentButtonState->ubButtonWasPressed = true;

      if (SERIAL_OUT) {
        Serial.print("Button trigger, pin: ");
        Serial.println(ubSwitchPin);
      }
      return true;
    }
  } else {
    // Button was released; reset state.
    p_CurrentButtonState->ubButtonWasPressed = false;
    p_CurrentButtonState->ubAlreadyTriggered = false;
  }

  p_CurrentButtonState->ubButtonWasPressed = ubPressed;
  return false;
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Output drivers.
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void enableOutput(const float* const p_fCurrentTemp) {
  if (!ubOutputEnabled) {
    ubOutputEnabled = true;
    digitalWrite(PIN_OUTPUT_ENABLE, ubOutputEnabled);
    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    if (SERIAL_OUT) {
      Serial.print("Enabled output: ");
      if (p_fCurrentTemp) {
        Serial.print(*p_fCurrentTemp, 2);
        Serial.println(" Degrees C trigger.");
      } else {
        Serial.println("Manual mode trigger.");
      }
    }
  }
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void disableOutput(const float* const p_fCurrentTemp) {
  if (ubOutputEnabled) {
    ubOutputEnabled = false;
    digitalWrite(PIN_OUTPUT_ENABLE, ubOutputEnabled);
    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    if (SERIAL_OUT) {
      Serial.print("Disabled output: ");
      if (p_fCurrentTemp) {
        Serial.print(*p_fCurrentTemp, 2);
        Serial.println(" Degrees C trigger.");
      } else {
        Serial.println("Manual mode trigger.");
      }
    }
  }
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////