#include <avr/wdt.h>

// ---------------------- Variables ----------------------

// Pin definition
const byte segmentPins[7] = {2,3,4,5,10,11,12};   // A B C D E G F (SEQUENCE)
const byte digitPins[4] = {6,7,8,9};   // d1 d2 d3 d4 (Left to right)
const byte colonPin = 13;
const byte buzzerPin = A3;

const byte modeBtn = A0;
const byte incrementBtn = A1;

const byte alarmModeBtn = A2;
const byte stopAlarmBtn = A4;

// Digit Pattern (0-9) [1 ---> Segement on]
const byte patterns[10] = {
  0b1111101,    // 0 ABCDEF
  0b0000101,    // 1 EF
  0b1011011,    // 2 AFGCD
  0b1001111,    // 3 AFGED
  0b0100111,    // 4 BEFG
  0b1101110,    // 5 ABDEG
  0b1111110,    // 6 ACDEFG
  0b1000101,    // 7 AEF
  0b1111111,    // 8 ABCDEFG
  0b1101111     // 9 ABDEFG
};

// Global time variables
unsigned long previousMillis = 0;
byte seconds = 0;
byte minutes = 0;
byte hours   = 12;     // Starts from 12:00
bool isPM = false;     // false = AM, true = PM

// Set mode variables
byte mode = 0;                // 0=run, 1=set hours, 2=set minutes
unsigned long lastBlink = 0;
bool blinkState = true;       // For blinking selected field

// Alarm Variables
byte alarmMinutes = 0;
byte alarmHours = 6;
bool alarmIsPM = false;     // Alarm AM/PM setting
bool alarmActive = false;
byte alarmMode = 0;          // 0=run, 1=set hours, 2=set minutes 
unsigned long alarmStartTime = 0;
const unsigned long alarmDuration = 60000;    // Alarm rings for 60 seconds

// Alarm On/Off Variables
bool alarmEnabled = true;   // true=ON, false=OFF
bool alarmToggleBlink = false;
unsigned long alarmToggleTime = 0;

// Snooze Variables
bool snoozeActive = false;
unsigned long snoozeStartTime = 0;
const unsigned long snoozeDuration = 300000;   // 5 minutes
unsigned long stopBtnPressTime = 0;

// Button state tracking with debounce
bool modeBtnPressed = false;
bool incBtnPressed = false;
bool alarmModeBtnPressed = false;
bool stopAlarmBtnPressed = false;

unsigned long lastModePress = 0;
unsigned long lastIncPress = 0;
unsigned long lastAlarmModePress = 0;
unsigned long lastStopAlarmPress = 0;

const unsigned long debounceDelay = 200;

// ---------------------- SETUP ----------------------

void setup() {
  // Segment pins as output
  for (byte i = 0; i < 7; i++) {
    pinMode(segmentPins[i], OUTPUT);
    digitalWrite(segmentPins[i], LOW);
  }

  // Digit pins as output (transistor bases)
  for (byte i = 0; i < 4; i++) {
    pinMode(digitPins[i], OUTPUT);
    digitalWrite(digitPins[i], LOW);   // digits off
  }

  // Colon pin
  pinMode(colonPin, OUTPUT);
  digitalWrite(colonPin, LOW);

  // Buzzer pin
  pinMode(buzzerPin, OUTPUT);
  digitalWrite(buzzerPin, LOW);

  // Buttons with internal pull-up resistors
  pinMode(modeBtn, INPUT_PULLUP); 
  pinMode(incrementBtn, INPUT_PULLUP);
  pinMode(alarmModeBtn, INPUT_PULLUP);
  pinMode(stopAlarmBtn, INPUT_PULLUP);

  Serial.begin(9600);    // For debugging
  Serial.println("Clock started at ");
  Serial.println(millis());
  wdt_disable();     // Watchdog Disable

}

// ---------------------- BUTTON HANDLING ----------------------

