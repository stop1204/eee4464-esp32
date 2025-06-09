// this code is for the Arduino board
// this device only has one sensor, so we can use the MAX30102 sensor to measure heart rate
// and then send the data to the main device via Bluetooth or Wi-Fi or 433MHz or LoRa

#include <Wire.h>
#include "MAX30105.h"
#include "heartRate.h"

MAX30105 sensor;

unsigned long previousMillis[] =  {0, 0, 0, 0}; //

const byte SAMPLE_COUNT = 4;
byte bpmHistory[SAMPLE_COUNT];
byte historyIndex = 0;

long lastBeatTime = 0;
float currentBPM = 0;
int averageBPM = 0;


void sendDataToESP32(int hr)
{
  Serial.print("{\"hr\":");
  Serial.print(hr);

  Serial.println("}");
}

void setup() {
  Serial.begin(9600);
  Serial.println("Initializing MAX30102...");

  if (!sensor.begin(Wire, I2C_SPEED_FAST)) {
    Serial.println("Sensor not found. Check wiring.");
    while (1);
  }

  sensor.setup();
  sensor.setPulseAmplitudeRed(0x2F);   // Stronger LED for clearer signal

}

void loop() {


  heartbeat(); // cannot add delay for this function


}

void heartbeat() {
  long ir = sensor.getIR();

  if (checkForBeat(ir)) {
    long currentTime = millis();
    long interval = currentTime - lastBeatTime;
    lastBeatTime = currentTime;

    float bpm = 60.0 / (interval / 1000.0);
    if (bpm >= 40 && bpm <= 180) {
      bpmHistory[historyIndex] = (byte)bpm;
      historyIndex = (historyIndex + 1) % SAMPLE_COUNT;

      int total = 0;
      for (byte i = 0; i < SAMPLE_COUNT; i++) {
        total += bpmHistory[i];
      }
      averageBPM = total / SAMPLE_COUNT;
      currentBPM = bpm;
      sendDataToESP32(averageBPM);
    }
  }

  unsigned long currentMillis = millis();

  if (currentMillis - previousMillis[1] >= 500) {
    previousMillis[1] = currentMillis;

//    Serial.print("IR=");
//    Serial.print(ir);
//    Serial.print(" | BPM=");
//    Serial.print(currentBPM);
//    Serial.print(" | Avg BPM=");
//    Serial.print(averageBPM);
//    if (ir < 50000) Serial.print(" (No finger detected)");
//    Serial.println();
  }

}
