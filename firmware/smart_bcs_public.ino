/*
  SMART BCS - Smart Bottle Collection System
  Public Reference Firmware

  Author: MARK VONOO
  Platform: ESP32
  Language: Arduino C++

  This public version demonstrates the core firmware used by SMART BCS:
  RFID user identification, load-cell bottle verification, servo sorting,
  ultrasonic bin-level monitoring, persistent reward storage, LCD feedback,
  Wi-Fi connectivity, SMS receipts, and a four-key user interface.

  SECURITY NOTICE:
  - Wi-Fi credentials are placeholders.
  - SMS API credentials are placeholders.
  - RFID UID and phone number are demonstration values.
  - Replace placeholders locally before compiling for deployment.

  Project repository: SMART-BOTTLE-COLLECTION-SYSTEM
*/

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <SPI.h>
#include <MFRC522.h>
#include <HX711.h>
#include <ESP32Servo.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Preferences.h>
#include <math.h>

// ============================================================
// SMART BOTTLE COLLECTION SYSTEM (SMART BCS)
// PUBLIC REFERENCE FIRMWARE - 4 ESSENTIAL KEYS
//
// USER INPUT:
//   A hold 2 sec = ON/STANDBY, B = Guest, # = Finish, * = Cancel
//   Registered user: scan RFID card -> insert bottles immediately
//
// NO NUMBER/QUANTITY ENTRY is used in this build.
// A session continues bottle-by-bottle until the inactivity timeout.
// Startup object safety: any detected object at power-up is forced to REJECT.
// All other tested hardware remains active.
// ============================================================

// ---------------- PRIVATE CONFIG ----------------
const char* WIFI_SSID = "YOUR_WIFI_NAME";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";
const char* BMS_API_KEY = "PASTE_YOUR_API_KEY_HERE";
const char* BMS_SENDER_ID = "SMART BCS";
const char* BMS_ENDPOINT = "https://api.mnotify.com/api/sms/quick";

// ---------------- USER DATABASE ----------------
// Public repository uses demonstration identity data only.
struct UserAccount {
  String cardUID;
  String userName;
  String phoneNumber;
  int balance;
};

UserAccount userDB[] = {
  {"DEMOUID01", "DemoUser", "233XXXXXXXXX", 0}
};

const int TOTAL_USERS = sizeof(userDB) / sizeof(userDB[0]);

// ---------------- PERMANENT STORAGE ----------------
Preferences preferences;

// ---------------- GPIO ----------------
#define RFID_SS_PIN 5
#define RFID_RST_PIN 4
#define LOADCELL_DOUT_PIN 26
#define LOADCELL_SCK_PIN 27
#define SERVO_PIN 13
#define TRIG_PIN 32
#define ECHO_PIN 33
#define LED_GREEN 25
#define LED_YELLOW 2
#define LED_RED 15
#define BUZZER_PIN 14
#define I2C_SDA_PIN 21
#define I2C_SCL_PIN 22

// 4-key interface - tested with external 10k pull-ups to 3.3V
#define KEY_A_PIN 34
#define KEY_B_PIN 35
#define KEY_HASH_PIN 36
#define KEY_STAR_PIN 39
const unsigned long KEY_DEBOUNCE_MS = 35UL;
const unsigned long A_HOLD_MS = 2000UL;

// ---------------- HARDWARE OBJECTS ----------------
LiquidCrystal_I2C lcdUpper(0x27, 20, 4);
LiquidCrystal_I2C lcdLower(0x26, 16, 2);
MFRC522 rfid(RFID_SS_PIN, RFID_RST_PIN);
HX711 scale;
Servo sortServo;

// ---------------- FINAL TESTED SETTINGS ----------------
const int ANGLE_ACCEPT = 0;
const int ANGLE_STANDBY = 90;
const int ANGLE_REJECT = 140;
const unsigned long SORT_HOLD_TIME_MS = 3500UL;

const float OBJECT_DETECTION_WEIGHT = 5.0f;
const float MIN_BOTTLE_WEIGHT = 12.0f;
const float MAX_BOTTLE_WEIGHT = 30.0f;
float calibration_factor = 420.0f;

const int FULL_BIN_DISTANCE_CM = 20;
const int POINTS_PER_BOTTLE = 10;
// No quantity limit is requested from the user in the no-keypad build.
// The session ends after about 13.3 seconds with no new bottle.
const unsigned long BOTTLE_TIMEOUT_MS = 13333UL;
const unsigned long PLATFORM_CLEAR_TIMEOUT_MS = 8000UL;
const unsigned long WIFI_RETRY_INTERVAL_MS = 15000UL;
const unsigned long SMS_RETRY_INTERVAL_MS = 60000UL;

int currentAngle = ANGLE_STANDBY;
unsigned long lastWiFiAttempt = 0;
unsigned long lastSmsRetry = 0;
bool systemBusy = false;
bool smsPaused = false;
unsigned long lastIdleBinCheck = 0;
bool cachedBinFull = false;

// ---------------- RFID SELF-RECOVERY ----------------
bool rfidReady = false;
unsigned long lastRfidHealthCheck = 0;
unsigned long lastRfidRecoveryAttempt = 0;
const unsigned long RFID_HEALTH_INTERVAL_MS = 1500UL;
const unsigned long RFID_RECOVERY_INTERVAL_MS = 2000UL;
const uint8_t RFID_INIT_ATTEMPTS = 5;

