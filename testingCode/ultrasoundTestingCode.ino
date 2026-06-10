bool ultraDetected = false;
unsigned long lastDetectedTime = 0;
void setup() {
  //Initialise pin modes and serial
  pinMode(A0, INPUT);
  Serial.begin(9600);

}

void loop() {
  //Read input voltage
  int thresholdVoltage = 115, VA0 = analogRead(A0);
  

  if(VA0 >= thresholdVoltage){
    ultraDetected = true;
    lastDetectedTime = millis();
  }
  if (ultraDetected && (millis() - lastDetectedTime >= 2000)) {
        ultraDetected = false;
    }
  Serial.println(VA0);
  Serial.println(ultraDetected);

  //Pause before next calculation
  delay(250);
}

