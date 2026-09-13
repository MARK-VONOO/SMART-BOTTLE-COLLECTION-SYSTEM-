# SMART BCS Hardware Documentation

## Overview

The SMART BCS hardware platform is built around an ESP32 microcontroller and integrates sensing, identification, actuation, user feedback, data storage, and wireless communication.

The main hardware subsystems are:

- ESP32 controller
- RC522 RFID reader
- Load cell with HX711 amplifier
- HC-SR04 ultrasonic sensor
- MG996R servo motor
- 20×4 I2C LCD
- 16×2 I2C LCD
- Red, yellow, and green LED indicators
- Buzzer
- Four-key user control interface
- External power supply and buck conversion stage

The final GPIO mapping used in the working SMART BCS build is documented below.

---

## Main Controller

### ESP32 DevKit / WROOM-32

The ESP32 is the main controller of the SMART BCS.

It performs:

- RFID user identification
- Bottle weight measurement
- Bottle acceptance and rejection decisions
- Servo sorting control
- Reward-point calculation
- User session management
- LCD control
- LED and buzzer feedback
- Ultrasonic bin-level monitoring
- Non-volatile point storage
- Wi-Fi communication
- SMS receipt handling

---

## Final ESP32 GPIO Assignment

| Function | ESP32 GPIO |
|---|---:|
| RC522 SS / SDA | GPIO5 |
| RC522 RST | GPIO4 |
| RC522 SCK | GPIO18 |
| RC522 MOSI | GPIO23 |
| RC522 MISO | GPIO19 |
| HX711 DOUT | GPIO26 |
| HX711 SCK | GPIO27 |
| HC-SR04 TRIG | GPIO32 |
| HC-SR04 ECHO | GPIO33 |
| Servo Signal | GPIO13 |
| Green LED | GPIO25 |
| Yellow LED | GPIO2 |
| Red LED | GPIO15 |
| Buzzer | GPIO14 |
| I2C SDA | GPIO21 |
| I2C SCL | GPIO22 |
| Key A | GPIO34 |
| Key B | GPIO35 |
| Key # | GPIO36 |
| Key * | GPIO39 |

---

## RFID Identification

### MFRC522 RFID Reader

The RC522 RFID module is used to identify registered SMART BCS users.

### Connections

| RC522 Pin | ESP32 Connection |
|---|---|
| SDA / SS | GPIO5 |
| SCK | GPIO18 |
| MOSI | GPIO23 |
| MISO | GPIO19 |
| RST | GPIO4 |
| 3.3 V | ESP32 3.3 V |
| GND | Common Ground |

> The RC522 is powered from 3.3 V. It should not be powered directly from 5 V.

The RFID system allows the firmware to distinguish between registered-user recycling sessions and Guest Mode operation.

---

## Bottle Weight Measurement

### Load Cell + HX711

The bottle verification system uses a load cell connected through an HX711 load-cell amplifier.

### Connections

| HX711 Pin | ESP32 Connection |
|---|---|
| DOUT / DT | GPIO26 |
| SCK | GPIO27 |
| VCC | 3.3 V |
| GND | Common Ground |

The final bottle acceptance range used during system testing was:

