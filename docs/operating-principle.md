# SMART BCS Operating Principle

## Overview

SMART BCS is an automated bottle collection and reward system built around an ESP32 microcontroller.

The system identifies users, checks bottle weight, automatically accepts or rejects bottles, awards points to registered users, monitors collection-bin level, and sends SMS transaction receipts.

SMART BCS supports two modes of operation:

- Registered User Mode
- Guest Mode

---

## 1. System Startup

When SMART BCS is powered on, the ESP32 initializes the connected hardware, including:

- RC522 RFID reader
- HX711 load-cell interface
- Servo sorting mechanism
- HC-SR04 ultrasonic sensor
- LCD displays
- LED indicators
- Buzzer
- Four-key control interface
- Wi-Fi communication
- Non-volatile storage

The servo is moved to its standby position.

The system also restores stored information such as:

- User reward balances
- Load-cell calibration offset
- Pending SMS receipts
- SMS retry state

---

## 2. Startup Safety Check

SMART BCS performs a safety check on the weighing platform during startup.

If an object is already detected on the platform, the system does not treat it as a normal recycling transaction.

Instead, the object is automatically rejected.

The startup object:

- Does not receive reward points
- Is routed toward the rejection path
- Must be cleared before normal operation begins

This prevents an object left inside the system before startup from being incorrectly processed.

---

## 3. Standby and Activation

The system normally begins in standby mode.

The user can hold Key A for approximately two seconds to activate SMART BCS.

### Key Functions

| Key | Function |
|---|---|
| A | ON / STANDBY |
| B | Guest Mode |
| # | Finish Session |
| * | Cancel / Back |

When active, the system displays instructions requesting either:

- An RFID card scan, or
- Guest Mode selection

---

## 4. Registered User Operation

A registered user begins a session by scanning an RFID card.

The RC522 reader obtains the card UID and sends it to the ESP32.

The firmware compares the UID against the registered-user database.

If a matching account is found:

1. The user is verified.
2. A welcome message is displayed.
3. The system prompts the user to insert an empty bottle.
4. The user's stored total reward balance remains in memory.
5. The LCD displays only the points earned during the current session.

If the RFID card is not registered, the system can continue in Guest Mode without reward points.

---

## 5. Guest Mode

A user without a registered RFID card can press Key B to enter Guest Mode.

Guest Mode allows bottle disposal without creating or using a user account.

All bottle-verification and sorting functions remain active.

However:

- No reward points are stored.
- No registered-user balance is updated.
- No personal SMS receipt is required.

---

## 6. Bottle Detection

The weighing platform is continuously monitored through the load cell and HX711.

An object-detection threshold of approximately:

