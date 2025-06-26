#include <IRremote.hpp>

const int RECV_PIN = 12;

// Fog Light Pins (LED 1)
const int FOG_R = 5;   // PWM
const int FOG_G = 6;   // PWM
const int FOG_B = 9;   // PWM

// Transparent Light Pins (LED 2)
const int CLEAR_R = 10; // PWM
const int CLEAR_G = 3;  // Digital only
const int CLEAR_B = 11; // Digital only

// === State Variables ===
bool fogOn = false;
bool clearOn = false;
bool auxMode = false;

int brightnessLevel = 1; // 0=Low, 1=Mid, 2=High
int colorTempStep = 2;   // 0~4

const int brightnessPWM[3] = {254, 80, 30};

void setup() {
  Serial.begin(9600);
  IrReceiver.begin(RECV_PIN, ENABLE_LED_FEEDBACK);

  pinMode(FOG_R, OUTPUT);
  pinMode(FOG_G, OUTPUT);
  pinMode(FOG_B, OUTPUT);
  pinMode(CLEAR_R, OUTPUT);
  pinMode(CLEAR_G, OUTPUT);
  pinMode(CLEAR_B, OUTPUT);

  updateLights();
}

void loop() {
  if (IrReceiver.decode()) {
    if (IrReceiver.decodedIRData.protocol == NEC) {
      uint8_t cmd = IrReceiver.decodedIRData.command;
      Serial.print("Received command: 0x");
      Serial.println(cmd, HEX);

      switch (cmd) {
        case 0x05: // Night Light
          fogOn = true;
          clearOn = false;
          auxMode = false;
          break;

        case 0x01: // Toggle Transparent LED
          clearOn = !clearOn;
          fogOn = false;
          auxMode = false;
          break;

        case 0x1A: // Auxiliary Light
          fogOn = true;
          clearOn = true;
          auxMode = true;
          brightnessLevel = 2; // force max
          break;

        case 0xFF: // Brightness Step
          auxMode = false;
          brightnessLevel = (brightnessLevel + 1) % 3;
          break;

        case 0x12: // Up = increase brightness
          auxMode = false;
          if (brightnessLevel < 2) brightnessLevel++;
          break;

        case 0x1E: // Down = decrease brightness
          auxMode = false;
          if (brightnessLevel > 0) brightnessLevel--;
          break;

        case 0x02: // Color Temp +
          if (colorTempStep < 4) colorTempStep++;
          break;

        case 0x03: // Color Temp -
          if (colorTempStep > 0) colorTempStep--;
          break;

        case 0x06: // Reset color temp
          colorTempStep = 2;
          break;

        default:
          Serial.println("⚠️ Unknown or unhandled command.");
          break;
      }
      updateLights();
    }
    IrReceiver.resume();
  }
}

void updateLights() {
  brightnessLevel = constrain(brightnessLevel, 0, 2);
  colorTempStep = constrain(colorTempStep, 0, 4);

  int fogPWM = brightnessPWM[brightnessLevel];
  int clearPWM = brightnessPWM[brightnessLevel];

  // Fog light - warm yellow (R+G)
  if (fogOn) {
    analogWrite(FOG_R, 255 - fogPWM);
    analogWrite(FOG_G, 255 - fogPWM);
    analogWrite(FOG_B, 255);
  } else {
    analogWrite(FOG_R, 255);
    analogWrite(FOG_G, 255);
    analogWrite(FOG_B, 255);
  }

  // Transparent light - color temp via PWM only on CLEAR_R
  if (clearOn) {
    float red = 1.0, green = 1.0, blue = 1.0;
    switch (colorTempStep) {
      case 0: red = 1.0; blue = 0.2; break;
      case 1: red = 1.0; blue = 0.5; break;
      case 2: red = 1.0; blue = 1.0; break;
      case 3: red = 0.7; blue = 1.0; break;
      case 4: red = 0.4; blue = 1.0; break;
    }
    int r = 255 - int(constrain(red * clearPWM, 0, 255));
    bool g_on = (green * clearPWM) >= 127;
    bool b_on = (blue * clearPWM) >= 127;
    Serial.print(clearPWM);

    analogWrite(CLEAR_R, r);
    digitalWrite(CLEAR_G, g_on ? LOW : HIGH);
    digitalWrite(CLEAR_B, b_on ? LOW : HIGH);
  } else {
    analogWrite(CLEAR_R, 255);
    digitalWrite(CLEAR_G, HIGH);
    digitalWrite(CLEAR_B, HIGH);
  }

  Serial.print("Fog: "); Serial.print(fogOn);
  Serial.print("  Clear: "); Serial.print(clearOn);
  Serial.print("  Aux: "); Serial.print(auxMode);
  Serial.print("  Brightness Level: "); Serial.print(brightnessLevel);
  Serial.print("  Color Temp Step: "); Serial.println(colorTempStep);
}