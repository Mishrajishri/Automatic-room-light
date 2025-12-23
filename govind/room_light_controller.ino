/*
 * Enhanced Automatic Room Light Controller
 * Using Arduino Uno, 2 IR Sensors, 16x2 LCD, Relay Module
 * 
 * Enhanced Features:
 * - Improved direction detection with state machine
 * - Auto light ON/OFF based on occupancy
 * - LCD display showing person count and status
 * - +1 button to manually increment count
 * - Reset button to set count to 1
 * - Emergency override switch
 * - Sensor health monitoring
 * - Configuration mode
 */

#include <LiquidCrystal.h>
#include <EEPROM.h>

// LCD Pin Configuration (RS, EN, D4, D5, D6, D7)
LiquidCrystal lcd(12, 11, 5, 4, 3, 2);

// Pin Definitions
const int IR1_PIN = 7;       // Entry side IR sensor
const int IR2_PIN = 8;       // Exit side IR sensor
const int PLUS_BTN = 9;      // +1 Button
const int RESET_BTN = 10;    // Reset Button (sets count to 1)
const int RELAY_PIN = 6;     // Relay for light control
const int EMERGENCY_PIN = A1; // Emergency override switch

// Configuration variables (stored in EEPROM)
struct Config {
  unsigned long timeout;
  unsigned long debounceDelay;
  unsigned long btnDebounce;
  int maxPersons;
  bool emergencyOverride;
};

Config config;
const int CONFIG_EEPROM_ADDR = 0;

// State machine for direction detection
enum SensorState {
  IDLE,
  IR1_DETECTED,
  IR2_DETECTED,
  ENTRY_DETECTED,
  EXIT_DETECTED
};

// Variables
int personCount = 0;
SensorState currentState = IDLE;
unsigned long stateTime = 0;
unsigned long lastSensorActivity = 0;
unsigned long lastEmergencyCheck = 0;

// Health monitoring
bool ir1Stuck = false;
bool ir2Stuck = false;
bool configMode = false;
unsigned long configStartTime = 0;
int configValue = 0;
int configParam = 0; // 0=timeout, 1=debounce, 2=maxPersons

// Button states for configuration
bool lastPlusBtnState = HIGH;
bool lastResetBtnState = HIGH;
bool configModeEntered = false;

// Config mode timing variables
unsigned long lastResetBtnTime = 0;
unsigned long lastPlusBtnTime = 0;

void setup() {
  // Load configuration from EEPROM
  loadConfiguration();
  
  // Initialize LCD
  lcd.begin(16, 2);
  lcd.clear();
  lcd.print("Room Light Ctrl");
  lcd.setCursor(0, 1);
  lcd.print("Initializing...");
  
  // Pin Modes
  pinMode(IR1_PIN, INPUT);
  pinMode(IR2_PIN, INPUT);
  pinMode(PLUS_BTN, INPUT_PULLUP);  // Using internal pull-up
  pinMode(RESET_BTN, INPUT_PULLUP); // Using internal pull-up
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(EMERGENCY_PIN, INPUT_PULLUP); // Emergency override switch
  
  // Initial state - Light OFF
  digitalWrite(RELAY_PIN, LOW);
  
  delay(1500);
  updateDisplay();
  
  Serial.begin(9600);
  Serial.println("Enhanced Room Light Controller Started");
  Serial.println("Emergency override: Pin A1");
}

void loop() {
  unsigned long currentTime = millis();
  
  // Check for configuration mode (long press both buttons)
  checkConfigurationMode(currentTime);
  
  if (configMode) {
    handleConfigurationMode(currentTime);
    return;
  }
  
  // Check emergency override
  checkEmergencyOverride(currentTime);
  
  // Check sensor health
  checkSensorHealth(currentTime);
  
  // Read IR sensors (LOW = Object detected for most IR modules)
  bool ir1State = digitalRead(IR1_PIN) == LOW;
  bool ir2State = digitalRead(IR2_PIN) == LOW;
  
  // Read buttons (LOW = Pressed due to pull-up)
  bool plusBtnState = digitalRead(PLUS_BTN) == LOW;
  bool resetBtnState = digitalRead(RESET_BTN) == LOW;
  
  // Update last sensor activity for health monitoring
  if (ir1State || ir2State) {
    lastSensorActivity = currentTime;
  }
  
  // ----- Enhanced Direction Detection with State Machine -----
  handleDirectionDetection(ir1State, ir2State, currentTime);
  
  // ----- Handle Buttons with Enhanced Debouncing -----
  handleButtons(plusBtnState, resetBtnState, currentTime);
  
  // Update display periodically
  static unsigned long lastDisplayUpdate = 0;
  if (currentTime - lastDisplayUpdate > 500) {
    updateDisplay();
    lastDisplayUpdate = currentTime;
  }
  
  delay(10);  // Small delay for stability
}

