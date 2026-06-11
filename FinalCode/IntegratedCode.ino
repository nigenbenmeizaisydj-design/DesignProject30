/*************************************************
 * COMBINED: Sensor Dashboard (WiFi101) +
 *           Motor Receiver  (nRF24L01)
* Run on the same Arduino MKR/Zero/M0 board
*
* SPI device assignment:
* WiFi101 — onboard CS (comes with Shield)
* nRF24L01 — CE=9, CSN=10
*
* Note: WiFi101 Shield uses hardware SPI,
* nRF24L01 is connected to the same SPI bus,
* use CSN=10 for chip select differentiation
。
 *************************************************/

/*************************************************
 * WIFI CONFIGURATION
 *************************************************/
#define USE_WIFI_NINA false
#define USE_WIFI101   true
#include <SPI.h>
#include <WiFi101.h>
#include <WiFiWebServer.h>

const char ssid[]      = "EEERover";
const char pass[]      = "exhibition";
const int  groupNumber = 30;
WiFiWebServer server(80);

/*************************************************
 * nRF24L01 RADIO
 *************************************************/
#include "nRF24L01.h"
#include "RF24.h"
#include "printf.h"

// CE=9, CSN=10 — Avoid the chip select of the WiFi Shield
const uint32_t RADIO_SPI_HZ = 1000000;
RF24 radio(9, 10, RADIO_SPI_HZ);
const uint64_t pipe = 0xE8E8F0F0E1LL;

/*************************************************
 * MOTOR PINS (TC78H620)
 *************************************************/
const uint8_t EN_R  = 3;
const uint8_t DIR_R = 4;
const uint8_t EN_L  = 6;
const uint8_t DIR_L = 8;
const uint8_t DIR_FORWARD = HIGH;

/*************************************************
 * JOYSTICK / MOTOR TUNING
 *************************************************/
const int JOY_CENTER   = 512;
const int JOY_DEADZONE = 40;
const int MAX_PWM      = 255;

const unsigned long FAILSAFE_MS = 300;

/*************************************************
 * RADIO RUNTIME STATE
 *************************************************/
unsigned long lastPacketMs  = 0;
unsigned long lastStatsMs   = 0;
unsigned long pktGood       = 0;
unsigned long pktBad        = 0;
unsigned long statsPrevGood = 0;
bool          linkUp        = false;
int           lastLeft      = 0;
int           lastRight     = 0;

#define DBG_LEVEL         2
#define STATS_INTERVAL_MS 2000UL

struct JoystickData {
    uint16_t x;
    uint16_t y;
    uint8_t  pressed;
};
JoystickData joystickPayload;

/*************************************************
 * SENSOR PIN DEFINITIONS
 *************************************************/
const int hallPin1       = A0;
const int hallPin2       = A1;
const int ultraPin       = A2;
const int COMPARATOR_PIN = 2;

/*************************************************
 * MAGNETIC SENSOR
 *************************************************/
const int AVERAGE_SIZE  = 16;
const int groups        = 40;
const int voteTimes     = 5;
const int voteThreshold = 5;

float baseMaxAvg = 0;
float baseMinAvg = 0;

float averageBuffer[AVERAGE_SIZE];
int   averageIndex = 0;
bool  averageFull  = false;

String magneticResult = "NOT DETECTED";

/*************************************************
 * IR ROCK TYPE
 *************************************************/
const int  MEASURE_TIME_MS = 1000;
volatile int pulseCount    = 0;
float        max_rate      = 0.0;
const int    THRESHOLD     = 350;
String       irType        = "UNKNOWN";

/*************************************************
 * ROCK AGE (UART via Serial1)
 *************************************************/
String uartBuffer      = "";
String lastGoodReading = "---";

/*************************************************
 * ULTRASOUND
 *************************************************/
bool          ultraDetected    = false;
unsigned long lastDetectedTime = 0;

/*************************************************
 * WEB PAGE
 *************************************************/
