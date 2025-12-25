#include <WiFi.h>
#include <PubSubClient.h>
#include <ESP32Servo.h>

/* ================= WIFI & MQTT ================= */
const char* ssid = "uwu";
const char* password = "12345678";

const char* mqtt_server = "broker.hivemq.com";
const int mqtt_port = 1883;

/* ================= HC-SR04 ================= */
#define NUM_SLOTS 6

int trigPins[NUM_SLOTS] = {4, 17, 18, 22, 25, 27};
int echoPins[NUM_SLOTS] = {16, 5, 21, 23, 26, 14};

#define OCCUPIED_THRESHOLD_CM 10
#define SCAN_INTERVAL 1000
#define BETWEEN_SENSOR_MS 60

unsigned long lastScan = 0;

/* ================= SERVO & MH SENSOR ================= */
#define MH_IN_PIN   32
#define MH_OUT_PIN  34   // GPIO34 chỉ input → cần pull-up ngoài

#define SERVO_PIN   19

#define SERVO_CLOSE 0
#define SERVO_OPEN  90

#define AUTO_CLOSE_DELAY 5000  // 5 giây

Servo servo;

/* ================= MQTT ================= */
WiFiClient espClient;
PubSubClient client(espClient);

/* ================= FUNCTIONS ================= */

void setupWiFi() {
  Serial.print("Connecting WiFi");
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWiFi connected");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());
}

void reconnectMQTT() {
  while (!client.connected()) {
    String cid = "ESP32-PARKING-" + String(random(0xffff));
    Serial.print("Connecting MQTT...");
    if (client.connect(cid.c_str())) {
      Serial.println("OK");
    } else {
      Serial.println("FAILED, retry...");
      delay(3000);
    }
  }
}

long measureDistanceCM(int trig, int echo) {
  digitalWrite(trig, LOW);
  delayMicroseconds(2);
  digitalWrite(trig, HIGH);
  delayMicroseconds(10);
  digitalWrite(trig, LOW);

  unsigned long duration = pulseIn(echo, HIGH, 30000);
  if (duration == 0) return -1;

  return duration / 58;
}

void publishSlot(int slotIndex, long distance_cm) {
  char topic[64];
  snprintf(topic, sizeof(topic), "parking/slot/%d", slotIndex + 1);

  bool occupied = (distance_cm > 0 && distance_cm < OCCUPIED_THRESHOLD_CM);
  unsigned long ts = millis();

  char payload[128];
  if (distance_cm < 0) {
    snprintf(payload, sizeof(payload),
      "{\"slot\":%d,\"distance_cm\":null,\"occupied\":%s,\"ts\":%lu}",
      slotIndex + 1, occupied ? "true" : "false", ts);
  } else {
    snprintf(payload, sizeof(payload),
      "{\"slot\":%d,\"distance_cm\":%ld,\"occupied\":%s,\"ts\":%lu}",
      slotIndex + 1, distance_cm, occupied ? "true" : "false", ts);
  }

  client.publish(topic, payload, true);
}

/* ================= SETUP ================= */

void setup() {
  Serial.begin(115200);
  delay(200);

  randomSeed(analogRead(0));

  // HC-SR04
  for (int i = 0; i < NUM_SLOTS; i++) {
    pinMode(trigPins[i], OUTPUT);
    pinMode(echoPins[i], INPUT);
    digitalWrite(trigPins[i], LOW);
  }

  // MH sensors
  pinMode(MH_IN_PIN, INPUT);
  pinMode(MH_OUT_PIN, INPUT);

  // Servo
  servo.attach(SERVO_PIN, 500, 2400);
  servo.write(SERVO_CLOSE);   // đóng mặc định

  setupWiFi();
  client.setServer(mqtt_server, mqtt_port);
}

/* ================= LOOP ================= */

void loop() {
  if (!client.connected()) reconnectMQTT();
  client.loop();

  /* -------- Barrier Control (auto close after 5s) -------- */
  bool mhIn  = (digitalRead(MH_IN_PIN)  == LOW);
  bool mhOut = (digitalRead(MH_OUT_PIN) == LOW);

  static bool servoOpened = false;
  static unsigned long lastDetectedTime = 0;

  if (mhIn || mhOut) {
    lastDetectedTime = millis();

    if (!servoOpened) {
      servo.write(SERVO_OPEN);
      servoOpened = true;
    }
  } else {
    if (servoOpened && (millis() - lastDetectedTime >= AUTO_CLOSE_DELAY)) {
      servo.write(SERVO_CLOSE);
      servoOpened = false;
    }
  }

  /* -------- Parking Slots -------- */
  unsigned long now = millis();
  if (now - lastScan >= SCAN_INTERVAL) {
    lastScan = now;
    for (int i = 0; i < NUM_SLOTS; i++) {
      long d = measureDistanceCM(trigPins[i], echoPins[i]);
      publishSlot(i, d);
      delay(BETWEEN_SENSOR_MS);
    }
  }
}
