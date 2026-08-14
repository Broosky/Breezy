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
// Description: A temperature-triggered fan controller designed to improve airflow.
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Fold all: Ctrl + K + 0
// Unfold all: Ctrl + K + J
// Show file explorer: Ctrl + Shift + E
// Auto format: Ctrl + T
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#include "Headers/gamma.h"
#include "Headers/pwm.h"
#include <EEPROM.h>  // 1024 bytes available, addresses: 0 - 1023, width: 8 bits. Writing degrades.
#include <TimeLib.h>
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Firmware version:
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
const uint8_t FW_VERSION_MAJOR = 1;
const uint8_t FW_VERSION_MINOR = 1;
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
  AUTOMATIC_MODE_INTERMITTENT,           // Cycles the output on and off continuously.
  AUTOMATIC_MODE_TIMER,                  // Cycles the output on and off at configured times.
  AUTOMATIC_MODE_COUNT                   // Must be last; for iterating/bounds.
} AUTOMATIC_MODE_T;
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
typedef enum DAY : uint8_t {
  DAY_UNKNOWN,
  DAY_SUNDAY,
  DAY_MONDAY,
  DAY_TUESDAY,
  DAY_WEDNESDAY,
  DAY_THURSDAY,
  DAY_FRIDAY,
  DAY_SATURDAY,
  DAY_COUNT  // Must be last; for iterating/bounds.
} DAY_T;
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
typedef enum ERROR_CODE : uint8_t {  // Align with ERROR_CODE_NAMES.
  ERROR_CODE_UNKNOWN,
  ERROR_CODE_INTERNAL,  // Logic related.
  ERROR_CODE_EXTERNAL,  // Device or external component related.
  ERROR_CODE_COUNT      // Must be last; for iterating/bounds.
} ERROR_CODE_T;
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
typedef struct BUTTON_INPUT_STATE {
  unsigned long ulPressStartMs;
  uint8_t ubButtonWasPressed;
  uint8_t ubAlreadyTriggered;
} BUTTON_INPUT_STATE_T;
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
typedef struct TIMER_STATE {
  DAY_T eWeekday;
  uint8_t ubHour;
  uint8_t ubMinute;
  uint8_t ubOutputState;
} TIMER_STATE_T;
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
typedef struct QUIET_TIME {
  DAY_T eWeekday;
  uint8_t ubStartHour;
  uint8_t ubStartMinute;
  uint8_t ubEndHour;
  uint8_t ubEndMinute;
} QUIET_TIME_T;
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Program constants:
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
const uint8_t SERIAL_OUT = true;                    // Debug output.
const uint16_t HANDLER_TOTAL_DURATION_MS = 5000;    // In each automatic mode, let the handler run for a specific duration.
const uint8_t SYNC_MODE_CHECK_SECONDS = 10;         // Number of seconds the sync mode checks if the temperature has increased or decreased.
const float SYNC_MODE_TEMP_DELTA = 1.0f;            // In sync mode, the temperature has to increase or decrease by this amount to consider cycling the output.
const uint8_t INTERMITTENT_MODE_CYCLE_MINUTES = 5;  // Number of minutes the output is enabled/disabled when cycled in intermittent mode.
const uint16_t PWM_DEFAULT_DELAY_US = 900;          // Microseconds.
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Inhibits the output from enabling in automatic mode when the output is commanded to activate.
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
const QUIET_TIME_T QUIET_TIMES[] = { { DAY_FRIDAY, 23, 30, 0, 0 },
                                     { DAY_SATURDAY, 0, 0, 10, 30 },
                                     { DAY_SATURDAY, 23, 30, 0, 0 },
                                     { DAY_SUNDAY, 0, 0, 10, 30 } };
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// The AC vent drops quickly and stabilizes at around 6 degrees C. After about 4 minutes once the AC turns off, the
// temperature rises to about 16.5 degrees C with the ambient room temperature of 22 degrees C.
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
const float OUTPUT_ENABLE_TEMP = 17.0f;   // Typical vent air is 13 to 18 degrees C.
const float OUTPUT_DISABLE_TEMP = 20.0f;  // AC off and vent temperature is rising; should be less than normal room temperature.
const float TEMP_FLOOR = 6.0f;            // The normalized temperature of the AC vent air when it's fully active.
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
const uint8_t GLOBAL_MODE_ADDRESS = 0;           // Update chart above.
const uint8_t AUTOMATIC_MODE_ADDRESS = 8;        // Update chart above.
const uint16_t LED_PULSE_DURATION_MS = 100;      // Common LED durations for the automatic monitoring pulse rate.
const unsigned long BUTTON_HOLD_TIME_MS = 2000;  // Common button hold duration to change modes of operation.
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
const uint8_t PIN_INPUT_THERMISTOR_SENSE = A0;
const uint8_t PIN_OUTPUT_ENABLE = 3;                // Red LED.
const uint8_t PIN_OUTPUT_MONITORING = 2;            // Yellow LED.
const uint8_t PIN_INPUT_GLOBAL_MODE_SELECT = 4;     // Red momentary push-button.
const uint8_t PIN_INPUT_AUTOMATIC_MODE_SELECT = 5;  // Black momentary push-button.
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
const float TEMP_ZERO_KELVIN = 273.15;
const uint8_t THERMISTOR_NOMINAL_TEMP = 25;            // Almost always 25 degrees C; check datasheet.
const uint8_t THERMISTOR_READ_SAMPLES = 16;            // How many times the voltage is read before deciding an average value.
const uint16_t THERMISTOR_NOMINAL_RESISTANCE = 10000;  // Ohms
const uint16_t THERMISTOR_BETA = 3950;                 // Datasheet
const uint16_t THERMISTOR_REFERENCE = 10000;           // Ohms; resistor in the voltage divider.
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
const char* GLOBAL_MODE_NAMES[] = {
  // Align with GLOBAL_MODE.
  "Global Mode Unknown",
  "Global Manual Mode",
  "Global Automatic Mode",
};
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
const char* AUTOMATIC_MODE_NAMES[] = {
  // Align with AUTOMATIC_MODE.
  "Automatic Mode Unknown",
  "Automatic Basic Mode",
  "Automatic Sync Mode",
  "Automatic Intermittent Mode",
  "Automatic Timer Mode",
};
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
const char* DAY_NAMES[] = {
  // Align with DAY.
  "Day Unknown",
  "Sunday",
  "Monday",
  "Tuesday",
  "Wednesday",
  "Thursday",
  "Friday",
  "Saturday",
  "Sunday",
};
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
const char* ERROR_CODE_NAMES[] = {
  // Align with ERROR_CODE.
  "Error Code Unknown",
  "Error Code Internal",
  "Error Code External",
};
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Program globals:
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
uint8_t ubOutputEnabled = false;
uint8_t ubOutputShouldEnable = false;  // For reconsidering output after quiet times expire.
GLOBAL_MODE_T eCurrentGlobalMode = GLOBAL_MODE_UNKNOWN;
AUTOMATIC_MODE_T eCurrentAutomaticMode = AUTOMATIC_MODE_UNKNOWN;
BUTTON_INPUT_STATE_T globalModeButtonState = { .ulPressStartMs = 0, .ubButtonWasPressed = false, .ubAlreadyTriggered = false };
BUTTON_INPUT_STATE_T automaticModeButtonState = { .ulPressStartMs = 0, .ubButtonWasPressed = false, .ubAlreadyTriggered = false };
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Main program initialization.
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void setup(void) {
  ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  // Hard delay in case of DC power chatter or rapid connect/disconnect of power.
  ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  delay(3000);
  ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  pinMode(PIN_INPUT_THERMISTOR_SENSE, INPUT);
  pinMode(PIN_INPUT_GLOBAL_MODE_SELECT, INPUT_PULLUP);
  pinMode(PIN_INPUT_AUTOMATIC_MODE_SELECT, INPUT_PULLUP);
  ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  pinMode(PIN_OUTPUT_ENABLE, OUTPUT);
  pinMode(PIN_OUTPUT_MONITORING, OUTPUT);
  ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  setupTime();
  setupPrintTime();
  ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  if (SERIAL_OUT) {
    Serial.begin(9600);
    delayInternalSeconds(1, interruptHandler);
    Serial.println("=================");
    Serial.println("== New Session ==");
    Serial.println("=================");
  }
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
        handleError(ERROR_CODE_INTERNAL, "Uncased global mode.");
        break;
      }
  }
  ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  // During automatic operation the output may be commanded to activate, but quiet times will prevent it from activating. In
  // case the output was commanded to activate and the quiet time has passed, we'll consider if the output should be re-enabled.
  ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  considerQuietTimes();
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
uint8_t considerQuietTimes(void) {
  /*
  if (eCurrentGlobalMode == GLOBAL_MODE_AUTOMATIC) {
    int nNowWeekday = weekday(), nNowHour = hour(), nNowMinute = minute();

    if (currentTimerState.eWeekday == nNowWeekday && (currentTimerState.ubHour > nNowHour || (currentTimerState.ubHour == nNowHour && currentTimerState.ubMinute >= nNowMinute))) {
    }
  }
  if (ubOutputShouldEnable) {
    ubOutputShouldEnable = false;
    enableOutput(NULL);
  }
  //*/
  return true;
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
// Enables/disables the output automatically using sub-modes:
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// 1. Basic: Uses wide fixed temperature thresholds.
// 2. Sync: Uses a narrow temperature change over a short time period.
// 3. Intermittent: Cycles the output on and off continuously.
// 4. Timer: Cycles the output on and off at configured times.
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
    case AUTOMATIC_MODE_INTERMITTENT:
      {
        handleAutomaticIntermittentMode();
        break;
      }
    case AUTOMATIC_MODE_TIMER:
      {
        handleAutomaticTimerMode();
        break;
      }
    default:
      {
        handleError(ERROR_CODE_INTERNAL, "Uncased automatic mode.");
        break;
      }
  }
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Handles the automatic basic mode.
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void handleAutomaticBasicMode(void) {
  ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  // Pulse the selected automatic mode and let the cycle run.
  ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  uint16_t usRemainingDurationMs = HANDLER_TOTAL_DURATION_MS;
  pulse(PIN_OUTPUT_MONITORING,
        (uint8_t)AUTOMATIC_MODE_BASIC,
        250,
        PWM_DEFAULT_DELAY_US,
        &usRemainingDurationMs);
  ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  // Handler:
  ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  float fCurrentTemp = readThermistor();

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
  delayInternalMilliseconds(usRemainingDurationMs, interruptHandler);
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void pulse(uint8_t ubPin, uint8_t ubTimes, uint16_t usDelayMs, uint16_t usPwmdelayUs, uint16_t* const p_usRemainingDurationMs) {
  unsigned long ulStartMs = millis(), ulElapsed;
  ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  for (uint8_t ubI = 0; ubI < ubTimes; ubI++) {
    pulsePin(ubPin, usPwmdelayUs);
    if (ubTimes > 1 && ubI + 1 < ubTimes) {
      delayInternalMilliseconds(usDelayMs, NULL);
    }
  }
  ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  ulElapsed = millis() - ulStartMs;
  ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  if (p_usRemainingDurationMs) {
    if (*p_usRemainingDurationMs > ulElapsed) {
      *p_usRemainingDurationMs -= ulElapsed;
    } else {
      *p_usRemainingDurationMs = 0;
    }
  }
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void pulsePin(uint8_t ubPin, uint16_t usPwmdelayUs) {
  uint8_t ubI = 0;
  ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  for (; ubI < GAMMA_CONDENSED_MAX_VALUES; ubI++) {
    softwarePwmAnalogWrite(ubPin, gammaCorrectedCondensed(ubI));
    delayInternalMicroseconds(usPwmdelayUs, NULL);
  }
  for (ubI = GAMMA_CONDENSED_MAX_VALUES - 1; ubI > 0; ubI--) {
    softwarePwmAnalogWrite(ubPin, gammaCorrectedCondensed(ubI));
    delayInternalMicroseconds(usPwmdelayUs, NULL);
  }
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Returns temperature in Celcius.
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
float readThermistor(void) {
  float fTempAverage = 0;
  unsigned long ulTempAccumulatedSamples = 0;

  // Ensure we're on the default ADC.
  if ((ADMUX & ((1 << REFS1) | (1 << REFS0))) != (1 << REFS0)) {
    analogReference(DEFAULT);

    // Dummy reads.
    for (uint8_t ubI = 0; ubI < 5; ubI++) {
      analogRead(PIN_INPUT_THERMISTOR_SENSE);
      delayInternalMilliseconds(5, interruptHandler);
    }
  }

  ulTempAccumulatedSamples = 0;
  for (uint8_t ubI = 0; ubI < THERMISTOR_READ_SAMPLES; ubI++) {
    ulTempAccumulatedSamples += analogRead(PIN_INPUT_THERMISTOR_SENSE);
    delayInternalMilliseconds(5, interruptHandler);
  }

  fTempAverage = ulTempAccumulatedSamples / (float)THERMISTOR_READ_SAMPLES;

  // Calculate NTC resistance.
  fTempAverage = 1023 / fTempAverage - 1;
  fTempAverage = THERMISTOR_REFERENCE / fTempAverage;

  float fTempNow = fTempAverage / THERMISTOR_NOMINAL_RESISTANCE;    // (R / Ro)
  fTempNow = log(fTempNow);                                         // ln(R / Ro)
  fTempNow /= THERMISTOR_BETA;                                      // 1 / B * ln(R / Ro)
  fTempNow += 1.0f / (THERMISTOR_NOMINAL_TEMP + TEMP_ZERO_KELVIN);  // + (1 / To)
  fTempNow = 1.0f / fTempNow;                                       // Invert
  fTempNow -= TEMP_ZERO_KELVIN;                                     // Convert absolute temperature to Celcius.

  return fTempNow;
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Handles the automatic sync mode.
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void handleAutomaticSyncMode(void) {
  static float fTempStart = readThermistor();
  static unsigned long ulMillisStart = millis();
  static unsigned long ulSyncMillis = (SYNC_MODE_CHECK_SECONDS >= 10 ? SYNC_MODE_CHECK_SECONDS : 10) * 1000;
  unsigned long ulMillisNow = millis();
  ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  // Pulse the selected automatic mode and let the cycle run.
  ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  uint16_t usRemainingDurationMs = HANDLER_TOTAL_DURATION_MS;
  pulse(PIN_OUTPUT_MONITORING,
        (uint8_t)AUTOMATIC_MODE_SYNC,
        250,
        PWM_DEFAULT_DELAY_US,
        &usRemainingDurationMs);
  ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  // Handler:
  ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  if ((ulMillisNow - ulMillisStart) >= ulSyncMillis) {
    float fCurrentTemp = readThermistor();

    if (fCurrentTemp > fTempStart && fCurrentTemp - fTempStart >= SYNC_MODE_TEMP_DELTA) {
      disableOutput(NULL);
      fTempStart = fCurrentTemp;
    } else if (fTempStart - fCurrentTemp >= SYNC_MODE_TEMP_DELTA) {
      enableOutput(NULL);
      fTempStart = fCurrentTemp;
    }
    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    // Else, not enough temperature difference.
    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    ulMillisStart = ulMillisNow;
  }
  ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  // Else, not enough time has passed to check.
  ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  delayInternalMilliseconds(usRemainingDurationMs, interruptHandler);
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Handles the automatic intermittent mode.
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void handleAutomaticIntermittentMode(void) {
  static unsigned long ulMillisStart = millis();
  static unsigned long ulIntermittentMillis = (INTERMITTENT_MODE_CYCLE_MINUTES >= 1 ? INTERMITTENT_MODE_CYCLE_MINUTES : 1) * 60 * 1000;
  unsigned long ulMillisNow = millis();
  ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  // Pulse the selected automatic mode and let the cycle run.
  ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  uint16_t usRemainingDurationMs = HANDLER_TOTAL_DURATION_MS;
  pulse(PIN_OUTPUT_MONITORING,
        (uint8_t)AUTOMATIC_MODE_INTERMITTENT,
        250,
        PWM_DEFAULT_DELAY_US,
        &usRemainingDurationMs);
  ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  // Handler:
  ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  if ((ulMillisNow - ulMillisStart) >= ulIntermittentMillis) {
    if (ubOutputEnabled) {
      disableOutput(NULL);
    } else {
      enableOutput(NULL);
    }
    ulMillisStart = ulMillisNow;
  }
  ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  // Else, not enough time has passed to check.
  ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  delayInternalMilliseconds(usRemainingDurationMs, interruptHandler);
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Handles the automatic timer mode.
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void handleAutomaticTimerMode(void) {
  ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  // Template: { Day [1 (Sunday) - 7 (Saturday)], Hour [0 - 23], Minute [0 - 59], State [true/false]}
  // The handler assumes day and time is ordered chronologically. Missing days are allowed.
  ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  /*
  static const TIMER_STATE_T timerStatesTemplate[] = {
    { DAY_SUNDAY, 0, 0, true },
    { DAY_MONDAY, 0, 0, true },
    { DAY_TUESDAY, 0, 0, true },
    { DAY_WEDNESDAY, 0, 0, true },
    { DAY_THURSDAY, 0, 0, true },
    { DAY_FRIDAY, 0, 0, true },
    { DAY_SATURDAY, 0, 0, true }
  };
  //*/
  static const TIMER_STATE_T timerStates[] = {
    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    { DAY_SUNDAY, 8, 30, true },
    { DAY_SUNDAY, 12, 10, false },
    { DAY_SUNDAY, 13, 0, true },
    { DAY_SUNDAY, 20, 30, false },
    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    { DAY_MONDAY, 8, 30, true },
    { DAY_MONDAY, 12, 10, false },
    { DAY_MONDAY, 13, 0, true },
    { DAY_MONDAY, 20, 30, false },
    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    { DAY_TUESDAY, 8, 30, true },
    { DAY_TUESDAY, 12, 10, false },
    { DAY_TUESDAY, 13, 0, true },
    { DAY_TUESDAY, 20, 30, false },
    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    { DAY_WEDNESDAY, 8, 30, true },
    { DAY_WEDNESDAY, 12, 10, false },
    { DAY_WEDNESDAY, 13, 0, true },
    { DAY_WEDNESDAY, 20, 30, false },
    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    { DAY_THURSDAY, 8, 30, true },
    { DAY_THURSDAY, 12, 10, false },
    { DAY_THURSDAY, 13, 0, true },
    { DAY_THURSDAY, 20, 30, false },
    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    { DAY_FRIDAY, 8, 30, true },
    { DAY_FRIDAY, 12, 10, false },
    { DAY_FRIDAY, 13, 0, true },
    { DAY_FRIDAY, 20, 30, false },
    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    { DAY_SATURDAY, 8, 30, true },
    { DAY_SATURDAY, 12, 10, false },
    { DAY_SATURDAY, 13, 0, true },
    { DAY_SATURDAY, 20, 30, false },
  };
  static const uint16_t usTimerStateCount = sizeof(timerStates) / sizeof(timerStates[0]);
  static uint16_t usContinueFromIndex = 0;
  uint16_t usCurrentIndex = 0;
  int nNowWeekday = weekday(), nNowHour = hour(), nNowMinute = minute();
  ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  // Pulse the selected automatic mode and let the cycle run.
  ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  uint16_t usRemainingDurationMs = HANDLER_TOTAL_DURATION_MS;
  pulse(PIN_OUTPUT_MONITORING,
        (uint8_t)AUTOMATIC_MODE_TIMER,
        250,
        PWM_DEFAULT_DELAY_US,
        &usRemainingDurationMs);
  ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  // Handler:
  ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  if (usTimerStateCount > 0) {
    for (usCurrentIndex = usContinueFromIndex; usCurrentIndex < usTimerStateCount; usCurrentIndex++) {
      TIMER_STATE_T currentTimerState = timerStates[usCurrentIndex];
      if (currentTimerState.eWeekday == nNowWeekday && (currentTimerState.ubHour > nNowHour || (currentTimerState.ubHour == nNowHour && currentTimerState.ubMinute >= nNowMinute))) {
        /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        // This is the upcoming time.
        /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        usContinueFromIndex = usCurrentIndex;
        break;
      }
      ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
      // We've cached the continuation index but we're at the end of the array; reset it.
      ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
      if (usContinueFromIndex > 0 && usCurrentIndex >= usTimerStateCount - 1) {
        usContinueFromIndex = 0;
        break;
      }
    }
    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    // The prior entry in this array will determine the last known state on initial boot. Wrap to the last element if this is
    // the first element, i.e. there an upcoming Sunday time, but the last known state was on a Thursday.
    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    uint16_t usLastStateIndex = 0;
    if (usCurrentIndex == 0 && usTimerStateCount > 1) {
      usLastStateIndex = usTimerStateCount - 1;
    } else {
      usLastStateIndex = usCurrentIndex - 1;
    }
    uint8_t ubLastOutputState = timerStates[usLastStateIndex].ubOutputState;
    if (ubLastOutputState) {
      enableOutput(NULL);
    } else {
      disableOutput(NULL);
    }
  } else {
    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    // No times are configured.
    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    disableOutput(NULL);
  }
  ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  delayInternalMilliseconds(usRemainingDurationMs, interruptHandler);
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
void putCurrentGlobalMode(size_t stAddress, GLOBAL_MODE_T eCurrentGlobalMode) {
  if (SERIAL_OUT) {
    Serial.print("Global Mode EEPROM.put(): ");
    Serial.print(eCurrentGlobalMode);
    Serial.print(", ");
    Serial.println(getGlobalModeName(eCurrentGlobalMode));
  }
  EEPROM.put(stAddress, eCurrentGlobalMode);
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
inline const char* const getGlobalModeName(GLOBAL_MODE_T eGlobalMode) {
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
void putCurrentAutomaticMode(size_t stAddress, AUTOMATIC_MODE_T eCurrentAutomaticMode) {
  if (SERIAL_OUT) {
    Serial.print("Automatic Mode EEPROM.put(): ");
    Serial.print(eCurrentAutomaticMode);
    Serial.print(", ");
    Serial.println(getAutomaticModeName(eCurrentAutomaticMode));
  }
  EEPROM.put(stAddress, eCurrentAutomaticMode);
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
inline const char* const getAutomaticModeName(AUTOMATIC_MODE_T eAutomaticMode) {
  if (eAutomaticMode >= AUTOMATIC_MODE_UNKNOWN && eAutomaticMode < AUTOMATIC_MODE_COUNT) {
    return AUTOMATIC_MODE_NAMES[eAutomaticMode];
  }
  return "UNKNOWN";
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
inline const char* const getDayName(DAY_T eDay) {
  if (eDay >= DAY_UNKNOWN && eDay < DAY_COUNT) {
    return DAY_NAMES[eDay];
  }
  return "UNKNOWN";
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
inline const char* const getErrorCodeName(ERROR_CODE_T eErrorCode) {
  if (eErrorCode >= ERROR_CODE_UNKNOWN && eErrorCode < ERROR_CODE_COUNT) {
    return ERROR_CODE_NAMES[eErrorCode];
  }
  return "UNKNOWN";
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Internal second delay handler to check on tasks while waiting.
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void delayInternalSeconds(uint16_t usDelaySeconds, void (*p_fnInterruptHandler)(void)) {
  unsigned long ulMillisStart = millis();
  unsigned long usDelayMs = usDelaySeconds * 1000;

  if (p_fnInterruptHandler) {
    while ((millis() - ulMillisStart) < usDelayMs) {
      p_fnInterruptHandler();
    }
  } else {
    while ((millis() - ulMillisStart) < usDelayMs) {
      ;
    }
  }
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Internal millisecond delay handler to check on tasks while waiting.
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void delayInternalMilliseconds(uint16_t usDelayMs, void (*p_fnInterruptHandler)(void)) {
  unsigned long ulMillisStart = millis();

  if (p_fnInterruptHandler) {
    while ((millis() - ulMillisStart) < usDelayMs) {
      p_fnInterruptHandler();
    }
  } else {
    while ((millis() - ulMillisStart) < usDelayMs) {
      ;
    }
  }
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Internal microsecond delay handler to check on tasks while waiting.
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void delayInternalMicroseconds(uint16_t usDelayUs, void (*p_fnInterruptHandler)(void)) {
  unsigned long ulMicrosStart = micros();

  if (p_fnInterruptHandler) {
    while ((micros() - ulMicrosStart) < usDelayUs) {
      p_fnInterruptHandler();
    }
  } else {
    while ((micros() - ulMicrosStart) < usDelayUs) {
      ;
    }
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

      /*
      if (SERIAL_OUT) {
        Serial.print("Button trigger, pin: ");
        Serial.println(ubSwitchPin);
      }
      //*/

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
  ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  // We'll consider quiet times in automatic modes and prevent the output from enabling.
  ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  uint8_t ubCanEnableOutput = considerQuietTimes();
  ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  if (ubCanEnableOutput && !ubOutputEnabled) {
    ubOutputEnabled = true;
    digitalWrite(PIN_OUTPUT_ENABLE, ubOutputEnabled);
    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    if (SERIAL_OUT) {
      Serial.print("Enabled output: ");
      if (p_fCurrentTemp) {
        Serial.print(*p_fCurrentTemp, 2);
        Serial.println(" Degrees C trigger.");
      } else {
        Serial.println("Manual/Intermittent/Timer mode trigger.");
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
        Serial.println("Error/Manual/Intermittent/Timer mode trigger.");
      }
    }
  }
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Sets the Arduino's clock to the compiled time of the program.
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void setupTime(void) {
  const char szCompiledTime[] = __DATE__ " " __TIME__;
  const char szMonthAbbreviations[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
  char szMonthBuffer[16];
  int nNowHour, nNowMinute, nNowSecond, nNowDay, nNowYear = 0;
  sscanf(szCompiledTime, "%s %d %d %d:%d:%d", szMonthBuffer, &nNowDay, &nNowYear, &nNowHour, &nNowMinute, &nNowSecond);
  int nNowMonth = (strstr(szMonthAbbreviations, szMonthBuffer) - szMonthAbbreviations) / 3 + 1;
  setTime(nNowHour, nNowMinute, nNowSecond, nNowDay, nNowMonth, nNowYear);
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Writes whatever the Arduino's configured clock was set as to the serial console.
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void setupPrintTime(void) {
  char szBuffer[19];
  sprintf(szBuffer, "%4d-%02d-%02d %02d:%02d:%02d", year(), month(), day(), hour(), minute(), second());

  if (SERIAL_OUT) {
    Serial.println(szBuffer);
  }
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void handleError(ERROR_CODE_T eErrorCode, const char* const szErrorDescription) {
  disableOutput(NULL);
  ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  if (SERIAL_OUT) {
    Serial.print("Error Code: ");
    Serial.print(eErrorCode);
    Serial.print(", ");
    Serial.println(getErrorCodeName(eErrorCode));
    Serial.print("Error Description: ");
    Serial.println(szErrorDescription);
  }
  ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  uint16_t usRemainingDurationMs = HANDLER_TOTAL_DURATION_MS;
  pulse(PIN_OUTPUT_ENABLE,
        (uint8_t)eErrorCode,
        250,
        PWM_DEFAULT_DELAY_US,
        NULL);
  ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  delayInternalMilliseconds(usRemainingDurationMs, interruptHandler);
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////