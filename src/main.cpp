#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#if __has_include(<DHT.h>)
  #include <DHT.h>
#else
  #ifndef DHT11
    #define DHT11 11
  #endif

  class DHT {
  public:
    DHT(uint8_t pin, uint8_t type) : _pin(pin), _type(type) {}
    void begin() {}
    float readTemperature(bool force = false) {
      (void)force;
      return 0.0f;
    }
    float readHumidity() {
      return 0.0f;
    }

  private:
    uint8_t _pin;
    uint8_t _type;
  };
#endif

// --- SCREEN CONFIGURATION ---
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// --- PIN DEFINITIONS ---
#define PIN_XYT01_TEMP  34  // Analog pin reading XY-T01 output / NTC probe
#define PIN_DHT         4   // DHT11 sensor pin
#define PIN_RELAY_HEAT  16  // Vaporizer heater relay
#define PIN_RELAY_FAN   17  // Fan relay
#define PIN_RELAY_HUM   18  // Humidifier relay
#define PIN_BUTTON_START 19 // Push button to start process

// --- SENSOR CONFIGURATION ---
#define DHTTYPE DHT11
DHT dht(PIN_DHT, DHTTYPE);

// --- PROCESS SETPOINTS & PARAMETERS ---
const float TEMP_SETPOINT_C = 9.0;
const float TEMP_HYSTERESIS = 2.0;

const float HUM_SETPOINT_RH = 70.0;
const float HUM_HYSTERESIS = 3.0;

const unsigned long FUME_DURATION_SEC = 300; // Fuming cycle duration: 5 minutes
const unsigned long PURGE_DURATION_SEC = 180; // Post-fuming ventilation: 3 minutes

// --- SYSTEM STATES ---
enum SystemState {
  STATE_IDLE,
  STATE_FUMING,
  STATE_PURGING,
  STATE_COMPLETE
};

SystemState currentState = STATE_IDLE;
unsigned long stateStartTime = 0;
bool lastButtonState = HIGH;

// --- SENSOR READINGS ---
float chamberTempXY = 0.0;
float chamberTempDHT = 0.0;
float chamberHumidity = 0.0;

// Function prototype for reading XY-T01 analog input
float readXYT01Temperature() {
  int rawADC = analogRead(PIN_XYT01_TEMP);
  // Conversion formula mapped for 0-3.3V ADC corresponding to 0-100°C output scale
  float tempC = (rawADC / 4095.0) * 100.0; 
  return tempC;
}


void updateDisplay(unsigned long elapsedSec) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);

  // Line 1: Header / State
  display.print(F("MODE: "));
  switch (currentState) {
    case STATE_IDLE:     display.println(F("IDLE (Press Go)")); break;
    case STATE_FUMING:   display.println(F("FUMING...")); break;
    case STATE_PURGING:  display.println(F("PURGING FUMES")); break;
    case STATE_COMPLETE: display.println(F("DONE / SAFE")); break;
  }
  display.drawFastHLine(0, 10, 128, SSD1306_WHITE);

  // Line 2 & 3: Temperatures & Humidity
  display.setCursor(0, 16);
  display.print(F("Vap Temp: "));
  display.print(chamberTempXY, 1);
  display.println(F(" C"));

  display.setCursor(0, 28);
  display.print(F("Humidity: "));
  display.print(chamberHumidity, 1);
  display.println(F(" %RH"));

  // Line 4: Timer Remaining
  display.setCursor(0, 42);
  display.print(F("Time Rem: "));
  
  if (currentState == STATE_FUMING) {
    long rem = FUME_DURATION_SEC - elapsedSec;
    if (rem < 0) rem = 0;
    display.print(rem / 60); display.print(F("m "));
    display.print(rem % 60); display.print(F("s"));
  } else if (currentState == STATE_PURGING) {
    long rem = PURGE_DURATION_SEC - elapsedSec;
    if (rem < 0) rem = 0;
    display.print(rem / 60); display.print(F("m "));
    display.print(rem % 60); display.print(F("s"));
  } else {
    display.print(F("--:--"));
  }

  // Line 5: Relay status indicators
  display.setCursor(0, 54);
  display.print(F("H:")); display.print(digitalRead(PIN_RELAY_HEAT) ? F("ON ") : F("OFF"));
  display.print(F(" F:")); display.print(digitalRead(PIN_RELAY_FAN) ? F("ON ") : F("OFF"));
  display.print(F(" M:")); display.print(digitalRead(PIN_RELAY_HUM) ? F("ON") : F("OFF"));

  display.display();
}

