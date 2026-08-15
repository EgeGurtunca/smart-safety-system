#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <DHT.h>
#include <SPI.h>
#include <SD.h>
#include <SoftwareSerial.h>

#define MQ2_PIN A0
#define FLAME_PIN A1

#define DHT_PIN 2
#define DHT_TYPE DHT11

#define BUZZER_PIN 3
#define BUTTON_PIN 4
#define RELAY_PIN 5

#define GREEN_LED 6
#define RED_LED 7

// Arduino -> NodeMCU
#define NODE_RX 8
#define NODE_TX 9

#define SD_CS 10

#define GAS_THRESHOLD 400
#define FLAME_THRESHOLD 80

#define RELAY_ON LOW
#define RELAY_OFF HIGH

LiquidCrystal_I2C lcd(0x27, 16, 2);
DHT dht(DHT_PIN, DHT_TYPE);

SoftwareSerial nodeSerial(NODE_RX, NODE_TX);

bool sdOK = false;

unsigned long lastLog = 0;
unsigned long lastLCD = 0;
unsigned long lastNodeSend = 0;

void setup() {

  Serial.begin(9600);
  nodeSerial.begin(9600);

  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(GREEN_LED, OUTPUT);
  pinMode(RED_LED, OUTPUT);

  digitalWrite(RELAY_PIN, RELAY_OFF);
  digitalWrite(GREEN_LED, HIGH);
  digitalWrite(RED_LED, LOW);

  noTone(BUZZER_PIN);

  dht.begin();

  lcd.init();
  lcd.backlight();
  lcd.clear();

  lcd.setCursor(0, 0);
  lcd.print("SMART SYSTEM");

  lcd.setCursor(0, 1);
  lcd.print("Starting...");

  delay(1500);

  if (SD.begin(SD_CS)) {

    sdOK = true;

    if (!SD.exists("log.csv")) {

      File file = SD.open("log.csv", FILE_WRITE);

      if (file) {
        file.println(
          "Time,Temperature,Humidity,Gas,Flame,Button,Fan,Alarm"
        );

        file.close();
      }
    }

    Serial.println("SD OK");

  } else {

    Serial.println("SD FAILED");
  }

  lcd.clear();
  lcd.print("SYSTEM READY");

  delay(1000);
  lcd.clear();
}

void loop() {

  // -------------------------
  // SENSORLAR
  // -------------------------

  int gasValue = analogRead(MQ2_PIN);
  int flameValue = analogRead(FLAME_PIN);

  float temperature = dht.readTemperature();
  float humidity = dht.readHumidity();

  bool buttonPressed =
    (digitalRead(BUTTON_PIN) == LOW);

  // -------------------------
  // ALARM
  // -------------------------

  bool gasAlarm =
    gasValue >= GAS_THRESHOLD;

  bool flameAlarm =
    flameValue <= FLAME_THRESHOLD;

  bool alarm =
    gasAlarm || flameAlarm;

  // -------------------------
  // FAN
  // -------------------------

  bool fanOn = alarm;

  if (buttonPressed) {
    fanOn = true;
  }

  digitalWrite(
    RELAY_PIN,
    fanOn ? RELAY_ON : RELAY_OFF
  );

  // -------------------------
  // LED
  // -------------------------

  if (alarm) {

    digitalWrite(RED_LED, HIGH);
    digitalWrite(GREEN_LED, LOW);

  } else {

    digitalWrite(RED_LED, LOW);
    digitalWrite(GREEN_LED, HIGH);
  }

  // -------------------------
  // BUZZER
  // -------------------------

  if (alarm) {
    tone(BUZZER_PIN, 1000);
  } else {
    noTone(BUZZER_PIN);
  }

  // -------------------------
  // LCD
  // -------------------------

  if (millis() - lastLCD >= 1000) {

    lastLCD = millis();

    lcd.clear();

    lcd.setCursor(0, 0);

    if (isnan(temperature)) {
      lcd.print("T:ERR");
    } else {
      lcd.print("T:");
      lcd.print(temperature, 1);
      lcd.print("C");
    }

    lcd.print(" ");

    if (isnan(humidity)) {
      lcd.print("H:ERR");
    } else {
      lcd.print("H:");
      lcd.print(humidity, 0);
      lcd.print("%");
    }

    lcd.setCursor(0, 1);

    lcd.print("G:");
    lcd.print(gasValue);

    lcd.print(" F:");
    lcd.print(flameValue);

    if (alarm) {
      lcd.setCursor(15, 1);
      lcd.print("!");
    }
  }

  // -------------------------
  // SD CARD
  // -------------------------

  if (sdOK && millis() - lastLog >= 2000) {

    lastLog = millis();

    File file =
      SD.open("log.csv", FILE_WRITE);

    if (file) {

      file.print(millis());
      file.print(",");

      file.print(temperature);
      file.print(",");

      file.print(humidity);
      file.print(",");

      file.print(gasValue);
      file.print(",");

      file.print(flameValue);
      file.print(",");

      file.print(buttonPressed);
      file.print(",");

      file.print(fanOn);
      file.print(",");

      file.println(alarm);

      file.close();
    }
  }

  // -------------------------
  // NODEMCU'YA VERİ GÖNDER
  // -------------------------

  if (millis() - lastNodeSend >= 1000) {

    lastNodeSend = millis();

    nodeSerial.print("@");

    nodeSerial.print("T=");
    nodeSerial.print(temperature);

    nodeSerial.print(",H=");
    nodeSerial.print(humidity);

    nodeSerial.print(",G=");
    nodeSerial.print(gasValue);

    nodeSerial.print(",F=");
    nodeSerial.print(flameValue);

    nodeSerial.print(",A=");
    nodeSerial.print(alarm);

    nodeSerial.print(",FN=");
    nodeSerial.println(fanOn);
  }

  // -------------------------
  // ARDUINO SERIAL MONITOR
  // -------------------------

  Serial.print("Temperature: ");
  Serial.print(temperature);

  Serial.print(" | Humidity: ");
  Serial.print(humidity);

  Serial.print(" | Gas: ");
  Serial.print(gasValue);

  Serial.print(" | Flame: ");
  Serial.print(flameValue);

  Serial.print(" | Button: ");
  Serial.print(buttonPressed);

  Serial.print(" | Fan: ");
  Serial.print(fanOn);

  Serial.print(" | Alarm: ");
  Serial.println(alarm);

  delay(1000);
}