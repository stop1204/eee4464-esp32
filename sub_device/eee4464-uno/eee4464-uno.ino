// this code is for the Arduino board
// this device only has one sensor, so we can use the MAX30102 sensor to measure heart rate
// and then send the data to the main device via Bluetooth or Wi-Fi or 433MHz or LoRa

#include <Wire.h>
#include "MAX30105.h"
#include "spo2_algorithm.h"

MAX30105 sensor;
const int ledPin = 13;

#if defined(__AVR_ATmega328P__) || defined(__AVR_ATmega168__)
uint16_t irBuffer[BUFFER_SIZE];
uint16_t redBuffer[BUFFER_SIZE];
#else
uint32_t irBuffer[BUFFER_SIZE];
uint32_t redBuffer[BUFFER_SIZE];
#endif

int32_t bufferLength;
int32_t spo2 = 0;
int8_t validSPO2 = 0;
int32_t heartRate = 0;
int8_t validHeartRate = 0;

void sendDataToESP32(int hr, int spo2)
{
  Serial.print("{\"hr\":");
  Serial.print(hr);
  Serial.print(",\"spo2\":");
  Serial.print(spo2);
  Serial.println("}");
}

void setup() {
  Serial.begin(115200);
  Serial.println("Initializing MAX30102...");
  pinMode(ledPin, OUTPUT);

  if (!sensor.begin(Wire, I2C_SPEED_FAST)) {
    Serial.println("Sensor not found. Check wiring.");
    while (1);
  }

  sensor.setup();
  sensor.setPulseAmplitudeRed(0x2F);

  bufferLength = BUFFER_SIZE;
  for (int i = 0; i < bufferLength; i++) {
    while (sensor.available() == false)
      sensor.check();
    redBuffer[i] = sensor.getRed();
    irBuffer[i] = sensor.getIR();
    sensor.nextSample();
  }

  maxim_heart_rate_and_oxygen_saturation(irBuffer, bufferLength, redBuffer,
                                         &spo2, &validSPO2, &heartRate,
                                         &validHeartRate);
}

void loop() {
  // shift the old samples
  for (byte i = 25; i < 100; i++) {
    redBuffer[i - 25] = redBuffer[i];
    irBuffer[i - 25] = irBuffer[i];
  }

  // collect new samples
  for (byte i = 75; i < 100; i++) {
    while (sensor.available() == false)
      sensor.check();

    digitalWrite(ledPin, !digitalRead(ledPin));

    redBuffer[i] = sensor.getRed();
    irBuffer[i] = sensor.getIR();
    sensor.nextSample();
  }

  maxim_heart_rate_and_oxygen_saturation(irBuffer, bufferLength, redBuffer,
                                         &spo2, &validSPO2, &heartRate,
                                         &validHeartRate);

  if (validHeartRate && validSPO2) {
    sendDataToESP32(heartRate, spo2);
  }

  Serial.print("HR=");
  Serial.print(heartRate);
  Serial.print(" SPO2=");
  Serial.println(spo2);
}