bool checkButton(int pin, bool &wasPressed, unsigned long &pressStartTime, unsigned long &pressDuration) {

  bool isPressed = (digitalRead(pin) == LOW); // LOW means pressed (pull-up)
  unsigned long now = millis();
  pressDuration = 0;
  
  if (isPressed && !wasPressed) {
    if (now - pressStartTime > debounceDelay) {
      wasPressed = true;
      pressStartTime = now;
      pressDuration = 0;
      return true; // Button was just pressed
    }

  } else if (isPressed && wasPressed) {
    pressDuration = now - pressStartTime;
    return false;    // Still held - duration updated

  } else if (!isPressed && wasPressed) {
    pressDuration = now - pressStartTime;
    wasPressed = false;
    return true;     // released - duration is final
  } 
  
  return false; // No new press
}


// ---------------------- ALARM FUNCTIONS ----------------------

void alarmTone() {
  static unsigned long lastChange = 0;
  static byte state = 0;

  unsigned long now = millis();
  if (now - lastChange >= 200) {     // Chnage every 200ms
    lastChange = now;
    state = (state + 1) % 3;

    if (state ==0) {
      tone(buzzerPin, 1000);
    } else if (state == 1) {
      tone(buzzerPin, 1500);
    } else if (state == 2) {
      tone(buzzerPin, 2000);
    }
  }

}

void checkAlarm() {
  if (!alarmEnabled || alarmActive) return;

  // Convert 12-hour to actual 24-hour for comparison
  byte actualAlarmHours = alarmHours;
  if (alarmIsPM && alarmHours != 12) actualAlarmHours += 12;
  if (!alarmIsPM && alarmHours == 12) actualAlarmHours = 0;  // 12 AM = 0
  
  byte actualCurrentHours = hours;
  if (isPM && hours != 12) actualCurrentHours += 12;
  if (!isPM && hours == 12) actualCurrentHours = 0;  // 12 AM = 0

  // Check if current time matches alarm time (including AM/PM)
  if (actualCurrentHours == actualAlarmHours && minutes == alarmMinutes && seconds == 0) {
    alarmActive = true;
    alarmStartTime = millis();
    snoozeActive = false;    // Clear any pending snooze
    Serial.println("ALARM TRIGGERED!");
  }
}

void checkSnooze() {
  if (!snoozeActive || alarmActive) return;
  
  unsigned long currentMillis = millis();
  if (currentMillis - snoozeStartTime >= snoozeDuration) {
    snoozeActive = false;
    alarmActive = true;
    alarmStartTime = currentMillis;
    Serial.println("SNOOZE FINISHED - Alarm ringing again");
  }
}

void handleAlarm() {
  if (!alarmActive) return;
  
  unsigned long currentMillis = millis();
  
  // Auto-stop after alarmDuration
  if (currentMillis - alarmStartTime >= alarmDuration) {
    alarmActive = false;
    snoozeActive = true;   // Start snooze
    snoozeStartTime = currentMillis;
    noTone(buzzerPin);
    Serial.println("Alarm auto-stopped & snooze activated");
    return;
  }
  
  alarmTone();
}

void alarmSet() {
  // Handle Alarm Set Button (A2) - short vs long press 
  unsigned long pressDuration = 0;
  // call the checkbutton function
  bool alarmEvent = checkButton(alarmModeBtn, alarmModeBtnPressed, lastAlarmModePress, pressDuration);

  // Long press - act on release
  if (alarmEvent && !alarmModeBtnPressed) {
    if (pressDuration >= 3000) {
      alarmEnabled = !alarmEnabled;     // Alarm Enable/Disable
      alarmToggleBlink = true;
      alarmToggleTime = millis();

      Serial.print("Alarm now ");
      Serial.println(alarmEnabled ? "ENABLED" : "DISABLED");

    } else {
      // Short press
      if (mode == 0 && !alarmActive && !snoozeActive) {
        alarmMode = (alarmMode + 1) % 3;
      
        Serial.print("Alarm Mode changed to: ");
        Serial.println(alarmMode);
      
        if (alarmMode == 0) {
          previousMillis = millis();     // prevent time jump
          blinkState = true;
          Serial.print("Alarm set to: ");
          Serial.print(alarmHours);
          Serial.print(":");
          Serial.print(alarmMinutes);
          Serial.println(alarmIsPM ? " PM" : " AM");
        }
      }  
    }
  }
}