void setup() {
  Serial.begin(115200);

  // Initialize Relays (Active HIGH standard; set LOW to ensure OFF initially)
  pinMode(PIN_RELAY_HEAT, OUTPUT);
  pinMode(PIN_RELAY_FAN, OUTPUT);
  pinMode(PIN_RELAY_HUM, OUTPUT);
  digitalWrite(PIN_RELAY_HEAT, LOW);
  digitalWrite(PIN_RELAY_FAN, LOW);
  digitalWrite(PIN_RELAY_HUM, LOW);

  // Initialize Button
  pinMode(PIN_BUTTON_START, INPUT_PULLUP);

  // Initialize Sensors & Display
  dht.begin();
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 allocation failed"));
    for (;;);
  }

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(15, 25);
  display.println(F("Cyano Fumer v1.0"));
  display.display();
  delay(1500);
}

void loop() {
  // 1. Read Sensors
  chamberTempXY = readXYT01Temperature();
  chamberTempDHT = dht.readTemperature();
  chamberHumidity = dht.readHumidity();

  // Handle DHT failure fallback
  if (isnan(chamberHumidity)) chamberHumidity = 0.0;

  // 2. Read Control Button (Debounced basic trigger)
  bool currentBtnState = digitalRead(PIN_BUTTON_START);
  if (lastButtonState == HIGH && currentBtnState == LOW) {
    if (currentState == STATE_IDLE || currentState == STATE_COMPLETE) {
      currentState = STATE_FUMING;
      stateStartTime = millis();
    } else if (currentState == STATE_FUMING) {
      // Manual abort -> jump to purge state for safety
      currentState = STATE_PURGING;
      stateStartTime = millis();
    }
  }
  lastButtonState = currentBtnState;

  // 3. Process State Machine & Relay Control Logic
  unsigned long elapsedSec = (millis() - stateStartTime) / 1000;

  switch (currentState) {
    case STATE_IDLE:
      digitalWrite(PIN_RELAY_HEAT, LOW);
      digitalWrite(PIN_RELAY_FAN, LOW);
      digitalWrite(PIN_RELAY_HUM, LOW);
      break;

    case STATE_FUMING:
      // Vaporizer Temperature Bang-Bang Control
      if (chamberTempXY < (TEMP_SETPOINT_C - TEMP_HYSTERESIS)) {
        digitalWrite(PIN_RELAY_HEAT, HIGH);
      } else if (chamberTempXY >= TEMP_SETPOINT_C) {
        digitalWrite(PIN_RELAY_HEAT, LOW);
      }

      // Humidifier Control
      if (chamberHumidity < (HUM_SETPOINT_RH - HUM_HYSTERESIS)) {
        digitalWrite(PIN_RELAY_HUM, HIGH);
      } else if (chamberHumidity >= HUM_SETPOINT_RH) {
        digitalWrite(PIN_RELAY_HUM, LOW);
      }

      // Fan running low/intermittent during fuming for distribution
      digitalWrite(PIN_RELAY_FAN, HIGH);

      // Check transition to Purge
      if (elapsedSec >= FUME_DURATION_SEC) {
        currentState = STATE_PURGING;
        stateStartTime = millis();
      }
      break;

    case STATE_PURGING:
      // Turn off heater and humidifier; run fan to clear fumes
      digitalWrite(PIN_RELAY_HEAT, LOW);
      digitalWrite(PIN_RELAY_HUM, LOW);
      digitalWrite(PIN_RELAY_FAN, HIGH);

      if (elapsedSec >= PURGE_DURATION_SEC) {
        currentState = STATE_COMPLETE;
      }
      break;

    case STATE_COMPLETE:
      digitalWrite(PIN_RELAY_HEAT, LOW);
      digitalWrite(PIN_RELAY_FAN, LOW);
      digitalWrite(PIN_RELAY_HUM, LOW);
      break;
  }

  // 4. Update OLED Display
  updateDisplay(elapsedSec);

  delay(200); // Main loop timing delay
}
