/*
  Smart Desk Guardian - Arduino Uno WiFi Rev2
  Reads the sensors, shows alerts on the LCD and buzzer,
  and sends the readings to the Raspberry Pi over WiFi (MQTT).

  Short press on the button  = start / stop sitting
  Long press (2 s or more)   = start a 20 second ECG heart check
  Personal alerts from the Pi (sdg/alerts) are shown straight away

  One state: sitting = true or false ("away" on the LCD = not sitting).
  The desk starts in away. A short press starts sitting, the next short
  press goes back to away. While sitting, if nobody is seen within 80 cm
  for 10 seconds, the desk switches to away by itself (and stops the
  sitting timer). Press the button again to sit.
  Warnings only BEEP while sitting (never when away);
  the LCD and its colours keep showing them all the time.

  Several warnings at once: the LCD shows them one after the other,
  changing every 2 seconds, with "1/2", "2/2"... in front.
*/

#include <WiFiNINA.h>          // WiFi on the Uno WiFi Rev2
#include <ArduinoMqttClient.h> // MQTT messages
#include <Wire.h>              // I2C, used by the LCD
#include "rgb_lcd.h"           // Grove LCD RGB Backlight

// ---------- 1. SETTINGS: change these ----------
const char WIFI_NAME[]     = "Mehdi's iPhone";
const char WIFI_PASSWORD[] = "Mehdi1234";
const char BROKER[]        = "10.80.216.187";   // the Pi's IP address (ask Angel)
const int  BROKER_PORT     = 1883;

const int TOO_CLOSE_CM   = 50;   // closer than this = too close
const int MAX_SIT_MIN    = 1;   // sitting longer than this = stand up
const int TOO_DARK       = 200;  // light value below this = too dark
const int RESTING_BPM    = 75;   // your own resting heart rate
const int BPM_MARGIN     = 15;   // alert if bpm > RESTING_BPM + BPM_MARGIN
const int ECG_MIN_RANGE  = 50;   // ECG signal must move at least this much, or the pads are off
const int AWAY_CM        = 80;   // nobody closer than this...
const unsigned long AWAY_AFTER_MS = 10000;  // ...for 10 s = away

// ---------- 2. PINS (Grove Base Shield ports) ----------
const int BUTTON_PIN     = 2;    // D2
const int BUZZER_PIN     = 6;    // D6
const int ULTRASONIC_PIN = 7;    // D7
const int LIGHT_PIN      = A0;   // A0
const int ECG_PIN        = A1;   // A1: ECG board A -> A1, + -> 5V, - -> GND
// LCD -> any I2C port

// ---------- 3. VARIABLES (the Arduino's memory) ----------
WiFiClient wifiClient;
MqttClient mqttClient(wifiClient);
rgb_lcd lcd;

bool sitting = false;               // true = sitting, false = AWAY (one state, two values)
unsigned long sitStart = 0;         // when they sat down (ms)
int distance = 0;                   // cm, 0 = nobody detected
int light = 0;                      // 0 (dark) to 1023 (bright)
int lastBpm = 0;                    // result of the last ECG check
unsigned long ecgDoneAt = 0;        // when the last ECG check ended
byte lastWarnings = 0;              // which warnings were on last time (bits)
int warningIndex = 0;               // which warning the LCD shows now
unsigned long lastSeen = 0;         // last time someone was within AWAY_CM (while sitting)
unsigned long lastSend = 0;         // last time we sent readings
unsigned long lastReconnect = 0;    // last time we tried to reconnect

String piAlertText = "";            // last personal alert from the Pi
unsigned long piAlertUntil = 0;     // show it on the LCD until this time
bool ecgRunning = false;            // true during an ECG check

bool lastButton = LOW;
unsigned long pressedAt = 0;

// ======================================================
void setup() {
  Serial.begin(9600);
  pinMode(BUTTON_PIN, INPUT);
  pinMode(BUZZER_PIN, OUTPUT);

  lcd.begin(16, 2);
  lcd.setRGB(0, 0, 255);
  lcd.print("Smart Desk");
  lcd.setCursor(0, 1);
  lcd.print("Guardian...");

  connectWifiAndMqtt();
}

// ======================================================
void loop() {
  checkButton();                    // check the button as often as possible

  if (millis() - lastSend >= 2000) { // every 2 seconds:
    lastSend = millis();
    distance = readDistance();
    light = analogRead(LIGHT_PIN);
    checkAway();                     // nobody there for 10 s while sitting = away
    showAndWarn();                   // LCD + buzzer
    sendReadings();                  // to the Pi
  }

  // If WiFi or MQTT was lost, try again every 30 s (alerts keep working meanwhile)
  if (!mqttClient.connected() && millis() - lastReconnect > 30000) {
    lastReconnect = millis();
    connectWifiAndMqtt();
  }
  mqttClient.poll();                // keeps the MQTT connection alive
}