// ---------------- SMS QUEUE ----------------
struct PendingReceipt {
  String phone;
  String name;
  int processed;
  int accepted;
  int rejected;
  int points;
  int balance;
};

const uint8_t MAX_PENDING_SMS = 20;
uint8_t smsHead = 0;
uint8_t smsTail = 0;
uint8_t smsCount = 0;

// ============================================================
// LCD / INDICATORS
// ============================================================
String fit16(String s) { return (s.length() > 16) ? s.substring(0, 16) : s; }
String fit20(String s) { return (s.length() > 20) ? s.substring(0, 20) : s; }

void showUpper(String l1 = "", String l2 = "", String l3 = "", String l4 = "") {
  lcdUpper.clear();
  lcdUpper.setCursor(0, 0); lcdUpper.print(fit20(l1));
  lcdUpper.setCursor(0, 1); lcdUpper.print(fit20(l2));
  lcdUpper.setCursor(0, 2); lcdUpper.print(fit20(l3));
  lcdUpper.setCursor(0, 3); lcdUpper.print(fit20(l4));
}

void showLower(String l1 = "", String l2 = "") {
  lcdLower.clear();
  lcdLower.setCursor(0, 0); lcdLower.print(fit16(l1));
  lcdLower.setCursor(0, 1); lcdLower.print(fit16(l2));
}

void setTrafficLight(bool red, bool yellow, bool green) {
  digitalWrite(LED_RED, red ? HIGH : LOW);
  digitalWrite(LED_YELLOW, yellow ? HIGH : LOW);
  digitalWrite(LED_GREEN, green ? HIGH : LOW);
}

void beepShort() { digitalWrite(BUZZER_PIN, HIGH); delay(80); digitalWrite(BUZZER_PIN, LOW); }
void beepAccept() { digitalWrite(BUZZER_PIN, HIGH); delay(100); digitalWrite(BUZZER_PIN, LOW); delay(80); digitalWrite(BUZZER_PIN, HIGH); delay(150); digitalWrite(BUZZER_PIN, LOW); }
void beepReject() { digitalWrite(BUZZER_PIN, HIGH); delay(600); digitalWrite(BUZZER_PIN, LOW); }
void beepSessionStart() { beepShort(); delay(100); beepShort(); }
void beepSessionComplete() { beepShort(); delay(80); beepShort(); delay(80); digitalWrite(BUZZER_PIN, HIGH); delay(220); digitalWrite(BUZZER_PIN, LOW); }
void beepPowerOn() { beepShort(); delay(80); digitalWrite(BUZZER_PIN, HIGH); delay(220); digitalWrite(BUZZER_PIN, LOW); }
void beepPowerOff() { digitalWrite(BUZZER_PIN, HIGH); delay(220); digitalWrite(BUZZER_PIN, LOW); delay(80); beepShort(); }

// ============================================================
// SERVO
// ============================================================
void moveServoSlow(int targetAngle, int stepDelayMs = 12) {
  if (targetAngle == currentAngle) return;

  if (currentAngle < targetAngle) {
    for (int pos = currentAngle; pos <= targetAngle; pos++) {
      sortServo.write(pos);
      delay(stepDelayMs);
    }
  } else {
    for (int pos = currentAngle; pos >= targetAngle; pos--) {
      sortServo.write(pos);
      delay(stepDelayMs);
    }
  }
  currentAngle = targetAngle;
}

// ============================================================
// ULTRASONIC / BIN FULL
// ============================================================
int getBinDistanceCm() {
  digitalWrite(TRIG_PIN, LOW); delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH); delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  long duration = pulseIn(ECHO_PIN, HIGH, 30000UL);
  if (duration <= 0) return 999;

  int distance = (int)(duration * 0.034f / 2.0f);
  return (distance > 0) ? distance : 999;
}

bool isBinFull() {
  int d = getBinDistanceCm();
  return d > 0 && d <= FULL_BIN_DISTANCE_CM;
}

// ============================================================
// WIFI BACKGROUND CONNECTION
// ============================================================
void startWiFiBackground() {
  WiFi.mode(WIFI_STA);
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  lastWiFiAttempt = millis();
  Serial.println("WiFi started in background.");
}

void manageWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;

  if (millis() - lastWiFiAttempt >= WIFI_RETRY_INTERVAL_MS) {
    lastWiFiAttempt = millis();
    WiFi.disconnect(false, false);
    delay(50);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  }
}

// ============================================================
// PERSISTENT USER BALANCES
// ============================================================
uint32_t hashUid(const String& uid) {
  uint32_t hash = 2166136261UL;
  for (size_t i = 0; i < uid.length(); i++) {
    hash ^= (uint8_t)uid.charAt(i);
    hash *= 16777619UL;
  }
  return hash;
}

String balanceKey(const String& uid) {
  char buf[13];
  snprintf(buf, sizeof(buf), "b%08lX", (unsigned long)hashUid(uid));
  return String(buf);
}

void loadUserBalances() {
  for (int i = 0; i < TOTAL_USERS; i++) {
    String key = balanceKey(userDB[i].cardUID);
    if (preferences.isKey(key.c_str())) {
      userDB[i].balance = preferences.getInt(key.c_str(), userDB[i].balance);
    } else {
      preferences.putInt(key.c_str(), userDB[i].balance);
    }
  }
}

