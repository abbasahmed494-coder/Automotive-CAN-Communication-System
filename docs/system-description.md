# System Description — Automotive CAN Communication Demo

## Purpose

This project demonstrates the basic structure of a Controller Area Network (CAN) message exchange in a Wokwi simulation. An ESP32 interacts with two emulated MCP2515 CAN controllers. A pushbutton initiates a message, and a received matching message changes the state of an LED.

## System blocks

- **ESP32 DevKit:** Runs the Arduino sketch, reads the button, communicates with both CAN controllers over SPI, and drives the LED.
- **Sender MCP2515 model:** Receives SPI commands from the ESP32 and models transmission of a CAN frame.
- **Receiver MCP2515 model:** Exposes the received frame through its emulated receive buffer and interrupt signal.
- **CAN bus:** The circuit connects the two emulated controllers using `CAN_H` and `CAN_L`.
- **Pushbutton:** Connected to GPIO 14 and ground. The GPIO uses the internal pull-up, so a press reads LOW.
- **LED and series resistor:** Connected to GPIO 16 and ground to indicate receipt of a matching frame.

## Message definition

| Field | Value |
|---|---|
| CAN identifier | `0x01` |
| Frame type requested by code | Standard data frame (`ext = 0`) |
| Payload length | 1 byte |
| Payload byte 0 | `0x01` |
| Nominal bitrate requested | 125 kbps |
| MCP2515 oscillator setting | 16 MHz |

## Runtime flow

1. `setup()` configures GPIO 14 as `INPUT_PULLUP`, GPIO 16 as an output, and starts serial output at 115200 baud.
2. The sketch initializes two `MCP_CAN` objects, with chip-select pins GPIO 5 and GPIO 15.
3. In `loop()`, a LOW button reading causes the sender to request transmission of ID `0x01` and payload `0x01`.
4. The receiver polls for a CAN message. If the received identifier and first data byte match, the LED output toggles. Otherwise, the code drives the LED LOW when the `else` branch is reached for a received message.
5. Serial messages provide basic diagnostic feedback.

## Limitations and safety

This deliverable documents a simulation design from the uploaded files. The screenshot is static, and no runtime output or verified test recording was included, so functional operation remains unconfirmed. The custom MCP2515 code emulates controller behavior and should not be treated as proof of conformance to a physical MCP2515 or automotive CAN network.

A practical hardware version needs appropriate CAN transceivers, correct power and logic levels, a correctly wired differential bus, suitable termination, and bench validation before any vehicle connection. The sketch does not implement robust debounce, retransmission diagnostics, bus-off recovery, message authentication, or vehicle-grade safety behavior.
