#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <MPU6050.h>
#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <ArduinoJson.h>

// Normal position baseline values
const float baselineAX = 0.03;  // Normal AX value
const float baselineAY = 0.07;  // Normal AY value
const float tiltThreshold = 1.0;  // Change in g-force required to detect a tilt


// Wi-Fi Credentials
const char* ssid = "project";
const char* password = "project007";

// Telegram Bot Token and Chat ID
#define BOT_TOKEN "7231755529:AAGWMdOcVJHXFXkaZuQanEvdmPCAHQ-xNsw"
#define CHAT_ID "7075737497"

// Initialize LCD
LiquidCrystal_I2C lcd(0x27, 16, 2);

// Initialize MPU6050
MPU6050 mpu;

// Pins
const int alcoholSensorPin = D0;
const int buzzerPin = D8;
const int Seatbelt = D5;
const int relay = D7;
const int EN = D6;  // Enable pin for L293D

// Variables
int alcoholValue = 0;
bool alcoholDetected = false;
bool tiltDetected = false;
bool seatbeltFastened = false;
bool motorRunning = false;
bool telegramSentAlcohol = false;
bool telegramSentTilt = false;
int motorSpeed = 255;

// Telegram Bot
X509List cert(TELEGRAM_CERTIFICATE_ROOT);
WiFiClientSecure secured_client;
UniversalTelegramBot bot(BOT_TOKEN, secured_client);

void setup() {
  Serial.begin(115200);
  analogWrite(EN, 0);

  // Initialize LCD
  lcd.init();
  lcd.backlight();
  lcd.print("System Starting");

  // Initialize MPU6050
  Wire.begin();
  mpu.initialize();
  if (!mpu.testConnection()) {
    lcd.setCursor(0, 1);
    lcd.print("MPU6050 Error");
    while (1);
  }

  // Initialize Wi-Fi
  Serial.print("Connecting to Wi-Fi");
  WiFi.begin(ssid, password);
  secured_client.setTrustAnchors(&cert); // Add root certificate for api.telegram.org
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi Connected");
  Serial.println("IP Address: " + WiFi.localIP().toString());

  // Configure time for SSL
  Serial.print("Retrieving time: ");
  configTime(0, 0, "pool.ntp.org"); // get UTC time via NTP
  time_t now = time(nullptr);
  while (now < 24 * 3600) {
    Serial.print(".");
    delay(100);
    now = time(nullptr);
  }
  Serial.println(now);

  // Initialize Pins
  pinMode(buzzerPin, OUTPUT);
  pinMode(relay, OUTPUT);
  digitalWrite(relay, LOW);
  pinMode(alcoholSensorPin, INPUT);
  pinMode(Seatbelt, INPUT);
  pinMode(EN, OUTPUT);
  digitalWrite(buzzerPin, LOW);
  stopMotor();

  lcd.setCursor(0, 0);
  lcd.print("G.H.S.V Alampur");
  lcd.setCursor(0, 1);
  lcd.print("     ETAH      ");

  sendTelegramMessage("🚗 System Running and Activated.");
}

void loop() {
  seatbeltFastened = digitalRead(Seatbelt) == LOW;

  if (!seatbeltFastened) {
    stopMotor();
    lcd.setCursor(0, 0);
    lcd.print("   SEAT BELT   ");
    lcd.setCursor(0, 1);
    lcd.print("  NOT CONNECTED");
    return;
  }

  alcoholValue = digitalRead(alcoholSensorPin);
  //Serial.println("ALCOHOL=" + String(alcoholValue));

  if (alcoholValue == LOW) {
    if (!alcoholDetected) {
      alcoholDetected = true;
      handleAlcoholDetection();
    }
  } else {
    alcoholDetected = false;
    lcd.setCursor(0, 0);
    lcd.print("   NO ALCOHOL  ");
    lcd.setCursor(0, 1);
    lcd.print("   Detected    ");
    digitalWrite(relay, HIGH);
    telegramSentAlcohol = false;  // Reset flag when condition changes
  }

  // Read MPU6050 for Tilt Detection
  int16_t ax, ay, az;
  mpu.getAcceleration(&ax, &ay, &az);
  float axG = ax / 16384.0;
  float ayG = ay / 16384.0;

  //Serial.println("AX=" + String(axG) + "g, AY=" + String(ayG) + "g");

  tiltDetected = fabs(axG) > 0.70 || fabs(ayG) > 0.70;

  if (tiltDetected) 
  {
    if (!telegramSentTilt) 
    {  // Prevent repeated Telegram messages
      handleTiltDetection(axG, ayG);
      telegramSentTilt = true;
    }
  } else {
    telegramSentTilt = false;  // Reset flag when no tilt detected
  }

  // Motor Control Logic
  if (!alcoholDetected && !tiltDetected && seatbeltFastened && !motorRunning) {
    startMotor();
  } else if ((alcoholDetected || tiltDetected || !seatbeltFastened) && motorRunning) {
    stopMotorSlowly();
  }

  delay(100);
}