void saveUserBalance(int idx) {
  if (idx < 0 || idx >= TOTAL_USERS) return;
  String key = balanceKey(userDB[idx].cardUID);
  preferences.putInt(key.c_str(), userDB[idx].balance);
}

// ============================================================
// SMS QUEUE IN NVS
// ============================================================
String qKey(uint8_t slot, const char* suffix) {
  String k = "q" + String(slot);
  k += suffix;
  return k;
}

void saveQueueMeta() {
  preferences.putUInt("qHead", smsHead);
  preferences.putUInt("qTail", smsTail);
  preferences.putUInt("qCount", smsCount);
}

void loadQueueMeta() {
  smsHead = (uint8_t)preferences.getUInt("qHead", 0);
  smsTail = (uint8_t)preferences.getUInt("qTail", 0);
  smsCount = (uint8_t)preferences.getUInt("qCount", 0);
  if (smsHead >= MAX_PENDING_SMS) smsHead = 0;
  if (smsTail >= MAX_PENDING_SMS) smsTail = 0;
  if (smsCount > MAX_PENDING_SMS) smsCount = 0;
}

bool enqueueReceipt(const PendingReceipt& r) {
  if (smsCount >= MAX_PENDING_SMS) return false;
  uint8_t s = smsTail;

  preferences.putString(qKey(s, "ph").c_str(), r.phone);
  preferences.putString(qKey(s, "nm").c_str(), r.name);
  preferences.putInt(qKey(s, "pr").c_str(), r.processed);
  preferences.putInt(qKey(s, "ac").c_str(), r.accepted);
  preferences.putInt(qKey(s, "rj").c_str(), r.rejected);
  preferences.putInt(qKey(s, "pt").c_str(), r.points);
  preferences.putInt(qKey(s, "bl").c_str(), r.balance);

  smsTail = (smsTail + 1) % MAX_PENDING_SMS;
  smsCount++;
  saveQueueMeta();
  return true;
}

bool readOldestReceipt(PendingReceipt& r) {
  if (smsCount == 0) return false;
  uint8_t s = smsHead;

  r.phone = preferences.getString(qKey(s, "ph").c_str(), "");
  r.name = preferences.getString(qKey(s, "nm").c_str(), "");
  r.processed = preferences.getInt(qKey(s, "pr").c_str(), 0);
  r.accepted = preferences.getInt(qKey(s, "ac").c_str(), 0);
  r.rejected = preferences.getInt(qKey(s, "rj").c_str(), 0);
  r.points = preferences.getInt(qKey(s, "pt").c_str(), 0);
  r.balance = preferences.getInt(qKey(s, "bl").c_str(), 0);

  return r.phone.length() > 0;
}

void removeQueueSlot(uint8_t s) {
  preferences.remove(qKey(s, "ph").c_str());
  preferences.remove(qKey(s, "nm").c_str());
  preferences.remove(qKey(s, "pr").c_str());
  preferences.remove(qKey(s, "ac").c_str());
  preferences.remove(qKey(s, "rj").c_str());
  preferences.remove(qKey(s, "pt").c_str());
  preferences.remove(qKey(s, "bl").c_str());
}

void popOldestReceipt() {
  if (smsCount == 0) return;
  uint8_t s = smsHead;
  removeQueueSlot(s);
  smsHead = (smsHead + 1) % MAX_PENDING_SMS;
  smsCount--;
  saveQueueMeta();
}

// ============================================================
// BMS SMS
// ============================================================
String jsonEscape(const String& input) {
  String out;
  for (size_t i = 0; i < input.length(); i++) {
    char c = input.charAt(i);
    if (c == '\\') out += "\\\\";
    else if (c == '"') out += "\\\"";
    else if (c == '\n') out += "\\n";
    else if (c == '\r') out += "\\r";
    else if (c == '\t') out += "\\t";
    else if ((uint8_t)c >= 0x20) out += c;
  }
  return out;
}

String buildReceiptMessage(const PendingReceipt& r) {
  String m = "SMART BCS RECEIPT\n";
  m += "Hello " + r.name + ",\n";
  m += "Bottles Processed: " + String(r.processed) + "\n";
  m += "Accepted: " + String(r.accepted) + "\n";
  m += "Rejected: " + String(r.rejected) + "\n";
  m += "Points Earned: +" + String(r.points) + " Pts\n";
  m += "Total Balance: " + String(r.balance) + " Pts\n";
  m += "Thank you for recycling!";
  return m;
}

// Return: 1 = confirmed accepted, 0 = not confirmed, -1 = account/service error
int sendReceiptNow(const PendingReceipt& r) {
  if (WiFi.status() != WL_CONNECTED) return 0;

  String url = String(BMS_ENDPOINT) + "?key=" + String(BMS_API_KEY);
  String payload = "{";
  payload += "\"recipient\":[\"" + jsonEscape(r.phone) + "\"],";
  payload += "\"sender\":\"" + jsonEscape(String(BMS_SENDER_ID)) + "\",";
  payload += "\"message\":\"" + jsonEscape(buildReceiptMessage(r)) + "\",";
  payload += "\"is_schedule\":false,";
  payload += "\"schedule_date\":\"\"";
  payload += "}";

  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(8);

  HTTPClient http;
  http.setTimeout(8000);

  if (!http.begin(client, url)) return 0;
  http.addHeader("Content-Type", "application/json");

  int httpCode = http.POST(payload);
  String response = (httpCode > 0) ? http.getString() : "";

  Serial.println("\n===== BMS SMS RESPONSE =====");
  Serial.print("HTTP Code: "); Serial.println(httpCode);
  Serial.println(response);

  http.end();

  if (httpCode == 419) return -1;

  bool httpOK = httpCode >= 200 && httpCode < 300;
  bool statusOK = response.indexOf("\"status\":\"success\"") >= 0;
  bool codeOK = response.indexOf("\"code\":\"2000\"") >= 0 || response.indexOf("\"code\":2000") >= 0;
  bool noRejected = response.indexOf("\"total_rejected\":0") >= 0;
  bool numberSent = response.indexOf("\"" + r.phone + "\"") >= 0;

  return (httpOK && statusOK && codeOK && noRejected && numberSent) ? 1 : 0;
}

