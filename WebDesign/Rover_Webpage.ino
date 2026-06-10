#define USE_WIFI_NINA         false
#define USE_WIFI101           true
#include <SPI.h>
#include <WiFi101.h>
#include <WiFiWebServer.h>

const char ssid[] = "Phoenix";
const char pass[] = "vhjj2762";
int Radiation = 300;
bool Magnetic = true; //true = Up, false = Down;
bool UltraSound = true;
double Age = 1.23;


//Webpage to return when root is requested
const char webpage[] PROGMEM = R"rawliteral(
<html>
<head>
<style>
.btn {background-color: inherit;padding: 14px 28px;font-size: 16px;}
.btn:hover {background: #eee;}
</style>
</head>

<body>

<button class="btn" onclick="Basaltoid()">Basaltoid</button>
<button class="btn" onclick="Gravion()">Gravion</button>
<button class="btn" onclick="Regolix()">Regolix</button>
<button class="btn" onclick="Lunarite()">Lunarite</button>
<button class="btn" onclick="Check_Rock()">Check Rock</button>
<button class="btn" onclick="Check_Age()">Check Age</button>

<br>ROCK TYPE: <span id="state">None detected</span>
<br>AGE: <span id="value">None detected</span>

<script>

function sendRequest(url, elementId) {
  var xhr = new XMLHttpRequest();
  xhr.onreadystatechange = function() {
    if (this.readyState == 4 && this.status == 200) {
      document.getElementById(elementId).innerHTML = this.responseText;
    }
  };
  xhr.open("GET", url);
  xhr.send();
}

function Basaltoid() { sendRequest("/Basaltoid", "state"); }
function Gravion() { sendRequest("/Gravion", "state"); }
function Regolix() { sendRequest("/Regolix", "state"); }
function Lunarite() { sendRequest("/Lunarite", "state"); }
function Check_Rock() { sendRequest("/Check_Rock", "state"); }
function Check_Age() { sendRequest("/Check_Age", "value"); }

</script>

</body>
</html>
)rawliteral";

WiFiWebServer server(80);

//Return the web page
void handleRoot()
{
  Serial.println("Root page requested");
  server.send(200, F("text/html"), webpage);
}


void Check_Rock()
{
  if (Radiation > 10 && Radiation < 400){
    if (Magnetic == true && UltraSound == true) {
      Regolix();
}
    else{
      Gravion();
  }
  }
  else if (Radiation > 400){
    if (Magnetic == false && UltraSound == true) {
      Basaltoid();
      }
    else{
      Lunarite();
    }
  }
  else{
    server.send(200, F("text/plain"), F("None detected"));
  }
}

void Check_Age()
{
  server.send(200, F("text/plain"), String(Age));
}

//Switch LED on and acknowledge
void Basaltoid()
{
  server.send(200, F("text/plain"), F("Basaltoid"));
}

//Switch LED on and acknowledge
void Gravion()
{
  server.send(200, F("text/plain"), F("Gravion"));
}

void Regolix()
{
  server.send(200, F("text/plain"), F("Regolix"));
}

void Lunarite()
{
  server.send(200, F("text/plain"), F("Lunarite"));
}

//Generate a 404 response with details of the failed request
void handleNotFound()
{
  String message = F("File Not Found\n\n"); 
  message += F("URI: ");
  message += server.uri();
  message += F("\nMethod: ");
  message += (server.method() == HTTP_GET) ? F("GET") : F("POST");
  message += F("\nArguments: ");
  message += server.args();
  message += F("\n");
  for (uint8_t i = 0; i < server.args(); i++)
  {
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }
  server.send(404, F("text/plain"), message);
}

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
const int groups = 40;
 
const int voteTimes = 5;
const int voteThreshold = 5;
 
float baseMaxAvg = 0;
float baseMinAvg = 0;
 
float averageBuffer[AVERAGE_SIZE];
int averageIndex = 0;
bool averageFull = false;
 
String magneticResult = "NOT DETECTED";
 
/*************************************************
* IR ROCK TYPE
*************************************************/
const int MEASURE_TIME_MS = 1000;
 
volatile int pulseCount = 0;
 
float max_rate = 0.0;
const int THRESHOLD = 350;
 
String irType = "UNKNOWN";
 
/*************************************************
* ROCK AGE (UART)
*************************************************/
String buffer = "";
String lastGoodReading = "---";
 
/*************************************************
* ULTRASOUND
*************************************************/
bool ultraDetected = false;
unsigned long lastDetectedTime = 0;
 
/*************************************************
* MAGNETIC FUNCTIONS
*************************************************/
float readDiff()
{
   return (float)(analogRead(hallPin1) - analogRead(hallPin2));
}
 
float readMovingAverage()
{
   float value = readDiff();
 
   averageBuffer[averageIndex] = value;
 
   averageIndex++;
 
   if (averageIndex >= AVERAGE_SIZE)
   {
       averageIndex = 0;
       averageFull = true;
   }
 
   int count = averageFull ? AVERAGE_SIZE : averageIndex;
 
   float sum = 0;
   for (int i = 0; i < count; i++)
       sum += averageBuffer[i];
 
   return sum / count;
}
 
