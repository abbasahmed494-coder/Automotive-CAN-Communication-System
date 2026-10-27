
#include <SPI.h>
#include <mcp_can.h>

#define BTN 14
#define LED 16

MCP_CAN CAN_SENDER(5);     // CS pin 5 for sender (chip 1)
MCP_CAN CAN_RECEIVER(15);  // CS pin 15 for receiver (chip 2)

void setup() {
  pinMode(BTN, INPUT_PULLUP);
  pinMode(LED, OUTPUT);

  Serial.begin(115200);

  // Initialize sender chip
  if (CAN_SENDER.begin(MCP_ANY, CAN_125KBPS, MCP_16MHZ) == CAN_OK) {
    Serial.println("CAN sender initialized!");
  } else {
    Serial.println("CAN sender initialization failed!");
  }

  // Initialize receiver chip
  if (CAN_RECEIVER.begin(MCP_ANY, CAN_125KBPS, MCP_16MHZ) == CAN_OK) {
    Serial.println("CAN receiver initialized!");
  } else {
    Serial.println("CAN receiver initialization failed!");
  }
}

void loop() {
  byte canData[8];
  unsigned long canId;
  byte len;
  static bool led_status = LOW;

  // Send a CAN message when the button is pressed (pin goes LOW)
  if (digitalRead(BTN) == LOW) {
    Serial.println("Button pressed. Sending CAN message...");

    canData[0] = 0x01;

    CAN_SENDER.sendMsgBuf(0x01, 0, 1, canData);
    delay(500);
  }

  // Receive a CAN message using the second controller
  if (CAN_RECEIVER.readMsgBuf(&canId, &len, canData) == CAN_OK) {
    Serial.print("Message received! ID: 0x");
    Serial.println(canId, HEX);

    if (canId == 0x01 && canData[0] == 0x01) {
      digitalWrite(LED, !led_status);
      led_status = !led_status;
    } else {
      digitalWrite(LED, LOW);
    }
  }
}