```text

12 g to 30 g
Objects weighing less than 12 g or greater than 30 g are rejected.

An object-detection threshold of approximately:
5 g
is used to detect the presence of an item on the weighing platform.

Ultrasonic Bin-Level Monitoring
HC-SR04

The HC-SR04 ultrasonic sensor monitors the level of bottles inside the collection bin.
Connections
HC-SR04 Pin	ESP32 Connection
TRIG	GPIO32
ECHO	GPIO33
VCC	5 V
GND	Common Ground
The practical full-bin detection region observed during testing was approximately:
17–20 cm

The firmware uses a nominal software threshold of approximately:
20 cm
When the measured distance indicates that the bin is full, the system prevents new recycling sessions until the collection bin is emptied.

Servo Sorting Mechanism
MG996R Servo Motor

The servo motor operates the bottle sorting mechanism.
The tested servo positions are:
Function	Angle
Accept Bottle	0°
Standby	90°
Reject Bottle	140°

The sorting position is held for approximately:
1.5 seconds before the servo returns to the standby position.

Servo Connection
Servo Wire	Connection
Signal	GPIO13
V+	External regulated 5–6 V supply
Ground	Common Ground

The servo should be supplied from a suitable external high-current supply rather than directly from the ESP32 regulator.
A local bulk capacitor near the servo supply is recommended to reduce voltage dips during movement.

LCD User Interface

SMART BCS uses two I2C LCD displays.
Upper Dashboard LCD
Size: 20×4
I2C Address: 0x27
Lower Slot LCD
Size: 16×2
I2C Address: 0x26

Both displays share:
SDA → GPIO21
SCL → GPIO22
The displays provide:
User instructions
Bottle weight feedback
Accepted/rejected status
Session reward points
RFID confirmation
Bin status
Session completion messages

LED Indicators

Three LEDs provide visual operating status.
Indicator	ESP32 GPIO
Green LED	GPIO25
Yellow LED	GPIO2
Red LED	GPIO15

Typical meanings include:

Green: system ready or successful bottle acceptance
Yellow: standby, processing, or waiting state
Red: rejection, error, or full-bin condition
Each LED should be used with a suitable current-limiting resistor.

Buzzer

The buzzer provides audible feedback for:
Power activation
Bottle acceptance
Bottle rejection
Session start
Session completion
Error or warning conditions
Connection
Buzzer Control → GPIO14
A transistor driver may be used where th buzzer current exceeds the safe GPIO output capability.

Four-Key User Interface

The final SMART BCS build uses four essential control keys.
Key	GPIO	Function
A	GPIO34	Hold approximately 2 seconds for ON/STANDBY
B	GPIO35	Guest Mode
#	GPIO36	Finish current session
*	GPIO39	Cancel/Back before first bottle
GPIO34, GPIO35, GPIO36, and GPIO39 are input-only pins on the ESP32.
External 10 kΩ pull-up resistors to 3.3 V are therefore used.

Typical key wiring:

3.3 V
  |
10 kΩ
  |
GPIO ---- Push Button ---- GND
The input reads HIGH when idle and LOW when the key is pressed.

Power System
The SMART BCS uses an external 12 V supply with voltage conversion for the electronics and servo.
Recommended arrangement:

12 V Supply
   |
   +---- 5 V Logic Rail
   |      ├── ESP32 VIN
   |      ├── LCD Displays
   |      ├── HC-SR04
   |      └── Other low-power electronics
   |
   +---- 5–6 V Servo Rail
          └── MG996R Servo

All system grounds must be connected together.

The servo power path should be capable of supplying high transient current without causing the ESP32 voltage to collapse.

Recommended local servo decoupling includes:

1000–2200 µF electrolytic capacitor
+
0.1 µF ceramic capacitor

placed close to the servo power connection.

Non-Volatile Storage

The ESP32 internal flash is used through Preferences/NVS to retain important information such as:
Registered-user point balances
Load-cell offset
Pending SMS receipts
SMS retry state

This allows important user information to survive system restart or power interruption.
Wireless Communication
The ESP32 connects to a 2.4 GHz Wi-Fi network.
Wi-Fi is used to communicate with the mNotify BMS SMS service.
Registered users can receive recycling receipts containing information such as:

Bottles processed
Bottles accepted
Bottles rejected
Points earned
Total reward balance

Private Wi-Fi credentials, API keys, phone numbers, and RFID identifiers are not included in the public GitHub firmware.

Hardware Safety Notes
Use 3.3 V for the RC522 RFID reader.
Use a suitable external supply for the MG996R servo.
Connect all system grounds together.
Do not power high-current loads directly from ESP32 GPIO pins.
Use resistors with LEDs.
Use external pull-ups for GPIO34, GPIO35, GPIO36, and GPIO39.
Ensure the HC-SR04 ECHO signal does not expose the ESP32 GPIO to unsafe voltage levels.
Avoid allowing servo current to flow through sensitive sensor or ESP32 ground wiring.
Verify the external supply voltage before connecting the ESP32.
Related Documentation

The repository also contains:

diagrams/system-architecture.pdf
diagrams/electrical-schematic.pdf
firmware/smart_bcs_public.ino

These files provide additional information about the SMART BCS system architecture, electrical connections, and firmware implementation.

Author
MARK VONOO
SMART BCS — Smart Bottle Collection System
Ghana