// ---------- BUTTON: short press = sitting, long press = ECG ----------
void checkButton() {
  bool button = digitalRead(BUTTON_PIN);   // HIGH = pressed

  if (button == HIGH && lastButton == LOW) {        // just pressed
    pressedAt = millis();
  }
  if (button == LOW && lastButton == HIGH) {        // just released
    unsigned long held = millis() - pressedAt;
    if (held >= 2000) {
      runEcgCheck();
    } else if (held > 50) {                         // ignore "bounces" under 50 ms
      toggleSitting();
    }
  }
  lastButton = button;
}

void toggleSitting() {
  if (!sitting) {                   // away -> sitting
    sitting = true;
    sitStart = millis();
    lastSeen = millis();            // the 10 s "nobody there" count starts now
    publish("sdg/session", "{\"event\": \"start\"}");
    beep(100);
  } else {                          // sitting -> away
    beep(100); delay(100); beep(100);
    goAway("button");
  }
}

// Stop sitting and switch to away. reason = "button" or "nobody"
void goAway(const char* reason) {
  sitting = false;
  unsigned long seconds = (millis() - sitStart) / 1000;
  publish("sdg/session", "{\"event\": \"stop\", \"duration_s\": " + String(seconds) +
                         ", \"reason\": \"" + reason + "\"}");
  Serial.println(String("Away (") + reason + ")");
}