```text
5 g
is used to determine whether an item has been placed on the platform.
Once an object is detected, SMART BCS begins the weighing and verification process.

7. Bottle Weight Verification
The system measures the bottle weight using the load cell and HX711 amplifier.
The implemented acceptance range is:
12 g to 30 g
The decision process is:
Weight Below 12 g
The object is considered unsuitable and is rejected.
Weight Between 12 g and 30 g
The bottle is accepted.
Weight Above 30 g
The bottle is rejected and the user is instructed to empty the bottle before trying again.
This range was selected based on the empty PET bottles used during system development and testing.

8. Automatic Sorting
The MG996R servo motor controls the mechanical sorting gate.
The tested positions are:
State	Servo Angle
Accept	0°
Standby	90°
Reject	140°

For an accepted bottle:
The servo moves to the accept position.
The bottle is directed into the collection path.
The position is held for approximately 3.5 seconds.
The servo returns to standby.
For a rejected bottle:
The servo moves to the reject position.
The bottle is directed toward the rejection path.
The position is held for approximately 3.5 seconds.
The servo returns to standby.

9. Reward Allocation
Registered users receive:
10 points per accepted bottle
Points are added only when a bottle passes the weight-validation process.
Rejected bottles do not earn points.
During the session, the LCD displays the points earned in that current transaction session.
The accumulated total balance is stored internally using ESP32 NVS.

10. Persistent Reward Storage
SMART BCS uses the ESP32 Preferences/NVS system to preserve user balances.
This means reward information can remain available after:
System restart
Power interruption
Temporary shutdown
The total stored balance is not displayed publicly on the LCD during operation.
It is included in the registered user's SMS receipt.

11. Session Operation
A session can continue bottle-by-bottle.
After each bottle:
The system waits for the platform to become clear.
The servo returns to standby.
The user can insert another bottle.
The user may press:
# = Finish Session
The system also ends the session automatically if no new bottle is detected within the configured inactivity period.
The current firmware uses an inactivity timeout of approximately:
13.3 seconds

12. Bin-Level Monitoring
The HC-SR04 ultrasonic sensor monitors the bottle collection bin.
The firmware uses a nominal full-bin threshold of approximately:
20 cm
During practical testing, the full-bin condition was generally observed around:
17–20 cm
depending on how the collected bottles were arranged.
When the bin is considered full:
New transactions are prevented.
The red warning indication is activated.
The LCD displays a full-bin warning.
The system waits for the collection bin to be emptied.
Normal operation resumes after the bin-level condition becomes acceptable again.

13. LCD Feedback
SMART BCS uses two I2C LCD displays.
Upper Display
20×4 LCD
I2C Address: 0x27

This display presents information such as:
System state
RFID verification
Bottle weight
Acceptance or rejection
Session points
Bin status
Lower Display
16×2 LCD
I2C Address: 0x26
This display provides shorter user instructions such as:
EMPTY BOTTLE
INSERT BOTTLE
ACCEPTED
TRY AGAIN
REMOVE CARD

14. LED and Buzzer Feedback
The system uses green, yellow, and red LEDs to provide visual status information.
Typical states include:
Green: ready or accepted
Yellow: standby or processing
Red: rejected, error, or bin full
The buzzer provides audible feedback for:
Activation
Session start
Bottle acceptance
Bottle rejection
Session completion
Warning conditions

15. SMS Receipt System
Registered-user sessions can generate an SMS transaction receipt through the mNotify BMS service.
The receipt may contain:
User name
Bottles processed
Bottles accepted
Bottles rejected
Points earned
Total reward balance
SMART BCS does not rely only on an HTTP connection to assume that a message was successfully accepted.
The firmware checks the service response before treating the SMS request as successful.

16. Offline SMS Queue
If Wi-Fi is unavailable or the SMS request cannot be confirmed, the receipt can be stored in an internal pending queue.
The queue is stored in ESP32 NVS.
This allows pending receipts to survive a power interruption.
When communication becomes available again, SMART BCS can retry sending the oldest pending receipt.
The queued receipt is removed only after successful confirmation from the SMS service.

17. End of Registered Session
At the end of a registered-user session:
1.Accepted bottles are counted.
2.Session points are calculated.
3.The accumulated balance is updated.
4.The balance is saved in NVS.
5.An SMS receipt is queued or sent.
6.The LCD displays the session summary.
7.The user is instructed to remove the RFID card.

A typical completion display includes:
SESSION COMPLETE
Bottles Accepted: 3
Points Earned: 30
REMOVE YOUR CARD

18. Overall Operating Sequence
The simplified operating flow is:

POWER ON
   |
   v
Initialize Hardware
   |
   v
Startup Safety Check
   |
   v
STANDBY
   |
   v
Activate SMART BCS
   |
   +----------------------+
   |                      |
   v                      v
Scan RFID             Guest Mode
   |                      |
   +----------+-----------+
              |
              v
       Check Bin Level
              |
              v
        Insert Bottle
              |
              v
       Detect and Weigh
              |
              v
       12 g <= Weight <= 30 g?
          /             \
        YES              NO
         |                |
         v                v
      ACCEPT            REJECT
         |                |
         v                v
   Servo Accept       Servo Reject
         |
         v
 Award 10 Points
(Registered User)
         |
         v
 Store Updated Balance
         |
         v
 Wait for Next Bottle
         |
         +---- # / Timeout ----+
                               |
                               v
                       Complete Session
                               |
                               v
                     Send / Queue SMS
                               |
                               v
                              END
Related Project Files
Additional technical information is available in:

docs/hardware.md
diagrams/system-architecture.pdf
diagrams/electrical-schematic.pdf
firmware/smart_bcs_public.ino

Author
MARK VONOO
SMART BCS — Smart Bottle Collection System
Ghana
