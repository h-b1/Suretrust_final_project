#include <WiFi.h>
#include <PubSubClient.h>
#include <SPI.h>
#include <MFRC522.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ------------------ RFID & OLED Setup ------------------
#define SS_PIN   5
#define RST_PIN  22

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

MFRC522 rfid(SS_PIN, RST_PIN);

// ------------------ Relay Setup ------------------
#define RELAY_PIN 14  // GPIO14 controls the solenoid lock

// ------------------ Registered Tags ------------------
const String ADMIN_TAG    = "5D17B05";
const String USER_A_TAG   = "5ED17B05";
const String USER_B_TAG   = "C78A6C05";
const String USER_C_TAG   = "47F06C05";
const String USER_D_TAG   = "9AF26B05";

// ------------------ Wi-Fi & MQTT Setup ------------------
const char* ssid     = "POCO F6";             // Replace with your Wi-Fi SSID
const char* password = "12345678";         // Replace with your Wi-Fi password

const char* mqtt_server = "broker.hivemq.com";
const int   mqtt_port   = 1883;

WiFiClient espClient;
PubSubClient client(espClient);

// MQTT Topics
const char* topic_unlock = "rfid/unlock";

// ------------------ Function Prototypes ------------------
void setup_wifi();
void reconnect();
void callback(char* topic, byte* payload, unsigned int length);
String getUIDString(byte *buffer, byte bufferSize);
void checkAccess(String uid);
void unlockSolenoid(int resource);

// ------------------ Setup ------------------
void setup() {
  Serial.begin(115200);
  SPI.begin();
  rfid.PCD_Init();

  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);  // Keep solenoid locked by default

  // OLED initialization
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED init failed!");
    while (true);
  }
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0, 0);
  display.println("RFID Access System");
  display.display();
  delay(1000);

  // Wi-Fi + MQTT setup
  setup_wifi();
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);
}

// ------------------ Main Loop ------------------
void loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  if (!rfid.PICC_IsNewCardPresent() || !rfid.PICC_ReadCardSerial()) return;

  String uid = getUIDString(rfid.uid.uidByte, rfid.uid.size);
  Serial.print("UID: "); Serial.println(uid);
  checkAccess(uid);

  rfid.PICC_HaltA();
}

// ------------------ Wi-Fi Setup ------------------
void setup_wifi() {
  delay(1000);
  Serial.print("Connecting to Wi-Fi: ");
  Serial.println(ssid);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    attempts++;
    if (attempts > 40) {
      Serial.println("\nFailed to connect. Check password & Wi-Fi.");
      return;
    }
  }

  Serial.println("\nWi-Fi connected!");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

// ------------------ MQTT Reconnect ------------------
void reconnect() {
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    String clientId = "ESP32Client-";
    clientId += String(random(0xffff), HEX);

    if (client.connect(clientId.c_str())) {
      Serial.println("connected");
      client.subscribe(topic_unlock);
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      delay(5000);
    }
  }
}

// ------------------ MQTT Callback ------------------
void callback(char* topic, byte* payload, unsigned int length) {
  String msg = "";
  for (unsigned int i = 0; i < length; i++) {
    msg += (char)payload[i];
  }

  Serial.print("Message arrived on topic: ");
  Serial.print(topic);
  Serial.print(". Message: ");
  Serial.println(msg);

  if (String(topic) == topic_unlock && msg == "1") {
    Serial.println("Remote unlock command received!");
    digitalWrite(RELAY_PIN, HIGH);
    delay(5000);
    digitalWrite(RELAY_PIN, LOW);
  }
}

// ------------------ Convert UID to String ------------------
String getUIDString(byte *buffer, byte bufferSize) {
  String uid = "";
  for (byte i = 0; i < bufferSize; i++) {
    if (buffer[i] < 0x10) uid += "0";
    uid += String(buffer[i], HEX);
  }
  uid.toUpperCase();
  return uid;
}

// ------------------ Check Access ------------------
void checkAccess(String uid) {
  String user = "";
  int resource = 0;
  bool accessGranted = false;

  if (uid == ADMIN_TAG) {
    user = "Admin";
    accessGranted = false;
  } else if (uid == USER_A_TAG) {
    user = "User A";
    resource = 1;
    accessGranted = true;
  } else if (uid == USER_B_TAG) {
    user = "User B";
    resource = 2;
    accessGranted = true;
  } else if (uid == USER_C_TAG) {
    user = "User C";
    resource = 3;
    accessGranted = true;
  } else if (uid == USER_D_TAG) {
    user = "User D";
    resource = 4;
    accessGranted = true;
  } else {
    user = "Unknown";
  }

  // OLED Display
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("UID: " + uid);
  display.println("User: " + user);
  if (accessGranted) {
    display.print("Access: Granted to ");
    display.println("R" + String(resource));
    unlockSolenoid(resource);
  } else {
    display.println("Access: Denied");
  }
  display.display();

  // Serial Print
  Serial.print("User: "); Serial.println(user);
  Serial.println(accessGranted ? "Access Granted" : "Access Denied");

  // ------------------ MQTT Publish per user ------------------
  String log_msg = "UID: " + uid + ", User: " + user + ", Access: " + (accessGranted ? "Granted" : "Denied");

  String topic;
  if (uid == USER_A_TAG) topic = "rfid/logs/userA";
  else if (uid == USER_B_TAG) topic = "rfid/logs/userB";
  else if (uid == USER_C_TAG) topic = "rfid/logs/userC";
  else if (uid == USER_D_TAG) topic = "rfid/logs/userD";
  else if (uid == ADMIN_TAG) topic = "rfid/logs/admin";
  else topic = "rfid/logs/unknown";

  client.publish(topic.c_str(), log_msg.c_str());
}

// ------------------ Unlock Solenoid ------------------
void unlockSolenoid(int resource) {
  Serial.println("Unlocking for resource: R" + String(resource));
  digitalWrite(RELAY_PIN, HIGH);
  delay(5000);
  digitalWrite(RELAY_PIN, LOW);
}
