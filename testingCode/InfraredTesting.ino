volatile unsigned long pulseCount = 0;
 
void pulseISR() {
  pulseCount++;
}
 
void setup() {
  Serial.begin(115200);
  attachInterrupt(digitalPinToInterrupt(2), pulseISR, RISING);
}
 
void loop() {
  noInterrupts();
  pulseCount = 0;
  interrupts();
 
  unsigned long start = micros();
 
  while (micros() - start < 1000000UL) {
    // 1 second
  }
 
  noInterrupts();
  unsigned long count = pulseCount;
  interrupts();
 
  Serial.print("Count = ");
  Serial.println(count);
 
  delay(500);
}