// Configuration Functions
void loadConfiguration() {
  // Load config from EEPROM or use defaults
  EEPROM.get(CONFIG_EEPROM_ADDR, config);
  
  // Check if EEPROM is empty (first run)
  if (EEPROM.read(CONFIG_EEPROM_ADDR) == 255) {
    // Set default values
    config.timeout = 5000;
    config.debounceDelay = 200;
    config.btnDebounce = 300;
    config.maxPersons = 99;
    config.emergencyOverride = false;
    saveConfiguration();
  }
}

void saveConfiguration() {
  EEPROM.put(CONFIG_EEPROM_ADDR, config);
}

// Configuration Mode Functions
void checkConfigurationMode(unsigned long currentTime) {
  static bool bothPressed = false;
  static unsigned long pressStartTime = 0;
  
  bool plusBtnState = digitalRead(PLUS_BTN) == LOW;
  bool resetBtnState = digitalRead(RESET_BTN) == LOW;
  
  if (plusBtnState && resetBtnState) {
    if (!bothPressed) {
      pressStartTime = currentTime;
      bothPressed = true;
    } else if (currentTime - pressStartTime > 3000) {
      // Enter configuration mode
      configMode = true;
      configModeEntered = true;
      configStartTime = currentTime;
      configParam = 0; // Start with timeout
      showConfigMode();
    }
  } else {
    bothPressed = false;
  }
}

void showConfigMode() {
  lcd.clear();
  lcd.print("CONFIG MODE");
  lcd.setCursor(0, 1);
  lcd.print("Timeout: ");
  lcd.print(config.timeout);
  lcd.print("ms");
}

void handleConfigurationMode(unsigned long currentTime) {
  bool plusBtnState = digitalRead(PLUS_BTN) == LOW;
  bool resetBtnState = digitalRead(RESET_BTN) == LOW;

  // Manual exit: short press both buttons (0.5-2 seconds)
  static bool exitPressed = false;
  static unsigned long exitPressTime = 0;
  if (plusBtnState && resetBtnState) {
    if (!exitPressed) {
      exitPressTime = currentTime;
      exitPressed = true;
    } else if (currentTime - exitPressTime >= 500 && currentTime - exitPressTime < 2000) {
      configMode = false;
      saveConfiguration();
      updateDisplay();
      return;
    }
  } else {
    exitPressed = false;
  }

  // Exit config mode after 30 seconds
  if (currentTime - configStartTime > 30000) {
    configMode = false;
    saveConfiguration();
    updateDisplay();
    return;
  }

  // Change parameter with reset button
  if (resetBtnState && (currentTime - lastResetBtnTime > config.btnDebounce)) {
    configParam = (configParam + 1) % 3;
    lastResetBtnTime = currentTime;
    updateConfigDisplay();
  }

  // Adjust value with +1 button
  if (plusBtnState && (currentTime - lastPlusBtnTime > config.btnDebounce)) {
    adjustConfigValue();
    saveConfiguration();
    lastPlusBtnTime = currentTime;
    updateConfigDisplay();
  }
}

void updateConfigDisplay() {
  lcd.clear();
  switch (configParam) {
    case 0:
      lcd.print("Timeout: ");
      lcd.print(config.timeout);
      lcd.print("ms");
      break;
    case 1:
      lcd.print("Debounce: ");
      lcd.print(config.debounceDelay);
      lcd.print("ms");
      break;
    case 2:
      lcd.print("Max Persons: ");
      lcd.print(config.maxPersons);
      break;
  }
  lcd.setCursor(0, 1);
  lcd.print("R:param +:value");
}

void adjustConfigValue() {
  switch (configParam) {
    case 0: // Timeout
      config.timeout += 100;
      if (config.timeout > 10000) config.timeout = 100;
      break;
    case 1: // Debounce
      config.debounceDelay += 50;
      if (config.debounceDelay > 1000) config.debounceDelay = 50;
      break;
    case 2: // Max Persons
      config.maxPersons += 1;
      if (config.maxPersons > 200) config.maxPersons = 1;
      break;
  }
}

// Emergency Override Functions
void checkEmergencyOverride(unsigned long currentTime) {
  if (currentTime - lastEmergencyCheck > 100) { // Check every 100ms
    bool emergencyState = digitalRead(EMERGENCY_PIN) == LOW;
    
    if (emergencyState) {
      config.emergencyOverride = true;
      digitalWrite(RELAY_PIN, HIGH); // Force light ON
      showEmergencyStatus();
    } else if (config.emergencyOverride) {
      config.emergencyOverride = false;
      updateLight(); // Return to normal operation
      updateDisplay();
    }
    
    lastEmergencyCheck = currentTime;
  }
}

void showEmergencyStatus() {
  lcd.clear();
  lcd.print("EMERGENCY MODE");
  lcd.setCursor(0, 1);
  lcd.print("Light: FORCE ON");
}