bool trySendOldestQueuedReceipt() {
  if (smsCount == 0 || WiFi.status() != WL_CONNECTED || smsPaused) return false;

  PendingReceipt r;
  if (!readOldestReceipt(r)) {
    popOldestReceipt();
    return false;
  }

  int result = sendReceiptNow(r);

  if (result == 1) {
    popOldestReceipt();
    Serial.println("Receipt confirmed and removed from queue.");
    return true;
  }

  if (result == -1) {
    smsPaused = true;
    preferences.putBool("smsPaused", true);
    Serial.println("SMS service paused due to BMS account/service error.");
  }

  return false;
}

void retryPendingSmsInBackground() {
  if (systemBusy || smsPaused || smsCount == 0 || WiFi.status() != WL_CONNECTED) return;

  if (millis() - lastSmsRetry >= SMS_RETRY_INTERVAL_MS) {
    lastSmsRetry = millis();
    trySendOldestQueuedReceipt();
  }
}

void serviceBackground() {
  manageWiFi();
  retryPendingSmsInBackground();
}

// ============================================================
// LOAD CELL / SAFE START
// ============================================================
float getWeight(int samples = 5) {
  if (!scale.is_ready()) return 0.0f;
  return fabs(scale.get_units(samples));
}

bool platformIsClear() {
  return getWeight(4) < OBJECT_DETECTION_WEIGHT;
}

void safeLoadCellStartup() {
  scale.begin(LOADCELL_DOUT_PIN, LOADCELL_SCK_PIN);
  delay(700);
  scale.set_scale(calibration_factor);

  // Restore the tested empty-platform offset. Do NOT tare automatically
  // at power-up because an object may already be sitting on the platform.
  long storedOffset = preferences.getLong("hxOffset", 61479);
  scale.set_offset(storedOffset);

  if (!preferences.isKey("hxOffset")) {
    preferences.putLong("hxOffset", 61479);
    Serial.println("WARNING: hxOffset missing. Restored tested offset 61479.");
  }

  delay(500);

  float startupWeight = getWeight(10);
  Serial.print("Startup platform weight: ");
  Serial.print(startupWeight, 2);
  Serial.println(" g");

  // STARTUP SAFETY RULE:
  // Any object detected at power-up is automatically REJECTED,
  // regardless of whether its weight would normally fall inside the
  // accepted 12-30 g range. No user session exists and no points are awarded.
  if (startupWeight >= OBJECT_DETECTION_WEIGHT) {
    systemBusy = true;
    setTrafficLight(true, false, false);

    showUpper("STARTUP OBJECT FOUND",
              "Weight: " + String(startupWeight, 1) + " g",
              "AUTO REJECTING...",
              "No points awarded");
    showLower("STARTUP OBJECT", "REJECTING");

    Serial.println("STARTUP OBJECT DETECTED -> FORCED REJECT");
    beepReject();

    moveServoSlow(ANGLE_REJECT, 12);
    delay(SORT_HOLD_TIME_MS);
    moveServoSlow(ANGLE_STANDBY, 12);

    // The machine must not enter normal operation while anything remains
    // on the weighing platform after the rejection movement.
    showUpper("STARTUP REJECT DONE",
              "Waiting for intake",
              "to become clear...",
              "");
    showLower("PLEASE WAIT", "CLEARING INTAKE");

    while (!platformIsClear()) {
      manageWiFi();
      delay(150);
    }

    setTrafficLight(false, true, false);
    showUpper("PLATFORM CLEAR",
              "Startup check passed",
              "SMART BCS READY",
              "");
    showLower("PLATFORM CLEAR", "SYSTEM READY");
    beepShort();
    delay(900);
    systemBusy = false;
  }
}

// ============================================================
// RFID
// ============================================================
int findUserIndex(const String& uid) {
  for (int i = 0; i < TOTAL_USERS; i++) {
    if (userDB[i].cardUID.equalsIgnoreCase(uid)) return i;
  }
  return -1;
}

bool rfidVersionValid(byte version) {
  // Common MFRC522 versions include 0x91 and 0x92.
  // 0x00 and 0xFF normally indicate no usable SPI response.
  return version != 0x00 && version != 0xFF;
}

byte readRfidVersion() {
  return rfid.PCD_ReadRegister(MFRC522::VersionReg);
}

void hardResetRFID() {
  // GPIO4 is connected to RC522 RST.
  pinMode(RFID_RST_PIN, OUTPUT);
  digitalWrite(RFID_RST_PIN, LOW);
  delay(80);
  digitalWrite(RFID_RST_PIN, HIGH);
  delay(120);
}

