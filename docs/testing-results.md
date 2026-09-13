# SMART BCS Testing and Results

## Overview

SMART BCS was subjected to repeated functional, operational, and communication testing to evaluate its performance as a complete bottle collection and reward system.

The tests covered:
- Guest Mode operation
- Registered-user transactions
- Bottle weight verification
- Servo sorting
- RFID identification
- Reward-point storage
- Ultrasonic bin-level monitoring
- Wi-Fi communication
- SMS receipt delivery
- Power-loss persistence
- User interaction

---

## 1. Guest Mode Testing

Guest Mode was tested through repeated bottle disposal cycles without requiring RFID authentication.
More than:
100 Guest Mode transaction cycles
were completed during development and testing.

The system successfully:
Detected inserted bottles
Measured bottle weight
Accepted valid bottles
Rejected unsuitable bottles
Operated the servo sorting mechanism
Returned to the ready state after each transaction
Guest Mode confirmed that the bottle verification and sorting system could operate independently of the registered-user reward system.


2. Registered User Testing
Registered-user operation was tested using RFID identification.

During cumulative testing, the registered account reached:
580 points

Since SMART BCS awards:
10 points per accepted bottle

this corresponds to:
58 accepted bottle events
Rejected bottles did not receive reward points.
The stored balance was successfully retained across system restarts using ESP32 non-volatile storage.


3. Bottle Weight Testing

Bottle verification was performed using a load cell and HX711 amplifier.
The implemented acceptance range was:
12 g to 30 g

Measured empty 500 mL PET bottles during practical testing were approximately:
16.2 g to 27.02 g
These values were within the configured acceptance range.
Objects below the lower limit were rejected as unsuitable items.
Objects above the upper limit were rejected and the user was instructed to empty the bottle before trying again.


4. Load-Cell Calibration
The HX711 was calibrated during development to obtain stable bottle-weight measurements.

A tested calibration factor of approximately:
420
was used in the final firmware.

The saved empty-platform offset used by the system was approximately:
61479
The system was configured not to automatically tare during startup because an object could already be present on the weighing platform.
Instead, the stored offset is restored from non-volatile memory.


5. Startup Object Safety Test
SMART BCS was tested with an object already present on the weighing platform during startup.

The system successfully detected the object and automatically rejected it before allowing normal user operation.

The startup object:

Was not treated as a valid transaction
Did not receive reward points
Was directed toward the rejection path
Had to be cleared before normal operation resumed

This test confirmed the effectiveness of the startup safety logic.


6. Servo Sorting Test
The MG996R servo motor was tested for both bottle acceptance and rejection.

The final tested positions were:
Function	Angle
Accept	0°
Standby	90°
Reject	140°

The sorting position was held for approximately:

1.5 seconds
before returning to standby.
Early Mechanical Issue

During early testing, the reject position was close to:
180°
This caused mechanical binding and occasional jamming.
The reject position was reduced to approximately:
140°
which improved mechanical operation and reduced binding.
Some early jams were also associated with bottles larger than the intended 500 mL bottle size.


7. RFID Testing
The RC522 RFID reader was tested independently and as part of the complete SMART BCS system.

The system successfully:
Read RFID card UIDs
Matched registered cards against the user database
Started registered-user sessions
Started Guest Mode for unregistered cards
Recovered the RFID reader after communication faults
Additional firmware recovery logic was introduced to improve reliability when the RFID reader temporarily stopped responding.


8. Reward Persistence Test
The ESP32 NVS system was tested to confirm that user balances survived power interruption and restart.
The system successfully restored previously stored reward points after reboot.
This prevented accumulated user rewards from being lost when SMART BCS was powered off.


9. Ultrasonic Bin-Level Testing
The HC-SR04 ultrasonic sensor was tested as a collection-bin level monitor.
The firmware uses a nominal full-bin threshold of approximately:
20 cm
During practical testing, the full-bin condition was generally observed between:
17 cm and 20 cm
depending on the arrangement and position of bottles inside the collection bin.
When the full-bin condition was detected, the system:
Prevented new recycling sessions
Activated the red warning indication
Displayed a service message
Waited for the bin to be emptied before resuming operation