// Sensor Health Monitoring
void checkSensorHealth(unsigned long currentTime) {
  static bool lastIr1State = (digitalRead(IR1_PIN) == LOW);
  static bool lastIr2State = (digitalRead(IR2_PIN) == LOW);
  static unsigned long ir1ChangeTime = currentTime;
  static unsigned long ir2ChangeTime = currentTime;

  bool ir1State = (digitalRead(IR1_PIN) == LOW);
  bool ir2State = (digitalRead(IR2_PIN) == LOW);
  
  // Check for stuck sensors (no change for 60 seconds)
  if (ir1State == lastIr1State) {
    if (currentTime - ir1ChangeTime > 60000 && !ir1Stuck) {
      ir1Stuck = true;
      showError("IR1 STUCK");
    }
  } else {
    ir1ChangeTime = currentTime;
    ir1Stuck = false;
  }
  
  if (ir2State == lastIr2State) {
    if (currentTime - ir2ChangeTime > 60000 && !ir2Stuck) {
      ir2Stuck = true;
      showError("IR2 STUCK");
    }
  } else {
    ir2ChangeTime = currentTime;
    ir2Stuck = false;
  }
  
  lastIr1State = ir1State;
  lastIr2State = ir2State;
  
  // Check for no sensor activity (room might be stuck)
  if (currentTime - lastSensorActivity > 300000 && personCount > 0) { // 5 minutes
    showError("NO ACTIVITY");
  }
}

void showError(const char* error) {
  lcd.clear();
  lcd.print("ERROR: ");
  lcd.print(error);
  lcd.setCursor(0, 1);
  lcd.print("Check sensors");
  delay(2000);
  updateDisplay();
}

// Enhanced Direction Detection
void handleDirectionDetection(bool ir1State, bool ir2State, unsigned long currentTime) {
  switch (currentState) {
    case IDLE:
      if (ir1State) {
        currentState = IR1_DETECTED;
        stateTime = currentTime;
      } else if (ir2State) {
        currentState = IR2_DETECTED;
        stateTime = currentTime;
      }
      break;
      
    case IR1_DETECTED:
      if (ir2State) {
        // Entry detected: IR1 -> IR2
        if (currentTime - stateTime < config.timeout) {
          if (personCount < config.maxPersons) {
            personCount++;
            showStatus("IN");
            updateLight();
            Serial.print("Person Entered! Count: ");
            Serial.println(personCount);
          } else {
            showError("MAX PERSONS");
          }
        }
        currentState = IDLE;
      } else if (currentTime - stateTime > config.timeout) {
        currentState = IDLE; // Timeout
      }
      break;
      
    case IR2_DETECTED:
      if (ir1State) {
        // Exit detected: IR2 -> IR1
        if (currentTime - stateTime < config.timeout) {
          if (personCount > 0) {
            personCount--;
            showStatus("OUT");
            updateLight();
            Serial.print("Person Exited! Count: ");
            Serial.println(personCount);
          }
        }
        currentState = IDLE;
      } else if (currentTime - stateTime > config.timeout) {
        currentState = IDLE; // Timeout
      }
      break;
  }
}

// Enhanced Button Handling
void handleButtons(bool plusBtnState, bool resetBtnState, unsigned long currentTime) {
  static unsigned long lastPlusTime = 0;
  static unsigned long lastResetTime = 0;
  
  // +1 Button with enhanced debouncing
  if (plusBtnState && (currentTime - lastPlusTime > config.btnDebounce)) {
    if (personCount < config.maxPersons) {
      personCount++;
      showStatus("MANUAL");
      updateLight();
      Serial.print("+1 Button Pressed! Count: ");
      Serial.println(personCount);
    } else {
      showError("MAX PERSONS");
    }
    lastPlusTime = currentTime;
  }
  
  // Reset Button with enhanced debouncing
  if (resetBtnState && (currentTime - lastResetTime > config.btnDebounce)) {
    personCount = 1;
    showStatus("RESET");
    updateLight();
    Serial.println("Reset Button Pressed! Count set to 1");
    lastResetTime = currentTime;
  }
}

// Enhanced Display Functions
void updateDisplay() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Persons: ");
  lcd.print(personCount);
  
  lcd.setCursor(0, 1);
  if (config.emergencyOverride) {
    lcd.print("Light: EMER ON");
  } else {
    lcd.print("Light: ");
    if (personCount > 0) {
      lcd.print("ON ");
    } else {
      lcd.print("OFF");
    }
  }
  
  // Show health status if needed
  if (ir1Stuck || ir2Stuck) {
    lcd.setCursor(12, 0);
    lcd.print("ERR");
  }
}

void showStatus(const char* status) {
  // Enhanced status display with animation
  lcd.setCursor(13, 0);
  lcd.print(status);
  delay(500);
  lcd.setCursor(13, 0);
  lcd.print("   "); // Clear status
}

void updateLight() {
  if (config.emergencyOverride) {
    digitalWrite(RELAY_PIN, HIGH); // Emergency override
  } else {
    if (personCount > 0) {
      digitalWrite(RELAY_PIN, HIGH);  // Light ON
    } else {
      digitalWrite(RELAY_PIN, LOW);   // Light OFF
    }
  }
}