bool initializeRFIDWithRecovery(bool verbose = true) {
  // Ensure the SPI bus and chip-select line are in a known state.
  pinMode(RFID_SS_PIN, OUTPUT);
  digitalWrite(RFID_SS_PIN, HIGH);

  for (uint8_t attempt = 1; attempt <= RFID_INIT_ATTEMPTS; attempt++) {
    hardResetRFID();

    // Re-start SPI in case the bus was left in a bad state.
    SPI.end();
    delay(20);
    SPI.begin(18, 19, 23, RFID_SS_PIN);
    delay(30);

    rfid.PCD_Init();
    delay(80);
    rfid.PCD_AntennaOn();
    delay(30);

    byte version = readRfidVersion();

    if (verbose) {
      Serial.print("RFID init attempt ");
      Serial.print(attempt);
      Serial.print("/ ");
      Serial.print(RFID_INIT_ATTEMPTS);
      Serial.print(" - VersionReg: 0x");
      if (version < 0x10) Serial.print("0");
      Serial.println(version, HEX);
    }

    if (rfidVersionValid(version)) {
      rfidReady = true;
      lastRfidHealthCheck = millis();
      lastRfidRecoveryAttempt = millis();
      if (verbose) Serial.println("RFID READER READY");
      return true;
    }

    delay(120);
  }

  rfidReady = false;
  lastRfidRecoveryAttempt = millis();
  if (verbose) {
    Serial.println("RFID RECOVERY FAILED - Guest mode remains available.");
    Serial.println("SMART BCS will retry the RC522 automatically.");
  }
  return false;
}

void reinitializeRFID() {
  initializeRFIDWithRecovery(true);
}

void serviceRFIDRecovery() {
  unsigned long now = millis();

  // If the reader is currently marked offline, retry periodically without
  // restarting the ESP32 or blocking Guest Mode for long periods.
  if (!rfidReady) {
    if (now - lastRfidRecoveryAttempt >= RFID_RECOVERY_INTERVAL_MS) {
      Serial.println("RFID offline -> automatic recovery attempt...");
      initializeRFIDWithRecovery(false);
      if (rfidReady) Serial.println("RFID AUTO-RECOVERY SUCCESSFUL");
    }
    return;
  }

  // Lightweight health check while the system is waiting for a user.
  if (now - lastRfidHealthCheck >= RFID_HEALTH_INTERVAL_MS) {
    lastRfidHealthCheck = now;
    byte version = readRfidVersion();
    if (!rfidVersionValid(version)) {
      Serial.println("RFID health check failed -> recovering reader...");
      rfidReady = false;
      lastRfidRecoveryAttempt = 0;
    }
  }
}

String readRFIDCard() {
  if (!rfidReady) return "";

  if (!rfid.PICC_IsNewCardPresent()) return "";

  if (!rfid.PICC_ReadCardSerial()) {
    // A failed serial read can leave the reader in a poor state. Mark it for
    // automatic recovery rather than requiring an ESP32 restart.
    Serial.println("RFID card present but serial read failed.");
    rfid.PICC_HaltA();
    rfid.PCD_StopCrypto1();
    rfidReady = false;
    lastRfidRecoveryAttempt = 0;
    return "";
  }

  String uid = "";
  for (byte i = 0; i < rfid.uid.size; i++) {
    if (rfid.uid.uidByte[i] < 0x10) uid += "0";
    uid += String(rfid.uid.uidByte[i], HEX);
  }

  uid.toUpperCase();
  return uid;
}

// ============================================================
// 4-KEY USER INTERFACE
// A = hold 2 sec ON/STANDBY, B = Guest, # = Finish, * = Cancel
// ============================================================
bool systemStandby = true;
bool requestStandby = false;

// Non-blocking edge detection for B, # and *.
// This is intentionally based on the standalone 4-key test that passed.
bool bPressedEvent() {
  static bool latched = false;
  bool down = (digitalRead(KEY_B_PIN) == LOW);

  if (!down) {
    latched = false;
    return false;
  }

  if (latched) return false;

  delay(KEY_DEBOUNCE_MS);
  if (digitalRead(KEY_B_PIN) == LOW) {
    latched = true;
    Serial.println(">>> KEY B DETECTED -> GUEST MODE");
    return true;
  }

  return false;
}

bool hashPressedEvent() {
  static bool previous = HIGH;
  bool now = digitalRead(KEY_HASH_PIN);
  bool event = false;

  if (now == LOW && previous == HIGH) {
    delay(KEY_DEBOUNCE_MS);
    if (digitalRead(KEY_HASH_PIN) == LOW) {
      event = true;
      Serial.println("KEY # DETECTED -> FINISH SESSION");
    }
  }

  previous = digitalRead(KEY_HASH_PIN);
  return event;
}

bool starPressedEvent() {
  static bool previous = HIGH;
  bool now = digitalRead(KEY_STAR_PIN);
  bool event = false;

  if (now == LOW && previous == HIGH) {
    delay(KEY_DEBOUNCE_MS);
    if (digitalRead(KEY_STAR_PIN) == LOW) {
      event = true;
      Serial.println("KEY * DETECTED -> CANCEL/BACK");
    }
  }

  previous = digitalRead(KEY_STAR_PIN);
  return event;
}