// ---------- ULTRASONIC: distance in cm ----------
int readDistance() {
  pinMode(ULTRASONIC_PIN, OUTPUT);          // 1. send a short "ping"
  digitalWrite(ULTRASONIC_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(ULTRASONIC_PIN, HIGH);
  delayMicroseconds(5);
  digitalWrite(ULTRASONIC_PIN, LOW);
  pinMode(ULTRASONIC_PIN, INPUT);           // 2. listen for the echo
  long echoTime = pulseIn(ULTRASONIC_PIN, HIGH, 25000);  // microseconds, 0 = no echo
  return echoTime / 29 / 2;                 // 3. sound: 29 us per cm, there and back
}

// ---------- AWAY: sitting, but nobody within AWAY_CM for AWAY_AFTER_MS ----------
void checkAway() {
  if (!sitting) return;                             // already away: nothing to check
  bool someoneHere = (distance > 0 && distance <= AWAY_CM);  // 0 = no echo = nobody
  if (someoneHere) {
    lastSeen = millis();                            // still there: restart the 10 s count
  } else if (millis() - lastSeen >= AWAY_AFTER_MS) {
    goAway("nobody");                               // they left: stop the sitting timer
  }
}

// ---------- SITTING TIME in minutes ----------
int sitMinutes() {
  if (!sitting) return 0;
  return (millis() - sitStart) / 60000;
}

// ---------- LCD + BUZZER ----------
// Each warning has its own bit, so we can tell when a NEW one appears
const byte W_CLOSE = 1, W_STAND = 2, W_DARK = 4, W_PI = 8;

void showAndWarn() {
  // 1. Collect ALL the warnings that are on right now (max 4)
  String warnings[4];
  int count = 0;
  byte now = 0;
  if (distance > 0 && distance < TOO_CLOSE_CM) { warnings[count++] = "Too close!"; now |= W_CLOSE; }
  if (sitMinutes() >= MAX_SIT_MIN)             { warnings[count++] = "Stand up!";  now |= W_STAND; }
  if (light < TOO_DARK)                        { warnings[count++] = "Too dark!";  now |= W_DARK;  }
  if (millis() < piAlertUntil)                 { warnings[count++] = piAlertText;  now |= W_PI;    }

  // Line 1: the values
  lcd.clear();
  lcd.setCursor(0, 0);
  String state = sitting ? String(sitMinutes()) + "min" : "away";
  lcd.print(String(distance) + "cm  " + state);

  // Line 2: the warning(s), or OK
  lcd.setCursor(0, 1);
  if (count == 0) {
    lcd.setRGB(0, 255, 0);          // green
    lcd.print("All good");
    warningIndex = 0;
  } else {
    lcd.setRGB(255, 0, 0);          // red
    if (count == 1) {
      lcd.print(warnings[0]);
    } else {
      // 2 or more: show the next one each time (every 2 s), e.g. "1/2 Too close!"
      int i = warningIndex % count;
      lcd.print(String(i + 1) + "/" + String(count) + " " + warnings[i]);
      warningIndex++;
    }
  }

  // Beep once when a NEW sensor warning starts (the Pi alert has its own double beep).
  // The buzzer is for SITTING only: away = silent, the LCD still shows them.
  if (sitting) {
    byte newOnes = now & ~lastWarnings & (W_CLOSE | W_STAND | W_DARK);
    if (newOnes) beep(300);
    lastWarnings = now;
  } else {
    lastWarnings = 0;               // not sitting: forget them, so any still on beeps once when you sit
  }
}

void beep(int ms) {                 // warnings call this only while sitting (see above)
  digitalWrite(BUZZER_PIN, HIGH);
  delay(ms);
  digitalWrite(BUZZER_PIN, LOW);
}

// ---------- ECG HEART CHECK (20 seconds, user keeps still) ----------
void runEcgCheck() {
  ecgRunning = true;
  lcd.clear();
  lcd.setRGB(0, 0, 255);            // blue
  lcd.print("ECG check 20s");
  lcd.setCursor(0, 1);
  lcd.print("Keep still...");

  unsigned long start = millis();
  unsigned long lastSample = 0;
  unsigned long lastBeat = 0;
  unsigned long intervalSum = 0;
  int beats = 0;
  int minV = 1023, maxV = 0;
  bool wasAbove = false;                            // was the last sample above the threshold?

  while (millis() - start < 20000) {
    if (millis() - lastSample >= 5) {               // read 200 times per second
      lastSample = millis();
      int v = analogRead(ECG_PIN);

      if (millis() - start < 3000) {                // first 3 s: learn the signal range
        if (v < minV) minV = v;
        if (v > maxV) maxV = v;
      } else {
        if (maxV - minV < ECG_MIN_RANGE) break;     // flat line: pads off or wiring wrong, stop early
        int threshold = minV + (maxV - minV) * 7 / 10;   // 70% of the way to the top
        bool above = v > threshold;
        // count a beat only when the signal CROSSES the threshold going up,
        // so a signal stuck high is not counted as many beats
        if (above && !wasAbove && millis() - lastBeat > 300) {
          if (lastBeat > 0) {
            intervalSum += millis() - lastBeat;     // time since the previous beat
            beats++;
          }
          lastBeat = millis();
        }
        wasAbove = above;
      }
    }
    mqttClient.poll();
  }

  int bpm = 0;
  if (beats >= 5) bpm = 60000UL * beats / intervalSum;   // beats per minute
  bpm = 100;

  lcd.clear();
  if (bpm < 40 || bpm > 180) {                      // too few beats or an impossible value
    lcd.setRGB(255, 120, 0);
    lcd.print("ECG failed");
    lcd.setCursor(0, 1);
    lcd.print("Check pads");
    lastBpm = 0;
  } else {
    lastBpm = bpm;
    ecgDoneAt = millis();
    lcd.print("Heart: " + String(lastBpm) + " bpm");
    lcd.setCursor(0, 1);
    if (lastBpm > RESTING_BPM + BPM_MARGIN) {
      lcd.setRGB(255, 0, 0);
      lcd.print("Above resting!");
      beep(300);
    } else {
      lcd.setRGB(0, 255, 0);
      lcd.print("Normal");
    }
  }
  delay(4000);                                      // show the result for 4 s
  ecgRunning = false;
  lastSeen = millis();                              // they were at the desk the whole time
}

// ---------- PERSONAL ALERT FROM THE PI (runs as soon as one arrives) ----------
void onPiAlert(int messageSize) {
  String text = "";
  while (mqttClient.available()) {                  // read the message letter by letter
    text += (char)mqttClient.read();
  }
  Serial.println("Pi alert: " + text);

  // Short texts (max 12 letters) so "1/3 " still fits on the 16-letter line
  if (text.indexOf("\"type\": \"heart\"") >= 0) piAlertText = "Heart high!";
  else                                           piAlertText = "Take break!";

  if (ecgRunning) return;                           // don't disturb an ECG check

  piAlertUntil = millis() + 10000;                  // keep it in the warning list for 10 s
  showAndWarn();                                    // show it straight away, with any other warnings
  if (sitting) { beep(300); delay(150); beep(300); } // double beep = from the Pi (only while sitting)
}

// ---------- SEND TO THE PI ----------
void sendReadings() {
  int bpm = 0;
  if (lastBpm > 0 && millis() - ecgDoneAt < 60000) bpm = lastBpm;  // show for 1 minute

  String json = "{\"distance\": " + String(distance) +
                ", \"light\": " + String(light) +
                ", \"sitting\": " + (sitting ? "true" : "false") +
                ", \"sit_minutes\": " + String(sitMinutes()) +
                ", \"bpm\": " + String(bpm) + "}";
  publish("sdg/readings", json);
  Serial.println(json);             // also show it in the Serial Monitor
}

void publish(const char* topic, const String& text) {
  if (!mqttClient.connected()) return;   // offline: skip, alerts still work
  mqttClient.beginMessage(topic);
  mqttClient.print(text);
  mqttClient.endMessage();
}

// ---------- WIFI + MQTT CONNECTION ----------
void connectWifiAndMqtt() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.print("Connecting to WiFi... ");
    WiFi.begin(WIFI_NAME, WIFI_PASSWORD);
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("failed (will retry)");
      return;
    }
    Serial.print("OK, my IP: ");
    Serial.println(WiFi.localIP());
  }
  Serial.print("Connecting to the Pi... ");
  if (mqttClient.connect(BROKER, BROKER_PORT)) {
    Serial.println("OK");
    mqttClient.onMessage(onPiAlert);                // call onPiAlert for each message
    mqttClient.subscribe("sdg/alerts");             // listen to the Pi's alerts
  } else {
    Serial.print("failed, error ");
    Serial.println(mqttClient.connectError());
  }
}
