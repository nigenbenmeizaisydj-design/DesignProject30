/*************************************************
 * WIFI CONFIGURATION
 *************************************************/
#define USE_WIFI_NINA false
#define USE_WIFI101   true
#include <SPI.h>
#include <WiFi101.h>
#include <WiFiWebServer.h>
const char ssid[] = "EEERover";
const char pass[] = "exhibition";
const int groupNumber = 30;
WiFiWebServer server(80);
 
/*************************************************
 * PIN DEFINITIONS
 *************************************************/
const int hallPin1 = A0;
const int hallPin2 = A1;
const int ultraPin = A2;
 
const int COMPARATOR_PIN = 2;
 
/*************************************************
 * MAGNETIC SENSOR
 *************************************************/
const int AVERAGE_SIZE = 16;
const int groups       = 40;
const int voteTimes    = 5;
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
const int MEASURE_TIME_MS = 1000;
 
volatile int pulseCount = 0;
 
float      max_rate  = 0.0;
const int  THRESHOLD = 350;
 
String irType = "UNKNOWN";
 
/*************************************************
 * ROCK AGE (UART)
 *************************************************/
String buffer          = "";
String lastGoodReading = "---";
 
/*************************************************
 * ULTRASOUND
 *************************************************/
bool    ultraDetected    = false;
unsigned long lastDetectedTime = 0;
 
/*************************************************
 * WEB PAGE
 *************************************************/