bool aHeldTwoSeconds() {
  if (digitalRead(KEY_A_PIN) != LOW) return false;
  unsigned long t = millis();
  while (digitalRead(KEY_A_PIN) == LOW) {
    manageWiFi();
    if (millis() - t >= A_HOLD_MS) {
      while (digitalRead(KEY_A_PIN) == LOW) delay(10);
      return true;
    }
    delay(10);
  }
  return false;
}

void showStandbyScreen() {
  moveServoSlow(ANGLE_STANDBY, 10);
  setTrafficLight(false, true, false);
  showUpper("SMART BCS - STANDBY", "Hold A for 2 seconds", "to activate system", "");
  showLower("SYSTEM STANDBY", "HOLD A TO START");
}

void showReadyScreen() {
  setTrafficLight(false, false, true);
  showUpper("SMART BCS - READY", "Scan RFID card", "or press B for Guest", "Bin: AVAILABLE");
  showLower("EMPTY BOTTLE", "CAP REMOVED");
}

// Return: 1=object, 0=timeout, -1=bin full, -2=# finish, -3=* cancel, -4=A standby
int waitForObject(bool allowCancel) {
  unsigned long start = millis();
  while (millis() - start < BOTTLE_TIMEOUT_MS) {
    manageWiFi();
    if (aHeldTwoSeconds()) return -4;
    if (hashPressedEvent()) return -2;
    if (allowCancel && starPressedEvent()) return -3;
    if (isBinFull()) return -1;
    if (getWeight(3) >= OBJECT_DETECTION_WEIGHT) return 1;
    delay(80);
  }
  return 0;
}

bool waitForPlatformClear(unsigned long timeoutMs) {
  unsigned long start = millis();
  while (millis() - start < timeoutMs) {
    manageWiFi();
    if (platformIsClear()) return true;
    delay(100);
  }
  return false;
}

void completeRegisteredSession(int userIndex, int processed, int accepted, int rejected, int sessionPoints) {
  userDB[userIndex].balance += sessionPoints;
  saveUserBalance(userIndex);
  int finalBalance = userDB[userIndex].balance;

  PendingReceipt r;
  r.phone = userDB[userIndex].phoneNumber; r.name = userDB[userIndex].userName;
  r.processed = processed; r.accepted = accepted; r.rejected = rejected;
  r.points = sessionPoints; r.balance = finalBalance;
  bool queued = enqueueReceipt(r);

  showUpper("SESSION COMPLETE", "Bottles Accepted: " + String(accepted), "Points Earned: +" + String(sessionPoints), "REMOVE YOUR CARD");
  showLower("REMOVE CARD", "THANK YOU!");
  beepSessionComplete();

  Serial.println("\n===== SESSION COMPLETE =====");
  Serial.print("User: "); Serial.println(userDB[userIndex].userName);
  Serial.print("Processed: "); Serial.println(processed);
  Serial.print("Accepted: "); Serial.println(accepted);
  Serial.print("Rejected: "); Serial.println(rejected);
  Serial.print("Points earned: "); Serial.println(sessionPoints);
  Serial.print("Stored total (SMS only): "); Serial.println(finalBalance);

  if (queued && WiFi.status() == WL_CONNECTED && !smsPaused) {
    bool sent = trySendOldestQueuedReceipt();
    Serial.println(sent ? "SMS RECEIPT SENT!" : "SMS queued for retry.");
  } else if (!queued) Serial.println("WARNING: SMS queue full; points are still saved.");
  delay(2500);
}