void stopOrSnooze() {
  unsigned long pressDuration = 0;
  bool snoozeEvent = checkButton(stopAlarmBtn, stopAlarmBtnPressed, lastStopAlarmPress, pressDuration);

  // Only act on release
  if (snoozeEvent && !stopAlarmBtnPressed) {
    if (alarmActive) {
      if (pressDuration >= 1000) {
        // Long press → Stop completely
        alarmActive = false;
        snoozeActive = false;
        noTone(buzzerPin);
        Serial.println("Alarm STOPPED completely (long press)");
      
      } else {
        // Short press → Snooze
        alarmActive = false;
        snoozeActive = true;
        snoozeStartTime = millis();
        noTone(buzzerPin);
        Serial.println("Alarm SNOOZED - Will ring again in 5 minutes");
      }
    } else if (snoozeActive) {
      // Cancel snooze if pressed during snooze period
      snoozeActive = false;
      Serial.println("Snooze CANCELLED");
    }
  }
}

// ---------------------- TIME FUNCTIONS ----------------------

void clockSet() {
  unsigned long pressDuration = 0;
  bool setEvent = checkButton(modeBtn, modeBtnPressed, lastModePress, pressDuration);

  // Handle mode button (A0)
  if (setEvent && !modeBtnPressed && alarmMode == 0 && !alarmActive && !snoozeActive) {
    mode = (mode + 1) % 3;  // 0 → 1 → 2 → 0
    
    Serial.print("Mode changed to: ");
    Serial.println(mode);
    
    if (mode == 0) {
      // Exiting set mode - reset timer to avoid time jump
      previousMillis = millis();
      blinkState = true; // Reset blink state
      Serial.print("Time set to: ");
      Serial.print(hours);
      Serial.print(":");
      Serial.println(minutes);
      Serial.println(isPM ? " PM" : " AM");
    }
  }
}

void handleIncrement() {
  unsigned long pressDuration = 0;
  bool incEvent = checkButton(incrementBtn, incBtnPressed, lastIncPress, pressDuration);

  // Handle increment button (A1)
  if (incEvent && !incBtnPressed && !alarmActive && !snoozeActive) {
    if (alarmMode != 0) {
      // Setting alarm time
      if (alarmMode == 1) {  // set alarm hours
        alarmHours = (alarmHours % 12) + 1;  // 1-12 cycle
        if (alarmHours == 12) alarmIsPM = !alarmIsPM;    // Toggle AM/PM 
      } else if (alarmMode == 2) {  // set alarm minutes
        alarmMinutes = (alarmMinutes + 1) % 60;
      }
    } else if (mode != 0) {
      // Setting clock time
      if (mode == 1) {  // set clock hours
        hours = (hours % 12) + 1;  // 1-12 cycle
        if (hours == 12) isPM = !isPM;    // Toggle AM/PM
      } else if (mode == 2) {  // set clock minutes
        minutes = (minutes + 1) % 60;
        seconds = 0;
      }
    }
  }
}

