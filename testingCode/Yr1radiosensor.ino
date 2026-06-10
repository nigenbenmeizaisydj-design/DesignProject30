String buffer = "";
String lastGoodReading = "";
int readingCount = 0;
 
void setup() {
  Serial.begin(9600);
  Serial1.begin(600);
  Serial.println("Listening for rock age...");
}
 
bool isValidReading(String s) {
  // Check format: # followed by 3 digits
  if (s.length() != 4) return false;
  if (s[0] != '#') return false;
  for (int i = 1; i < 4; i++) {
    if (!isDigit(s[i])) return false;
  }
  return true;
}
 
void loop() {
  while (Serial1.available()) {
    char c = Serial1.read();
    if (c == '#') {
      buffer = "#";
    }
    else if (buffer.length() > 0) {
      buffer += c;
      if (buffer.length() >= 4) {
        if (isValidReading(buffer)) {
          lastGoodReading = buffer;
          readingCount++;
          Serial.println("Rock age: " + buffer);
        }
        buffer = "";
      }
    }
  }
}



