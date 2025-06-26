#include <IRremote.hpp>

const int RECV_PIN = 12;

void setup() {
  Serial.begin(9600);
  IrReceiver.begin(RECV_PIN, ENABLE_LED_FEEDBACK);
}

void loop() {
  if (IrReceiver.decode()) {
    if (IrReceiver.decodedIRData.protocol == NEC) {
      Serial.println("------");
      Serial.print("Raw data: 0x");
      Serial.println(IrReceiver.decodedIRData.decodedRawData, HEX);

      Serial.print("Address: 0x");
      Serial.println(IrReceiver.decodedIRData.address, HEX);

      Serial.print("Command: 0x");
      Serial.println(IrReceiver.decodedIRData.command, HEX);

      Serial.println("Protocol: NEC");
    }
    IrReceiver.resume();
  }
}