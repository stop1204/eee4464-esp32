void setup() {
  Serial.begin(9600); // Arduino唯一的硬體UART
}

void sendDataToESP32(const char* msg) {
  Serial.println(msg);
}

void loop() {
  sendDataToESP32("Hello ESP32!");
  delay(1000);
}