const char webpage[] PROGMEM = R"rawliteral(
<html>
<head>
<style>
  body { font-family: sans-serif; padding: 20px; }
  .btn { background-color: inherit; padding: 14px 28px; font-size: 16px; }
  .btn:hover { background: #eee; }
  .label { font-weight: bold; }
</style>
</head>
<body>
  <h2>Rock Sensor Dashboard</h2>
  <p><span class="label">Rock Type:</span> <span id="rocktype">--</span></p>
  <p><span class="label">Rock Age:</span>  <span id="rockage">--</span></p>
  <p><span class="label">Magnetic:</span>  <span id="magnetic">--</span></p>
  <p><span class="label">IR Type:</span>   <span id="irtype">--</span></p>
  <p><span class="label">Ultrasound:</span><span id="ultra">--</span></p>
 
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
 
/*************************************************
 * MAGNETIC FUNCTIONS
 *************************************************/
float readDiff()
{
    int h1 = analogRead(hallPin1);
    int h2 = analogRead(hallPin2);
    int diff = h1 - h2;
 
 
    return (float)diff;
}
 
float readMovingAverage()
{
    float value = readDiff();
 
    if (averageIndex < 0 || averageIndex >= AVERAGE_SIZE)
        averageIndex = 0;
 
    averageBuffer[averageIndex] = value;
    averageIndex++;
 
    if (averageIndex >= AVERAGE_SIZE)
    {
        averageIndex = 0;
        averageFull  = true;
    }
 
    int count = averageFull ? AVERAGE_SIZE : averageIndex;
    if (count <= 0) return value;
 
    float sum = 0;
    for (int i = 0; i < count; i++)
    {
        if (averageBuffer[i] < -1023 || averageBuffer[i] > 1023)
        {
            Serial.print("BAD buffer[");
            Serial.print(i);
            Serial.print("] = ");
            Serial.println(averageBuffer[i]);
            averageBuffer[i] = value;
        }
 
        sum += averageBuffer[i];
    }
 
    return sum / count;
}
void getExtreme(float &maxAvg, float &minAvg)
{
    maxAvg = -1000000;
    minAvg =  1000000;
 
    for (int i = 0; i < groups; i++)
    {
        float avg = readMovingAverage();
        if (avg > maxAvg) maxAvg = avg;
        if (avg < minAvg) minAvg = avg;
        delay(10);
    }
}
 
void updateMagnetic()
{
    int upCount   = 0;
    int downCount = 0;
 
    for (int i = 0; i < voteTimes; i++)
    {
        float curMax, curMin;
        getExtreme(curMax, curMin);
 
        if      (curMax > baseMaxAvg && curMin > baseMinAvg) upCount++;
        else if (curMax < baseMaxAvg && curMin < baseMinAvg) downCount++;
    }
 
    if      (upCount   >= voteThreshold) magneticResult = "TRUE UP";
    else if (downCount >= voteThreshold) magneticResult = "TRUE DOWN";
    else                                 magneticResult = "NOT DETECTED";
}
 
/*************************************************
 * IR FUNCTIONS
 *************************************************/
void countPulse()
{
    pulseCount++;
}
 
void updateIR()
{
    noInterrupts();
    pulseCount = 0;
    interrupts();
 
    delay(MEASURE_TIME_MS);
 
    noInterrupts();
    int count = pulseCount;
    interrupts();
 
    float rate = (float)count / (MEASURE_TIME_MS / 1000.0);
    if (rate > max_rate) max_rate = rate;
 
    if      (max_rate < 100)      irType = "UNKNOWN";
    else if (max_rate > THRESHOLD) irType = "TYPE 2";
    else                           irType = "TYPE 1";
}
 
/*************************************************
 * ROCK AGE
 *************************************************/
bool isValidReading(String s)
{
    if (s.length() != 4) return false;
    if (s[0] != '#')     return false;
    for (int i = 1; i < 4; i++)
        if (!isDigit(s[i])) return false;
    return true;
}
 
void updateRockAge()
{
    while (Serial1.available())
    {
        char c = Serial1.read();
        if (c == '#')
        {
            buffer = "#";
        }
        else if (buffer.length() > 0)
        {
            buffer += c;
            if (buffer.length() >= 4)
            {
                if (isValidReading(buffer))
                    lastGoodReading = buffer;
                  buffer = "";
            }
        }
    }
}
 
/*************************************************
 * ULTRASOUND
 *************************************************/
void updateUltrasound()
{
    int value = analogRead(ultraPin);
 
    if (value >= 115)
    {
        ultraDetected    = true;
        lastDetectedTime = millis();
    }
 
    if (ultraDetected && millis() - lastDetectedTime > 2000)
        ultraDetected = false;
       delay(20);
}
 
/*************************************************
 * ROCK TYPE CLASSIFICATION
 *************************************************/
String classifyRockType()
{
    bool ir1  = (irType == "TYPE 1");
    bool ir2  = (irType == "TYPE 2");
    bool ultra = ultraDetected;
    bool up   = (magneticResult == "TRUE UP");
    bool down = (magneticResult == "TRUE DOWN");
 
    if (ir2 && ultra && down)  return "Basaltoid";
    if (ir1 && !ultra && down) return "Gravion";
    if (ir1 && ultra && up)    return "Regolix";
    if (ir2 && !ultra && up)   return "Lunarite";
    return "Unknown";
}
 
/*************************************************
 * WEB SERVER HANDLERS
 *************************************************/
void handleRoot()
{
    server.send(200, F("text/html"), webpage);
}
 
void handleRockType()
{
    server.send(200, F("text/plain"), classifyRockType());
}
 
void handleRockAge()
{
    server.send(200, F("text/plain"), lastGoodReading);
}
 
void handleMagnetic()
{
    server.send(200, F("text/plain"), magneticResult);
}
 
void handleIRType()
{
    server.send(200, F("text/plain"), irType);
}
 
void handleUltra()
{
    server.send(200, F("text/plain"), ultraDetected ? "Detected" : "Not detected");
}
 
void handleNotFound()
{
    String message = F("File Not Found\n\nURI: ");
    message += server.uri();
    message += F("\nMethod: ");
    message += (server.method() == HTTP_GET) ? F("GET") : F("POST");
    server.send(404, F("text/plain"), message);
}
 
/*************************************************
 * SETUP
 *************************************************/
void setup()
{
    Serial.begin(9600);
    Serial1.begin(600);
 
    pinMode(A0, INPUT);
    pinMode(A1, INPUT);
    pinMode(A2, INPUT);
    pinMode(COMPARATOR_PIN, INPUT);
    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, 0);
    // Initialise TR
    attachInterrupt(digitalPinToInterrupt(COMPARATOR_PIN), countPulse, RISING);
 
    // Initialise magnetic
    float initValue = readDiff();
    for (int i = 0; i < AVERAGE_SIZE; i++)
        averageBuffer[i] = initValue;
    averageFull = true;
 
    delay(500);
    getExtreme(baseMaxAvg, baseMinAvg);
 
    Serial.println(F("Sensors ready"));
 
    // ----- WiFi -----
    while (!Serial && millis() < 10000);
    Serial.println(F("\nStarting Web Server"));
 
    if (WiFi.status() == WL_NO_SHIELD)
    {
        Serial.println(F("WiFi shield not present"));
        while (true);
    }
 
    Serial.print(F("Connecting to: "));
    Serial.println(ssid);
    WiFi.begin(ssid, pass);
 
    while (WiFi.status() != WL_CONNECTED)
    {
        delay(500);
        Serial.print('.');
    }
    Serial.println();
 
    // Register routes
    server.on(F("/"),         handleRoot);
    server.on(F("/rocktype"), handleRockType);
    server.on(F("/rockage"),  handleRockAge);
    server.on(F("/magnetic"), handleMagnetic);
    server.on(F("/irtype"),   handleIRType);
    server.on(F("/ultra"),    handleUltra);
    server.onNotFound(handleNotFound);
 
    server.begin();
    Serial.print(F("HTTP server started @ "));
    Serial.println(static_cast<IPAddress>(WiFi.localIP()));
}
 
/*************************************************
 * LOOP
 *************************************************/
void loop()
{
    // Update all sensors
    updateRockAge();
    updateUltrasound();
    updateMagnetic();
    updateIR();
 
    // Serve any pending web requests
    server.handleClient();
 
    // Debug output
    Serial.println(F("========== SENSOR REPORT =========="));
    Serial.print(F("Magnetic   : ")); Serial.println(magneticResult);
    Serial.print(F("IR Type    : ")); Serial.println(irType);
    Serial.print(F("Ultrasound : ")); Serial.println(ultraDetected ? "detected" : "not detected");
    Serial.print(F("Rock Type  : ")); Serial.println(classifyRockType());
    Serial.print(F("Rock Age   : ")); Serial.println(lastGoodReading);
    Serial.println(F("==================================="));
 
    delay(100);
}