void processSession(bool isGuest, int userIndex) {
  systemBusy = true;
  requestStandby = false;
  int processed = 0, accepted = 0, rejected = 0, sessionPoints = 0;
  bool firstBottle = true;

  while (true) {
    if (isBinFull()) {
      setTrafficLight(true, false, false); moveServoSlow(ANGLE_STANDBY, 10);
      showUpper("BIN FULL!", "SESSION ENDING", "Points are safe", "");
      showLower("PLEASE EMPTY BIN", "POINTS ARE SAFE"); beepReject(); delay(1000); break;
    }

    setTrafficLight(false, true, false);
    showUpper("INSERT BOTTLE", "Empty bottle only", "Accepted: 12 - 30 g", "# Finish   * Cancel");
    if (isGuest) showLower("GUEST MODE", "INSERT BOTTLE");
    else showLower("SESSION POINTS", "+" + String(sessionPoints) + " PTS");

    int detection = waitForObject(firstBottle);
    if (detection == -4) { requestStandby = true; break; }
    if (detection == -2) { Serial.println("# FINISH pressed"); break; }
    if (detection == -3) {
      showUpper("SESSION CANCELLED", "No bottle processed", "Returning to ready", "");
      showLower("CANCELLED", "PLEASE WAIT"); beepShort(); delay(1000); break;
    }
    if (detection == -1) { showUpper("BIN FULL!", "SESSION ENDING"); beepReject(); break; }
    if (detection == 0) {
      showUpper("SESSION TIMEOUT", "No new bottle", "Session ending", "");
      showLower("NO NEW BOTTLE", "SESSION ENDING"); beepShort(); delay(1200); break;
    }

    showUpper("WEIGHING BOTTLE", "Please wait...", "", "");
    showLower("WEIGHING...", "PLEASE WAIT"); delay(1200);
    float measuredWeight = getWeight(10);
    Serial.print("Measured weight: "); Serial.print(measuredWeight, 2); Serial.println(" g");

    if (measuredWeight < OBJECT_DETECTION_WEIGHT) {
      showUpper("OBJECT REMOVED", "Insert bottle again", "", ""); delay(700); continue;
    }

    firstBottle = false;

    if (measuredWeight < MIN_BOTTLE_WEIGHT) {
      rejected++; processed++; setTrafficLight(true, false, false); beepReject();
      showUpper("INVALID ITEM!", "Weight: " + String(measuredWeight,1) + " g", "Allowed: 12 - 30 g", "SESSION ENDING");
      showLower("NOT ACCEPTED", "REMOVE ITEM");
      moveServoSlow(ANGLE_REJECT, 12); delay(SORT_HOLD_TIME_MS); moveServoSlow(ANGLE_STANDBY, 12); break;
    }

    if (measuredWeight <= MAX_BOTTLE_WEIGHT) {
      processed++; accepted++; if (!isGuest) sessionPoints += POINTS_PER_BOTTLE;
      setTrafficLight(false, false, true); beepAccept();
      if (isGuest) showUpper("BOTTLE ACCEPTED!", "Weight: " + String(measuredWeight,1) + " g", "Thank you!", "Insert next bottle");
      else showUpper("BOTTLE ACCEPTED!", "Weight: " + String(measuredWeight,1) + " g", "Reward: +10 points", "Session Points: " + String(sessionPoints));
      showLower("ACCEPTED!", isGuest ? "THANK YOU" : "+10 POINTS");
      moveServoSlow(ANGLE_ACCEPT, 12); delay(SORT_HOLD_TIME_MS); moveServoSlow(ANGLE_STANDBY, 12);
    } else {
      processed++; rejected++; setTrafficLight(true, false, false); beepReject();
      showUpper("BOTTLE REJECTED!", "Weight: " + String(measuredWeight,1) + " g", "EMPTY YOUR BOTTLE", "THEN TRY AGAIN");
      showLower("EMPTY BOTTLE", "TRY AGAIN");
      moveServoSlow(ANGLE_REJECT, 12); delay(SORT_HOLD_TIME_MS); moveServoSlow(ANGLE_STANDBY, 12);
    }

    if (!waitForPlatformClear(PLATFORM_CLEAR_TIMEOUT_MS)) {
      setTrafficLight(true, false, false); showUpper("CLEAR PLATFORM", "Object still detected", "Session ending", "");
      showLower("OBJECT REMAINS", "PLEASE REMOVE"); beepReject(); delay(1000); break;
    }
  }

  moveServoSlow(ANGLE_STANDBY, 10);
  if (!isGuest && userIndex >= 0) completeRegisteredSession(userIndex, processed, accepted, rejected, sessionPoints);
  else {
    showUpper("SESSION COMPLETE", "Bottles Accepted: " + String(accepted), "THANK YOU FOR", "RECYCLING!");
    showLower("THANK YOU!", "RECYCLE AGAIN"); beepSessionComplete(); delay(1800);
  }
  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();
  if (rfidReady) rfid.PCD_AntennaOn();
  systemBusy = false;
}

// ============================================================
// FULL BIN LOCKOUT
// ============================================================
void handleBinFullLockout() {
  systemBusy = true;

  moveServoSlow(ANGLE_STANDBY, 10);
  scale.power_down();

  setTrafficLight(true, false, false);
  showUpper("BIN IS FULL!", "SERVICE NEEDED");
  showLower("PLEASE EMPTY BIN", "SYSTEM LOCKED");
  beepReject();

  while (isBinFull()) {
    manageWiFi();
    delay(250);
  }

  scale.power_up();
  delay(500);
  scale.set_scale(calibration_factor);
  scale.set_offset(preferences.getLong("hxOffset", 61479));

  // Refresh RFID after a potentially long service lockout.
  initializeRFIDWithRecovery(false);

  showUpper("BIN AVAILABLE", "SYSTEM RESUMING");
  showLower("PLEASE WAIT", "");
  setTrafficLight(false, true, false);
  delay(800);

  systemBusy = false;
}

// ============================================================
// SETUP
// ============================================================
void setup() {
  Serial.begin(115200);

  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_YELLOW, OUTPUT);
  pinMode(LED_RED, OUTPUT);

  pinMode(KEY_A_PIN, INPUT);
  pinMode(KEY_B_PIN, INPUT);
  pinMode(KEY_HASH_PIN, INPUT);
  pinMode(KEY_STAR_PIN, INPUT);

  // The four external 10k pull-ups should make all keys HIGH at rest.
  Serial.print("KEY IDLE CHECK  A="); Serial.print(digitalRead(KEY_A_PIN));
  Serial.print(" B="); Serial.print(digitalRead(KEY_B_PIN));
  Serial.print(" #="); Serial.print(digitalRead(KEY_HASH_PIN));
  Serial.print(" *="); Serial.println(digitalRead(KEY_STAR_PIN));

  setTrafficLight(false, true, false);

  preferences.begin("smartbcs", false);
  loadUserBalances();
  loadQueueMeta();
  smsPaused = preferences.getBool("smsPaused", false);

  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  lcdUpper.init(); lcdUpper.backlight();
  lcdLower.init(); lcdLower.backlight();

  showUpper("SMART BCS", "INITIALIZING", "Please wait...", "");
  showLower("SYSTEM STARTUP", "PLEASE WAIT");

  startWiFiBackground();

  ESP32PWM::allocateTimer(0);
  sortServo.setPeriodHertz(50);
  sortServo.attach(SERVO_PIN, 500, 2400);
  sortServo.write(ANGLE_STANDBY);
  currentAngle = ANGLE_STANDBY;

  pinMode(RFID_SS_PIN, OUTPUT);
  digitalWrite(RFID_SS_PIN, HIGH);
  pinMode(RFID_RST_PIN, OUTPUT);
  digitalWrite(RFID_RST_PIN, HIGH);

  SPI.begin(18, 19, 23, RFID_SS_PIN);
  initializeRFIDWithRecovery(true);

  safeLoadCellStartup();

  if (isBinFull()) handleBinFullLockout();

  Serial.println("\n====================================");
  Serial.println(" SMART BCS - PUBLIC BUILD");
  Serial.println("====================================");
  Serial.println("A hold 2 sec = ACTIVE / STANDBY");
  Serial.println("B = Guest mode");
  Serial.println("# = Finish session");
  Serial.println("* = Cancel before first bottle");
  Serial.println("RFID = registered-user session");
  Serial.println("RC522 self-recovery = ENABLED");
  Serial.println("====================================");

  systemStandby = true;
  showStandbyScreen();
}