10. Wi-Fi Testing
The ESP32 Wi-Fi subsystem was tested under connected and disconnected conditions.
SMART BCS was designed so that Wi-Fi availability does not prevent the main bottle collection system from starting.
When Wi-Fi is unavailable:
Bottle collection can continue
Reward points remain stored
SMS receipts can be queued for later transmission
The ESP32 attempts to reconnect to Wi-Fi automatically in the background.


11. SMS Receipt Testing
SMART BCS uses the mNotify BMS SMS service for registered-user transaction receipts.
SMS functionality was tested successfully.

A recorded mNotify dashboard sample showed:
Total Messages: 36
Delivered: 36
Delivery Rate: 100%
for the displayed test period.
The successful messages confirmed communication between:

ESP32
   ↓
Wi-Fi
   ↓
mNotify BMS API
   ↓
Registered User Phone


12. SMS Verification Logic
The firmware does not assume that every HTTP request represents successful message delivery.
A successful request is verified using information returned by the SMS service.
The firmware checks conditions including:

HTTP success response
Service status
Successful response code
No rejected recipients
Confirmation of the intended recipient
Only after successful confirmation is a queued SMS receipt removed from local storage.

13. Offline SMS Queue Test
The SMS receipt queue was tested under failed or unavailable communication conditions.

When transmission was unavailable:
The receipt was stored in ESP32 NVS.
The system retained the receipt after restart.
Wi-Fi reconnection was attempted.
The pending receipt was retried.
The receipt was removed only after confirmed successful transmission.
This test demonstrated that recycling transaction information could survive temporary communication failure.


14. LCD and User Feedback Testing
The dual LCD displays were tested throughout the user workflow.

The displays successfully presented messages for:
System startup
Standby
RFID verification
Guest Mode
Bottle insertion
Weight measurement
Bottle acceptance
Bottle rejection
Session points
Bin-full warning
Session completion
Remove-card instruction
LED indicators and buzzer signals were also synchronized with key system events.


15. User Interaction
SMART BCS was demonstrated to and interacted with by approximately:

6 lecturers
and more than:
10 students
during development and demonstration activities.
These interactions provided practical observations of the system's usability and operation.
No formal System Usability Scale (SUS) study was conducted, so no formal usability score is claimed.


16. Summary of Recorded Results
Test Area	Recorded Result
Guest Mode testing	100+ transaction cycles
Registered-user reward total	580 points
Accepted registered bottle events	58
Reward per accepted bottle	10 points
Bottle acceptance range	12–30 g
Tested empty bottle weights	16.2–27.02 g
Servo reject position	140°
Sorting hold time	3.5 s
Full-bin software threshold	~20 cm
Practical full-bin region	~17–20 cm
SMS dashboard sample	36 / 36 delivered
SMS delivery rate in sample	100%
Lecturers involved	~6
Students involved	10+


18. Engineering Lessons
Testing SMART BCS highlighted several important engineering lessons.

Mechanical Design
Servo angle selection must account for the physical limits of the sorting mechanism.

Power Design
High-current loads such as servo motors can affect microcontroller stability if power distribution is poorly designed.

Sensor Calibration
Load-cell performance depends strongly on proper calibration, mechanical mounting, and stable power.

Communication Reliability
Network-dependent services should not prevent core system operation.

Persistent Storage
Important user information and pending transactions should survive restart or power interruption.

System Integration
A subsystem that works independently may behave differently when integrated with the complete system.
For this reason, complete-system testing was essential.

Conclusion
The testing process demonstrated that SMART BCS could successfully integrate bottle verification, user identification, automated sorting, reward storage, collection-bin monitoring, and SMS communication into a single working system.

The results also helped identify and correct practical issues involving servo movement, power stability, RFID reliability, and communication recovery.
These iterations contributed significantly to the final working SMART BCS implementation.

Author
MARK VONOO
SMART BCS — Smart Bottle Collection System
Ghana