// ---------------------- MAIN LOOP ----------------------
void loop() {
  
  // Handle Clock set system
  clockSet();

  // Handle Alarm On/Off & alarm set system
  alarmSet();

  // Handle Increment button
  handleIncrement();

  // Handle Alarm stop button
  stopOrSnooze();

  // Update time every 1000 ms (Not in set mode)
  unsigned long currentMillis = millis();
  static unsigned long compensation = 0;    // keep count across loop iteration
  if (mode == 0 && alarmMode == 0 && currentMillis - previousMillis >= 1000) {
    
    previousMillis += 1000;
    compensation ++;

    if (compensation >= 600) {     // 144 seconds drift a day
      seconds += 2;
      compensation = 0;
    } else seconds ++;

    if (seconds >= 60) {
      seconds -= 60;     // No lost compensation seconds
      minutes++;
      if (minutes >= 60) {
        minutes = 0;
        hours++;
        if (hours > 12) {
          hours = 1;   // 12-hour format
          isPM = !isPM;  // Toggle AM/PM when crossing 12-*
        }
      }
    }
    checkAlarm();    // Check for alarm trigger
  }
  checkSnooze();
  handleAlarm();     // Handle alarm beeping

  // Update blink state for set mode
  if (currentMillis - lastBlink >= 500) {
    blinkState = !blinkState;
    lastBlink = currentMillis;
  }

  // Fast multiplexing refresh (~ every 2 ms)
  static byte currentDigit = 0;
  static unsigned long lastRefresh = 0;

  if (currentMillis - lastRefresh >= 2) {
    lastRefresh = currentMillis;

    // Turn off previous digit
    digitalWrite(digitPins[currentDigit], LOW);
    delayMicroseconds(100);   // Prevents ghosting

    // Move to next digit
    currentDigit = (currentDigit + 1) % 4;

    // Clear all segments first
    for (byte i = 0; i < 7; i++) digitalWrite(segmentPins[i], LOW);

    // Get the number to show for this digit
    byte numberToShow;
    byte displayHours, displayMinutes;

    // Determine whether to show clock time or alarm time
    if (alarmMode !=  0) {
      // Show alarm time when in alarm setting mode
      displayHours = alarmHours;
      displayMinutes = alarmMinutes;
    } else {
      // Show clock time
      displayHours = hours;
      displayMinutes = minutes;
    }

    switch (currentDigit) {
      case 0: numberToShow = displayHours / 10;            // tens of hours
        if (numberToShow == 0) numberToShow = 10;      // Show blank for leading 0
        break;
      case 1: numberToShow = displayHours % 10;   break;   // units of hours
      case 2: numberToShow = displayMinutes / 10; break;   // tens of minutes
      case 3: numberToShow = displayMinutes % 10; break;   // units of minutes
    }

    // Check if we should blink this digit 
    bool shouldShow = true;

    // Clock setting blink
    if (mode == 1 && (currentDigit == 0 || currentDigit == 1)) {  // hours digits
      shouldShow = blinkState;
    } else if (mode == 2 && (currentDigit == 2 || currentDigit == 3)) {  // minutes digits
      shouldShow = blinkState;
    }else if (alarmMode == 1 && (currentDigit == 0 || currentDigit == 1)) {
      shouldShow = blinkState;
    } else if (alarmMode == 2 && (currentDigit == 2 || currentDigit == 3)) {
      shouldShow = blinkState;
    }

    // Light segments according to pattern (common cathode: HIGH = on)
    if (shouldShow && numberToShow < 10) {
      byte pattern = patterns[numberToShow];
      for (byte seg = 0; seg < 7; seg++) {
        if (bitRead(pattern, 6 - seg)) {           // MSB is A, LSB is F
          digitalWrite(segmentPins[seg], HIGH);    
        }
      }
    }

    // Colon behavior
    bool colonOn = (seconds % 2 == 0);   // On in even numbers
    if (alarmActive) {
      // Fast blink when alarm is ringing
      digitalWrite(colonPin, (millis() / 250) % 2 == 0 ? HIGH : LOW);
    } else if (alarmToggleBlink) {
      // Fast Blink for 1 second
      if (millis() - alarmToggleTime < 1000) {
        digitalWrite(colonPin, (millis() / 200) % 2 ? HIGH : LOW);
      } else {
        alarmToggleBlink = false;
      }
    } else if(snoozeActive) {
      // Slow blink when snooze is pending (1 second cycle)
      digitalWrite(colonPin, (millis() / 500) % 2 == 0 ? HIGH : LOW);
    } else if (alarmMode == 1 || alarmMode == 2 || mode == 1 || mode == 2) {
      if (alarmMode != 0) {
        // In alarm setting mode - indicates AM/PM
        digitalWrite(colonPin, alarmIsPM ? HIGH : LOW);
      } else {
        // In clock setting mode - indicates AM/PM
        digitalWrite(colonPin, isPM ? HIGH : LOW);
      }   
    } else {
      // Normal blink in run mode
      digitalWrite(colonPin, colonOn ? HIGH : LOW);
    }
    
    // Turn on current digit (transistor base HIGH)
    digitalWrite(digitPins[currentDigit], HIGH);
  }
}