void getExtreme(float &maxAvg, float &minAvg)
{
   maxAvg = -1000000;
   minAvg = 1000000;
 
   for (int i = 0; i < groups; i++)
   {
       float avg = readMovingAverage();
 
       if (avg > maxAvg)
           maxAvg = avg;
 
       if (avg < minAvg)
           minAvg = avg;
 
       delay(2);
   }
}
 
void updateMagnetic()
{
   int upCount = 0;
   int downCount = 0;
 
   for (int i = 0; i < voteTimes; i++)
   {
       float curMax, curMin;
       getExtreme(curMax, curMin);
 
       if (curMax > baseMaxAvg && curMin > baseMinAvg)
           upCount++;
 
       else if (curMax < baseMaxAvg && curMin < baseMinAvg)
           downCount++;
   }
 
   if (upCount >= voteThreshold)
       magneticResult = "TRUE UP";
   else if (downCount >= voteThreshold)
       magneticResult = "TRUE DOWN";
   else
       magneticResult = "NOT DETECTED";
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
 
   if (rate > max_rate)
       max_rate = rate;
 
   if (max_rate < 100)
       irType = "UNKNOWN";
   else if (max_rate > THRESHOLD)
       irType = "TYPE 2";
   else
       irType = "TYPE 1";
}
 
/*************************************************
* ROCK AGE
*************************************************/
bool isValidReading(String s)
{
   if (s.length() != 4) return false;
   if (s[0] != '#') return false;
 
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
 
   if (value >= 40)
   {
       ultraDetected = true;
       lastDetectedTime = millis();
   }
 
   if (ultraDetected && millis() - lastDetectedTime > 2000)
   {
       ultraDetected = false;
   }
}
 
/*************************************************
* ROCK TYPE CLASSIFICATION
*************************************************/
String classifyRockType()
{
   bool ir1 = (irType == "TYPE 1");
   bool ir2 = (irType == "TYPE 2");
 
   bool ultra = ultraDetected;
 
   bool up = (magneticResult == "TRUE UP");
   bool down = (magneticResult == "TRUE DOWN");
 
   if (ir2 && ultra && down)
      Basaltoid();
       return "Basaltoid";
 
   if (ir1 && !ultra && down)
    Gravion();
       return "Gravion";
 
   if (ir1 && ultra && up)
      Regolix();
       return "Regolix";
 
   if (ir2 && !ultra && up)
      Lunarite();
       return "Lunarite";
 
   return "Unknown";
}

void setup()
{
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, 0);

  Serial.begin(9600);
  Serial1.begin(600);

  pinMode(A0, INPUT);
  pinMode(A1, INPUT);
  pinMode(A2, INPUT);

  pinMode(COMPARATOR_PIN, INPUT);
  attachInterrupt(digitalPinToInterrupt(COMPARATOR_PIN), countPulse, RISING);

  float initValue = readDiff();

  for (int i = 0; i < AVERAGE_SIZE; i++)
      averageBuffer[i] = initValue;

  averageFull = true;

  delay(500);

  getExtreme(baseMaxAvg, baseMinAvg);

  Serial.println("System Ready");

  //Wait 10s for the serial connection before proceeding
  //This ensures you can see messages from startup() on the monitor
  //Remove this for faster startup when the USB host isn't attached
  while (!Serial && millis() < 10000);  

  Serial.println(F("\nStarting Web Server"));

  //Check WiFi shield is present
  if (WiFi.status() == WL_NO_SHIELD)
  {
    Serial.println(F("WiFi shield not present"));
    while (true);
  }

  // attempt to connect to WiFi network
  Serial.print(F("Connecting to WPA SSID: "));
  Serial.println(ssid);

  WiFi.begin(ssid, pass);

  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print('.');
  }

  //Register the callbacks to respond to HTTP requests
  server.on(F("/"), handleRoot);
  server.on(F("/Basaltoid"), Basaltoid);
  server.on(F("/Gravion"), Gravion);
  server.on(F("/Regolix"), Regolix);
  server.on(F("/Lunarite"), Lunarite);
  server.on(F("/Check_Rock"), Check_Rock);
  server.on(F("/Check_Age"), Check_Age);

  server.onNotFound(handleNotFound);
  
  server.begin();
  
  Serial.print(F("HTTP server started @ "));
  Serial.println(static_cast<IPAddress>(WiFi.localIP()));
  Serial.println(strlen(webpage));
}

//Call the server polling function in the main loop
void loop()
{
  server.handleClient();
  updateRockAge();
  updateUltrasound();
  updateMagnetic();
  updateIR();

  Serial.println("========== SENSOR REPORT ==========");

  Serial.print("Magnetic : ");
  Serial.println(magneticResult);

  Serial.print("IR Type  : ");
  Serial.println(irType);

  Serial.print("Ultrasound : ");
  Serial.println(ultraDetected ? "detected" : "not detected");

  String rockType = classifyRockType();

  Serial.print("Rock Type : ");
  Serial.println(rockType);

  Serial.print("Rock Age  : ");
  Serial.println(lastGoodReading);

  Serial.println("===================================");
  
  delay(100);
}