void handleAlcoholDetection() {
  lcd.clear();
  lcd.print("Alcohol Detected!");
  digitalWrite(buzzerPin, HIGH);
  if (!telegramSentAlcohol) {
    sendTelegramMessage("🚨 Alcohol Detected! Stopping Motor and Activating Buzzer.");
    telegramSentAlcohol = true;
  }
  stopMotorSlowly();
  digitalWrite(buzzerPin, LOW);
}

void handleTiltDetection(float axG, float ayG) 
{
  lcd.clear();
  String tiltMessage = "⚠️ Tilt Detected! ";

  if (axG > 0.90) 
  {
    lcd.print("Tilt: Left");
    tiltMessage += "Left.";
  } 
  else if (axG < -1.03) 
  {
    lcd.print("Tilt: Right");
    tiltMessage += "RIght.";

  } 
  else if (ayG > 0.90) 
  {
    lcd.print("Tilt: Backward");
    tiltMessage += "Backward.";

  } 
  else if (ayG < -0.90) 
  {
    lcd.print("Tilt: Forward");
    tiltMessage += "Forward.";

  }

  sendTelegramMessage(tiltMessage);
  delay(1000);
  sendAccidentLocation();  // Send GPS location
  stopMotorSlowly();
}

void stopMotorSlowly() {
  if (!motorRunning) return;  // Prevent redundant execution

  for (int i = motorSpeed; i >= 0; i -= 10) {
    analogWrite(EN, i);
    lcd.setCursor(0, 0);
    lcd.print("Slowing Down  ");
    lcd.setCursor(0, 1);
    lcd.print("Speed: " + String((i * 100) / 255) + "% ");
    delay(300);
  }
  analogWrite(EN, 0);
  digitalWrite(relay, LOW);
  motorRunning = false;
  lcd.setCursor(0, 0);
  lcd.print("Motor Stopped");
  lcd.setCursor(0, 1);
  lcd.print("Safety Mode On");
}

void stopMotor() {
  analogWrite(EN, 0);
  digitalWrite(relay, LOW);
  motorRunning = false;
}

void startMotor() {
  analogWrite(EN, motorSpeed);
  digitalWrite(relay, HIGH);
  motorRunning = true;
  lcd.setCursor(0, 0);
  lcd.print("Motor Running ");
  lcd.setCursor(0, 1);
  lcd.print("Speed: 100%   ");
}

void sendTelegramMessage(const String& message) {
  WiFiClientSecure client;
  client.setInsecure();  // ⚡ This allows an HTTPS request without SSL verification

  Serial.println("Connecting to Telegram...");

  if (!client.connect("api.telegram.org", 443)) {
    Serial.println("Connection failed!");
    return;
  }

  String url = "/bot" + String(BOT_TOKEN) + "/sendMessage?chat_id=" + String(CHAT_ID) + "&text=" + message;

  Serial.println("Sending request...");
  client.print(String("GET ") + url + " HTTP/1.1\r\n" +
               "Host: api.telegram.org\r\n" +
               "User-Agent: ESP8266\r\n" +
               "Connection: close\r\n\r\n");

  delay(1000);

  Serial.println("Response from Telegram:");
  while (client.available()) {
    Serial.write(client.read());
  }

  Serial.println("\nMessage sent!");
}


void sendAccidentLocation() {
  // Replace with the actual accident location coordinates
  float latitude = 28.7041;   // Example: Delhi
  float longitude = 77.1025;  // Example: Delhi

  //String googleMapsLink = "📍 Accident Location: \n";
   String googleMapsLink = "https://www.google.com/maps?q=" + String(latitude, 6) + "," + String(longitude, 6);

  sendTelegramMessage(googleMapsLink);
}