const char webpage[] PROGMEM = R"rawliteral(
<html>
<head>
<style>
  body { font-family: sans-serif; padding: 20px; }
  .label { font-weight: bold; }
  table { border-collapse: collapse; margin-top: 16px; }
  td, th { border: 1px solid #ccc; padding: 10px 20px; }
  th { background: #f0f0f0; }
</style>
</head>
<body>
  <h2>Rock Sensor Dashboard</h2>
  <table>
    <tr><th>Sensor</th><th>Value</th></tr>
    <tr><td class="label">Rock Type</td>  <td id="rocktype">--</td></tr>
    <tr><td class="label">Rock Age</td>   <td id="rockage">--</td></tr>
    <tr><td class="label">Magnetic</td>   <td id="magnetic">--</td></tr>
    <tr><td class="label">IR Type</td>    <td id="irtype">--</td></tr>
    <tr><td class="label">Ultrasound</td> <td id="ultra">--</td></tr>
  </table>
  <script>
    function fetchAll() {
      fetch('/rocktype').then(r => r.text()).then(t => document.getElementById('rocktype').innerText = t);
      fetch('/rockage').then(r  => r.text()).then(t => document.getElementById('rockage').innerText  = t);
      fetch('/magnetic').then(r => r.text()).then(t => document.getElementById('magnetic').innerText = t);
      fetch('/irtype').then(r   => r.text()).then(t => document.getElementById('irtype').innerText   = t);
      fetch('/ultra').then(r    => r.text()).then(t => document.getElementById('ultra').innerText    = t);
    }
    fetchAll();
    setInterval(fetchAll, 2000);
  </script>
</body>
</html>
)rawliteral";

/*================================================
 * MOTOR FUNCTIONS
 *===============================================*/
void stopMotors() {
    analogWrite(EN_R, 0);
    analogWrite(EN_L, 0);
}

void setMotor(uint8_t enPin, uint8_t dirPin, int speed) {
    speed = constrain(speed, -MAX_PWM, MAX_PWM);
    if (speed >= 0) {
        digitalWrite(dirPin, DIR_FORWARD);
        analogWrite(enPin, speed);
    } else {
        digitalWrite(dirPin, !DIR_FORWARD);
        analogWrite(enPin, -speed);
    }
}

void driveFromJoystick(const JoystickData &p) {
    if (p.pressed) {
        stopMotors();
        lastLeft = lastRight = 0;
#if DBG_LEVEL >= 2
        Serial.println(F("[drive] STOP (button)"));
#endif
        return;
    }

    int fwd  = (int)p.y - JOY_CENTER;
    int turn = (int)p.x - JOY_CENTER;
    if (abs(fwd)  < JOY_DEADZONE) fwd  = 0;
    if (abs(turn) < JOY_DEADZONE) turn = 0;

    int left  = constrain((fwd + turn) / 2, -MAX_PWM, MAX_PWM);
    int right = constrain((fwd - turn) / 2, -MAX_PWM, MAX_PWM);

    setMotor(EN_R, DIR_R, right);
    setMotor(EN_L, DIR_L, left);
    lastLeft = left; lastRight = right;

#if DBG_LEVEL >= 2
    Serial.print(F("[drive] fwd=")); Serial.print(fwd);
    Serial.print(F(" turn="));       Serial.print(turn);
    Serial.print(F(" -> L="));       Serial.print(left);
    Serial.print(F(" R="));          Serial.println(right);
#endif
}

bool validPayload(const JoystickData &p) {
    return (p.x <= 1023) && (p.y <= 1023) && (p.pressed <= 1);
}

void printPeriodicStats() {
#if DBG_LEVEL >= 1
    unsigned long now = millis();
    if (now - lastStatsMs >= STATS_INTERVAL_MS) {
        unsigned long rate = ((pktGood - statsPrevGood) * 1000UL) / STATS_INTERVAL_MS;
        Serial.print(F("[stats] good="));  Serial.print(pktGood);
        Serial.print(F(" bad="));          Serial.print(pktBad);
        Serial.print(F(" rate="));         Serial.print(rate);
        Serial.print(F("/s link="));       Serial.print(linkUp ? F("UP") : F("DOWN"));
        Serial.print(F(" L="));            Serial.print(lastLeft);
        Serial.print(F(" R="));            Serial.println(lastRight);
        statsPrevGood = pktGood;
        lastStatsMs   = now;
    }
#endif
}

/*================================================
 * RADIO TASK — Call in loop()
 *===============================================*/
void updateRadio() {
    if (radio.available()) {
        JoystickData in;
        while (radio.available()) radio.read(&in, sizeof(in));

        if (validPayload(in)) {
            joystickPayload = in;
            pktGood++;
            lastPacketMs = millis();
            if (!linkUp) {
                linkUp = true;
                Serial.println(F("[link] UP — receiving valid packets"));
            }
            driveFromJoystick(joystickPayload);
        } else {
            pktBad++;
            Serial.print(F("[warn] bad packet: x=")); Serial.print(in.x);
            Serial.print(F(" y="));                   Serial.print(in.y);
            Serial.print(F(" pressed="));             Serial.println(in.pressed);
        }
    }

    // Failsafe: Overtime parking
    if (millis() - lastPacketMs > FAILSAFE_MS) {
        if (linkUp) {
            linkUp = false;
            Serial.print(F("[link] DOWN — no valid packet for "));
            Serial.print(FAILSAFE_MS);
            Serial.println(F(" ms; motors stopped"));
        }
        stopMotors();
    }

    printPeriodicStats();
}

/*================================================
 * MAGNETIC FUNCTIONS
 *===============================================*/
float readDiff() {
    return (float)(analogRead(hallPin1) - analogRead(hallPin2));
}

float readMovingAverage() {
    float value = readDiff();
    if (averageIndex < 0 || averageIndex >= AVERAGE_SIZE) averageIndex = 0;
    averageBuffer[averageIndex++] = value;
    if (averageIndex >= AVERAGE_SIZE) { averageIndex = 0; averageFull = true; }

    int count = averageFull ? AVERAGE_SIZE : averageIndex;
    if (count <= 0) return value;

    float sum = 0;
    for (int i = 0; i < count; i++) {
        if (averageBuffer[i] < -1023 || averageBuffer[i] > 1023)
            averageBuffer[i] = value;
        sum += averageBuffer[i];
    }
    return sum / count;
}

void getExtreme(float &maxAvg, float &minAvg) {
    maxAvg = -1000000; minAvg = 1000000;
    for (int i = 0; i < groups; i++) {
        float avg = readMovingAverage();
        if (avg > maxAvg) maxAvg = avg;
        if (avg < minAvg) minAvg = avg;
        delay(2);
    }
}

void updateMagnetic() {
    int upCount = 0, downCount = 0;
    for (int i = 0; i < voteTimes; i++) {
        float curMax, curMin;
        getExtreme(curMax, curMin);
        if      (curMax > baseMaxAvg && curMin > baseMinAvg) upCount++;
        else if (curMax < baseMaxAvg && curMin < baseMinAvg) downCount++;
    }
    if      (upCount   >= voteThreshold) magneticResult = "TRUE UP";
    else if (downCount >= voteThreshold) magneticResult = "TRUE DOWN";
    else                                 magneticResult = "NOT DETECTED";
}

/*================================================
 * IR FUNCTIONS
 *===============================================*/
void countPulse() { pulseCount++; }

void updateIR() {
    noInterrupts(); pulseCount = 0; interrupts();
    delay(MEASURE_TIME_MS);
    noInterrupts(); int count = pulseCount; interrupts();

    float rate = (float)count / (MEASURE_TIME_MS / 1000.0);
    if (rate > max_rate) max_rate = rate;

    if      (max_rate < 100)       irType = "UNKNOWN";
    else if (max_rate > THRESHOLD) irType = "TYPE 2";
    else                           irType = "TYPE 1";
}

/*================================================
 * ROCK AGE (UART)
 *===============================================*/
bool isValidReading(String s) {
    if (s.length() != 4 || s[0] != '#') return false;
    for (int i = 1; i < 4; i++)
        if (!isDigit(s[i])) return false;
    return true;
}

void updateRockAge() {
    while (Serial1.available()) {
        char c = Serial1.read();
        if (c == '#') {
            uartBuffer = "#";
        } else if (uartBuffer.length() > 0) {
            uartBuffer += c;
            if (uartBuffer.length() >= 4) {
                if (isValidReading(uartBuffer)) lastGoodReading = uartBuffer;
                uartBuffer = "";
            }
        }
    }
}

/*================================================
 * ULTRASOUND
 *===============================================*/
void updateUltrasound() {
    int value = analogRead(ultraPin);
    if (value >= 115) { ultraDetected = true; lastDetectedTime = millis(); }
    if (ultraDetected && millis() - lastDetectedTime > 2000) ultraDetected = false;
    delay(20);
}

/*================================================
 * ROCK TYPE CLASSIFICATION
 *===============================================*/
String classifyRockType() {
    bool ir1   = (irType == "TYPE 1");
    bool ir2   = (irType == "TYPE 2");
    bool ultra = ultraDetected;
    bool up    = (magneticResult == "TRUE UP");
    bool down  = (magneticResult == "TRUE DOWN");

    if (ir2 && ultra && down)  return "Basaltoid";
    if (ir1 && !ultra && down) return "Gravion";
    if (ir1 && ultra && up)    return "Regolix";
    if (ir2 && !ultra && up)   return "Lunarite";
    return "Unknown";
}

/*================================================
 * WEB SERVER HANDLERS
 *===============================================*/
void handleRoot()     { server.send(200, F("text/html"),  webpage); }
void handleRockType() { server.send(200, F("text/plain"), classifyRockType()); }
void handleRockAge()  { server.send(200, F("text/plain"), lastGoodReading); }
void handleMagnetic() { server.send(200, F("text/plain"), magneticResult); }
void handleIRType()   { server.send(200, F("text/plain"), irType); }
void handleUltra()    { server.send(200, F("text/plain"), ultraDetected ? "Detected" : "Not detected"); }
void handleNotFound() {
    String msg = F("File Not Found\n\nURI: ");
    msg += server.uri();
    server.send(404, F("text/plain"), msg);
}

/*================================================
 * SETUP
 *===============================================*/
void setup() {
    Serial.begin(115200);
    Serial1.begin(600);
    while (!Serial && millis() < 2000);
    printf_begin();
    Serial.println(F("\n=== Combined Sensor + Motor Receiver — boot ==="));

    // ---- Sensor pin----
    pinMode(A0, INPUT); pinMode(A1, INPUT); pinMode(A2, INPUT);
    pinMode(COMPARATOR_PIN, INPUT);
    attachInterrupt(digitalPinToInterrupt(COMPARATOR_PIN), countPulse, RISING);

    // ---- Motor pins----
    pinMode(EN_R, OUTPUT); pinMode(DIR_R, OUTPUT);
    pinMode(EN_L, OUTPUT); pinMode(DIR_L, OUTPUT);
    stopMotors();
    Serial.println(F("[motor] outputs initialised, motors stopped"));

    // ---- LED ----
    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, LOW);

    // ---- Magnetic Sensor Baseline Calibration ----
    float initValue = readDiff();
    for (int i = 0; i < AVERAGE_SIZE; i++) averageBuffer[i] = initValue;
    averageFull = true;
    delay(500);
    getExtreme(baseMaxAvg, baseMinAvg);
    Serial.println(F("[sensor] magnetic baseline calibrated"));

    // ---- nRF24L01 initialization ----
    Serial.println(F("[radio] begin()..."));
    if (!radio.begin()) {
        Serial.println(F("[radio] FATAL: radio.begin() failed — check CE=9/CSN=10/SPI wiring"));
        //Does not block the entire board, only stops and alarms, WiFi side can still operate
        stopMotors();
    } else if (!radio.isChipConnected()) {
        Serial.println(F("[radio] FATAL: chip not connected — SPI read-back failed"));
        stopMotors();
    } else {
        radio.setPALevel(RF24_PA_LOW);
        radio.setDataRate(RF24_1MBPS);
        radio.setPayloadSize(sizeof(JoystickData));
        radio.openReadingPipe(1, pipe);
        radio.startListening();
#if DBG_LEVEL >= 1
        radio.printDetails();
#endif
        Serial.println(F("[radio] ready"));
    }
    lastStatsMs  = millis();
    lastPacketMs = millis();   // Prevent failsafe from triggering at power-up

    // ---- WiFi initialization ----
    Serial.println(F("[wifi] connecting..."));
    if (WiFi.status() == WL_NO_SHIELD) {
        Serial.println(F("[wifi] shield not present — web server disabled"));
    } else {
        WiFi.begin(ssid, pass);
        unsigned long t0 = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) {
            delay(500); Serial.print('.');
        }
        Serial.println();

        if (WiFi.status() == WL_CONNECTED) {
            server.on(F("/"),         handleRoot);
            server.on(F("/rocktype"), handleRockType);
            server.on(F("/rockage"),  handleRockAge);
            server.on(F("/magnetic"), handleMagnetic);
            server.on(F("/irtype"),   handleIRType);
            server.on(F("/ultra"),    handleUltra);
            server.onNotFound(handleNotFound);
            server.begin();
            Serial.print(F("[wifi] HTTP server @ "));
            Serial.println(static_cast<IPAddress>(WiFi.localIP()));
        } else {
            Serial.println(F("[wifi] connection failed — web server disabled"));
        }
    }

    Serial.println(F("=== boot complete ===\n"));
}

/*================================================
 * LOOP
 *===============================================*/
void loop() {
    // --- Remote control motornRF24L01）---
    updateRadio();

    // --- sensor upload ---
    updateRockAge();
    updateUltrasound();
    updateMagnetic();
    updateIR();

    // --- webserver（WiFi101）---
    server.handleClient();

    // --- Serial debug output---
    Serial.println(F("========== SENSOR REPORT =========="));
    Serial.print(F("Magnetic   : ")); Serial.println(magneticResult);
    Serial.print(F("IR Type    : ")); Serial.println(irType);
    Serial.print(F("Ultrasound : ")); Serial.println(ultraDetected ? "detected" : "not detected");
    Serial.print(F("Rock Type  : ")); Serial.println(classifyRockType());
    Serial.print(F("Rock Age   : ")); Serial.println(lastGoodReading);
    Serial.println(F("==================================="));

    delay(100);
}

