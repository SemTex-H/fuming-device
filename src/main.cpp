/*
  Superglue (CA) Fuming Chamber Controller
  ESP32 + 2-channel relay (heater, fan) + DHT22 (temp/humidity)

  State machine:
    IDLE  -> HEATING -> HOLDING -> PURGING -> IDLE

  Safety:
    - Hard over-temp cutoff kills heater regardless of state
    - Heater relay is only ever allowed ON if fan relay is also ON (interlock)
    - Fan runs during purge even after an over-temp fault, until temp drops
*/

#include <Arduino.h>
#include <DHT.h>

// ---------- Pin config ----------
#define DHTPIN        4
#define DHTTYPE       DHT22
#define RELAY_HEATER  26
#define RELAY_FAN     27
#define RELAY_ACTIVE_LOW true   // most relay modules are active-low; flip if needed

// ---------- Setpoints ----------
const float TARGET_TEMP_C   = 45.0;   // fuming temp
const float TARGET_HUMIDITY = 75.0;   // % RH, informational / can gate cycle start
const float TEMP_HYSTERESIS = 1.5;    // bang-bang deadband
const float OVERTEMP_CUTOFF = 62.0;   // hard safety limit, independent of state
const unsigned long HOLD_DURATION_MS  = 10UL * 60UL * 1000UL; // 10 min fuming hold
const unsigned long PURGE_DURATION_MS = 5UL  * 60UL * 1000UL; // 5 min purge

DHT dht(DHTPIN, DHTTYPE);

enum State { IDLE, HEATING, HOLDING, PURGING, FAULT };
State state = IDLE;
unsigned long stateEnteredAt = 0;

bool overTempFault = false;

void logStatus(float t, float h, bool sensorOk);

void setRelay(int pin, bool on) {
  bool level = RELAY_ACTIVE_LOW ? !on : on;
  digitalWrite(pin, level ? HIGH : LOW);
}

void enterState(State s) {
  state = s;
  stateEnteredAt = millis();
  Serial.print("-> State: ");
  Serial.println(s);
}

void setup() {
  Serial.begin(115200);
  pinMode(RELAY_HEATER, OUTPUT);
  pinMode(RELAY_FAN, OUTPUT);
  setRelay(RELAY_HEATER, false);
  setRelay(RELAY_FAN, false);
  dht.begin();
  Serial.println("Fuming chamber controller ready. Send 'start' or 'stop' over Serial.");
}

void loop() {
  float t = dht.readTemperature();
  float h = dht.readHumidity();
  bool sensorOk = !isnan(t) && !isnan(h);

  // ---- Serial commands ----
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    if (cmd == "start" && state == IDLE) enterState(HEATING);
    else if (cmd == "stop") { overTempFault = false; enterState(PURGING); }
  }

  // ---- Hard safety cutoff, checked every loop regardless of state ----
  if (sensorOk && t >= OVERTEMP_CUTOFF) {
    overTempFault = true;
  }

  if (overTempFault) {
    setRelay(RELAY_HEATER, false);  // heater always off on fault
    setRelay(RELAY_FAN, true);      // fan stays on to cool/vent
    if (state != FAULT) enterState(FAULT);
    if (sensorOk && t < (OVERTEMP_CUTOFF - 5.0)) {
      // cooled down enough, drop into purge instead of staying latched forever
      overTempFault = false;
      enterState(PURGING);
    }
    logStatus(t, h, sensorOk);
    delay(1000);
    return;
  }

  // ---- Heater/fan interlock: heater only allowed if fan is on ----
  bool fanOn = false;
  bool heaterRequest = false;

  switch (state) {
    case IDLE:
      fanOn = false;
      heaterRequest = false;
      break;

    case HEATING:
      fanOn = true;
      if (sensorOk) {
        heaterRequest = t < TARGET_TEMP_C;
        if (t >= TARGET_TEMP_C) enterState(HOLDING);
      }
      break;

    case HOLDING:
      fanOn = true;
      if (sensorOk) {
        // simple bang-bang around target
        if (t < TARGET_TEMP_C - TEMP_HYSTERESIS) heaterRequest = true;
        else if (t > TARGET_TEMP_C + TEMP_HYSTERESIS) heaterRequest = false;
        else heaterRequest = (t < TARGET_TEMP_C);
      }
      if (millis() - stateEnteredAt >= HOLD_DURATION_MS) enterState(PURGING);
      break;

    case PURGING:
      fanOn = true;
      heaterRequest = false;
      if (millis() - stateEnteredAt >= PURGE_DURATION_MS) enterState(IDLE);
      break;

    case FAULT:
      // handled above
      break;
  }

  setRelay(RELAY_FAN, fanOn);
  setRelay(RELAY_HEATER, fanOn && heaterRequest); // interlock enforced here

  logStatus(t, h, sensorOk);
  delay(1000);
}

void logStatus(float t, float h, bool sensorOk) {
  Serial.print("State="); Serial.print(state);
  if (sensorOk) {
    Serial.print(" T="); Serial.print(t);
    Serial.print("C H="); Serial.print(h);
    Serial.print("%");
  } else {
    Serial.print(" SENSOR READ FAILED");
  }
  Serial.println();
}