// ============================================================
// MAIN LOOP
// ============================================================
void loop() {
  // ----------------------------------------------------------
  // PRIORITY 1: A ON/STANDBY. Nothing else may block this.
  // ----------------------------------------------------------
  if (aHeldTwoSeconds()) {
    systemStandby = !systemStandby;

    if (systemStandby) {
      beepPowerOff();
      showStandbyScreen();
    } else {
      // Re-initialize RC522 every time the machine becomes active.
      // This allows the system to recover even if the reader lost
      // SPI state while the machine was sitting idle.
      initializeRFIDWithRecovery(true);
      beepPowerOn();
      showReadyScreen();
      if (rfidReady) Serial.println("SYSTEM ACTIVE - RFID READY / B GUEST AVAILABLE");
      else Serial.println("SYSTEM ACTIVE - RFID RECOVERING / B GUEST AVAILABLE");
    }

    delay(150);
    return;
  }

  // In standby, user operation is disabled. Background Wi-Fi/SMS work
  // is allowed here because no customer is waiting to start a session.
  if (systemStandby) {
    serviceBackground();
    delay(30);
    return;
  }

  // ----------------------------------------------------------
  // PRIORITY 2: B GUEST KEY.
  // This is checked BEFORE ultrasonic, Wi-Fi retry, SMS or RFID work.
  // ----------------------------------------------------------
  if (bPressedEvent()) {
    beepSessionStart();
    showUpper("GUEST MODE", "No RFID required", "Insert empty bottle", "# Finish   * Cancel");
    showLower("GUEST MODE", "EMPTY BOTTLES");
    delay(350);

    processSession(true, -1);

    if (requestStandby) {
      systemStandby = true;
      beepPowerOff();
      showStandbyScreen();
    } else {
      showReadyScreen();
    }
    return;
  }

  // ----------------------------------------------------------
  // PRIORITY 3: RFID.
  // Poll every loop before any slow background operation.
  // The RC522 is health-checked and self-recovers without ESP32 restart.
  // ----------------------------------------------------------
  serviceRFIDRecovery();
  String uid = readRFIDCard();
  if (uid.length() > 0) {
    Serial.println(">>> RFID CARD DETECTED");
    Serial.print("UID: ");
    Serial.println(uid);
    beepShort();

    int userIndex = findUserIndex(uid);
    bool isGuest = (userIndex == -1);

    if (isGuest) {
      showUpper("CARD NOT REGISTERED", "Starting Guest Mode", "No reward points", "Insert empty bottle");
      showLower("GUEST MODE", "NO POINTS");
      delay(700);
    } else {
      // Stored total balance stays private; LCD shows session points only.
      showUpper("WELCOME " + userDB[userIndex].userName,
                "Card verified",
                "Insert empty bottle",
                "Session Points: 0");
      showLower("CARD VERIFIED", "INSERT BOTTLE");
      Serial.print("Registered user: ");
      Serial.println(userDB[userIndex].userName);
      delay(700);
    }

    // Check bin only after a user has actually requested a session.
    if (isBinFull()) {
      showUpper("BIN FULL!", "SESSION CANCELLED", "Please empty bin", "");
      showLower("BIN FULL", "SERVICE NEEDED");
      beepReject();
      rfid.PICC_HaltA();
      rfid.PCD_StopCrypto1();
      handleBinFullLockout();
      showReadyScreen();
      return;
    }

    beepSessionStart();
    processSession(isGuest, userIndex);

    if (requestStandby) {
      systemStandby = true;
      beepPowerOff();
      showStandbyScreen();
    } else {
      showReadyScreen();
    }
    return;
  }

  // ----------------------------------------------------------
  // PRIORITY 4: Lightweight idle maintenance only.
  // Never run a blocking SMS HTTP retry while waiting for a user.
  // ----------------------------------------------------------
  manageWiFi();

  // Check bin at most once per second while READY instead of on every loop.
  if (millis() - lastIdleBinCheck >= 1000UL) {
    lastIdleBinCheck = millis();
    cachedBinFull = isBinFull();

    if (cachedBinFull) {
      handleBinFullLockout();
      showReadyScreen();
      return;
    }
  }

  delay(15);
}
