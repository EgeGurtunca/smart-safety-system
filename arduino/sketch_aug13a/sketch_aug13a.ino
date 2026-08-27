#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <DHT.h>
#include <SPI.h>
#include <SD.h>
#include <SoftwareSerial.h>
#include <EEPROM.h>

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

#define GAS_THRESHOLD_DEFAULT 400
#define FLAME_THRESHOLD_DEFAULT 80

// Sicaklik alarmi.
// TEMP_RISE: 60 saniyede bu kadar C yukselirse alarm. Yanginin imzasi
//   mutlak sicaklik degil, hizli yukselistir -- mutlak esik yazin yanlis
//   alarm verir, kisin gec kalir.
// TEMP_MAX: mutlak tavan, yavas yukselen durumu yakalar.
//   DHT11 sadece 0-50 C olcuyor, 50 uzeri bir tavan hic tetiklenmez.
#define TEMP_RISE_DEFAULT 5
#define TEMP_MAX_DEFAULT 45

// 6 x 10 saniye = 60 saniyelik kayan pencere.
#define TEMP_HISTORY 6
#define TEMP_SAMPLE_MS 10000

#define RELAY_ON LOW
#define RELAY_OFF HIGH

// Nem uyari sinirlari. Alarm DEGIL: fan, buzzer ve role etkilenmez,
// sadece LCD'de donusumlu gosterilir.
#define HUMIDITY_HIGH 95
#define HUMIDITY_LOW 5
#define SCREEN_SWAP_MS 2000

// EEPROM: esikler elektrik kesintisinde kaybolmasin.
//
// Sihirli sayi 2 bayt: tek baytlik bir imza kartta kalmis eski veriyle
// tesadufen eslesip cop degerleri gecerli sanmaya yol aciyordu.
// Sicaklik esikleri eklenince duzen degisti; magic'i artirmak eski
// kartlardaki yarim veriyi otomatik sifirlar.
#define EEPROM_MAGIC 0xA55B
#define EEPROM_ADDR_MAGIC 0
#define EEPROM_ADDR_GAS 2
#define EEPROM_ADDR_FLAME 4
#define EEPROM_ADDR_TEMP_RISE 10
#define EEPROM_ADDR_TEMP_MAX 12
#define EEPROM_ADDR_TEMP_RISE_DEF 14
#define EEPROM_ADDR_TEMP_MAX_DEF 16

// Son damgalamada gecerli olan derleme varsayilanlari da saklaniyor.
// Kodda varsayilani degistirip yuklersen EEPROM'daki eski deger degil
// senin yazdigin deger kazanir -- yoksa "#define'i degistirdim ama
// hicbir sey olmuyor" tuzagina dusuluyor.
#define EEPROM_ADDR_GAS_DEF 6
#define EEPROM_ADDR_FLAME_DEF 8

LiquidCrystal_I2C lcd(0x27, 16, 2);
DHT dht(DHT_PIN, DHT_TYPE);

SoftwareSerial nodeSerial(NODE_RX, NODE_TX);

bool sdOK = false;

// Uzaktan gelen komut durumu.
bool remoteFan = false;
bool remoteMute = false;

int gasThreshold = GAS_THRESHOLD_DEFAULT;
int flameThreshold = FLAME_THRESHOLD_DEFAULT;
int tempRise = TEMP_RISE_DEFAULT;
int tempMax = TEMP_MAX_DEFAULT;

// Sicaklik gecmisi: 10 saniyede bir ornek, 60 saniye once ile karsilastirilir.
float tempHistory[TEMP_HISTORY];
byte tempIndex = 0;
bool tempHistoryFull = false;
unsigned long lastTempSample = 0;

// DHT11 saniyede birden fazla okumada nan doner ve ~250 ms bloklar,
// bu yuzden zamanlayiciya bagli ve deger onbellekleniyor.
float temperature = NAN;
float humidity = NAN;

unsigned long lastLog = 0;
unsigned long lastLCD = 0;
unsigned long lastNodeSend = 0;
unsigned long lastDHT = 0;
unsigned long lastPrint = 0;
unsigned long lastScreenSwap = 0;

// LCD nem uyari ekranini mi gosteriyor?
bool showHumidityScreen = false;

// NodeMCU'dan gelen komut satiri. Uno'da RAM kisitli:
// SD + LCD + DHT + SoftwareSerial zaten yer yiyor, String kullanilmiyor.
char cmdBuf[32];
byte cmdLen = 0;


// -------------------------
// EEPROM
// -------------------------

void saveThresholds() {

  uint16_t magic = EEPROM_MAGIC;

  int gasDefault = GAS_THRESHOLD_DEFAULT;
  int flameDefault = FLAME_THRESHOLD_DEFAULT;

  // EEPROM.put degismeyen baytlari yazmiyor; ayrica sadece
  // deger degistiginde cagriliyor (~100k yazim omru).
  int riseDefault = TEMP_RISE_DEFAULT;
  int maxDefault = TEMP_MAX_DEFAULT;

  EEPROM.put(EEPROM_ADDR_MAGIC, magic);
  EEPROM.put(EEPROM_ADDR_GAS, gasThreshold);
  EEPROM.put(EEPROM_ADDR_FLAME, flameThreshold);
  EEPROM.put(EEPROM_ADDR_TEMP_RISE, tempRise);
  EEPROM.put(EEPROM_ADDR_TEMP_MAX, tempMax);

  EEPROM.put(EEPROM_ADDR_GAS_DEF, gasDefault);
  EEPROM.put(EEPROM_ADDR_FLAME_DEF, flameDefault);
  EEPROM.put(EEPROM_ADDR_TEMP_RISE_DEF, riseDefault);
  EEPROM.put(EEPROM_ADDR_TEMP_MAX_DEF, maxDefault);
}

void loadThresholds() {

  uint16_t magic = 0;

  EEPROM.get(EEPROM_ADDR_MAGIC, magic);

  if (magic != EEPROM_MAGIC) {
    // Ilk acilis ya da eski/bozuk icerik: varsayilanlari damgala.
    saveThresholds();
    return;
  }

  int gasDefault;
  int flameDefault;
  int riseDefault;
  int maxDefault;

  EEPROM.get(EEPROM_ADDR_GAS_DEF, gasDefault);
  EEPROM.get(EEPROM_ADDR_FLAME_DEF, flameDefault);
  EEPROM.get(EEPROM_ADDR_TEMP_RISE_DEF, riseDefault);
  EEPROM.get(EEPROM_ADDR_TEMP_MAX_DEF, maxDefault);

  if (gasDefault != GAS_THRESHOLD_DEFAULT ||
      flameDefault != FLAME_THRESHOLD_DEFAULT ||
      riseDefault != TEMP_RISE_DEFAULT ||
      maxDefault != TEMP_MAX_DEFAULT) {

    // Kodda varsayilan degistirilmis: derlenen deger kazanir,
    // EEPROM'daki eski deger uzerine yazilir.
    Serial.println(F("Kod varsayilanlari degismis, EEPROM sifirlandi"));

    saveThresholds();
    return;
  }

  int g;
  int f;
  int r;
  int m;

  EEPROM.get(EEPROM_ADDR_GAS, g);
  EEPROM.get(EEPROM_ADDR_FLAME, f);
  EEPROM.get(EEPROM_ADDR_TEMP_RISE, r);
  EEPROM.get(EEPROM_ADDR_TEMP_MAX, m);

  // Cop deger okunursa varsayilanda kal.
  if (g >= 0 && g <= 1023) {
    gasThreshold = g;
  }

  if (f >= 0 && f <= 1023) {
    flameThreshold = f;
  }

  if (r >= 1 && r <= 30) {
    tempRise = r;
  }

  if (m >= 0 && m <= 50) {
    tempMax = m;
  }
}


// -------------------------
// KOMUT: #F1,M0,G400,L80
// -------------------------

int fieldValue(const char *line, char key, int fallback) {

  const char *p = strchr(line, key);

  if (!p) {
    return fallback;
  }

  return atoi(p + 1);
}

void applyCommand(const char *line) {

  if (line[0] != '#') {
    return;
  }

  remoteFan = fieldValue(line, 'F', 0) != 0;
  remoteMute = fieldValue(line, 'M', 0) != 0;

  int g = fieldValue(line, 'G', gasThreshold);
  int f = fieldValue(line, 'L', flameThreshold);
  int r = fieldValue(line, 'R', tempRise);
  int x = fieldValue(line, 'X', tempMax);

  if (g >= 0 && g <= 1023 &&
      f >= 0 && f <= 1023 &&
      r >= 1 && r <= 30 &&
      x >= 0 && x <= 50 &&
      (g != gasThreshold || f != flameThreshold ||
       r != tempRise || x != tempMax)) {

    gasThreshold = g;
    flameThreshold = f;
    tempRise = r;
    tempMax = x;

    saveThresholds();

    Serial.print(F("EEPROM yazildi G:"));
    Serial.print(gasThreshold);
    Serial.print(F(" L:"));
    Serial.print(flameThreshold);
    Serial.print(F(" R:"));
    Serial.print(tempRise);
    Serial.print(F(" X:"));
    Serial.println(tempMax);
  }

  Serial.print(F("CMD alindi: "));
  Serial.println(line);
}

void readCommand() {

  while (nodeSerial.available()) {

    char c = nodeSerial.read();

    if (c == '\n' || c == '\r') {

      if (cmdLen > 0) {
        cmdBuf[cmdLen] = '\0';
        applyCommand(cmdBuf);
        cmdLen = 0;
      }

      continue;
    }

    if (cmdLen < sizeof(cmdBuf) - 1) {
      cmdBuf[cmdLen++] = c;

    } else {
      // Tasma: bozuk satiri at, yarim komut uygulama.
      cmdLen = 0;
    }
  }
}


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

  loadThresholds();

  for (byte i = 0; i < TEMP_HISTORY; i++) {
    tempHistory[i] = NAN;
  }

  dht.begin();

  lcd.init();
  lcd.backlight();
  lcd.clear();

  lcd.setCursor(0, 0);
  lcd.print(F("SMART SYSTEM"));

  lcd.setCursor(0, 1);
  lcd.print(F("Starting..."));

  delay(1500);

  if (SD.begin(SD_CS)) {

    sdOK = true;

    if (!SD.exists("log.csv")) {

      File file = SD.open("log.csv", FILE_WRITE);

      if (file) {
        file.println(
          F("Time,Temperature,Humidity,Gas,Flame,Button,Fan,Alarm")
        );

        file.close();
      }
    }

    Serial.println(F("SD OK"));

  } else {

    Serial.println(F("SD FAILED"));
  }

  Serial.print(F("Esikler G:"));
  Serial.print(gasThreshold);
  Serial.print(F(" L:"));
  Serial.print(flameThreshold);
  Serial.print(F(" R:"));
  Serial.print(tempRise);
  Serial.print(F(" X:"));
  Serial.println(tempMax);

  lcd.clear();
  lcd.print(F("SYSTEM READY"));

  delay(1000);
  lcd.clear();
}

void loop() {

  // -------------------------
  // KOMUT OKU
  // -------------------------

  readCommand();

  // -------------------------
  // SENSORLAR
  // -------------------------

  int gasValue = analogRead(MQ2_PIN);
  int flameValue = analogRead(FLAME_PIN);

  if (millis() - lastDHT >= 2000) {

    lastDHT = millis();

    temperature = dht.readTemperature();
    humidity = dht.readHumidity();
  }

  bool buttonPressed =
    (digitalRead(BUTTON_PIN) == LOW);

  // -------------------------
  // ALARM
  // -------------------------

  bool gasAlarm =
    gasValue >= gasThreshold;

  bool flameAlarm =
    flameValue <= flameThreshold;

  // -------------------------
  // SICAKLIK
  //
  // 10 saniyede bir ornek alinip 60 saniye oncekiyle karsilastiriliyor.
  // Yanginin imzasi mutlak sicaklik degil hizli yukselis; mutlak esik
  // tek basina yazin yanlis alarm verir, kisin gec kalir.
  // -------------------------

  if (millis() - lastTempSample >= TEMP_SAMPLE_MS) {

    lastTempSample = millis();

    tempHistory[tempIndex] = temperature;
    tempIndex = (tempIndex + 1) % TEMP_HISTORY;

    if (tempIndex == 0) {
      tempHistoryFull = true;
    }
  }

  bool tempAlarm = false;

  if (!isnan(temperature)) {

    if (temperature >= tempMax) {
      tempAlarm = true;
    }

    if (tempHistoryFull) {

      // tempIndex, uzerine yazilacak olan slot: yani en eski ornek.
      float oldest = tempHistory[tempIndex];

      if (!isnan(oldest) && (temperature - oldest) >= tempRise) {
        tempAlarm = true;
      }
    }
  }

  bool alarm =
    gasAlarm || flameAlarm || tempAlarm;

  // -------------------------
  // FAN
  // -------------------------

  // Uzaktan sadece ACILIR. Alarm varken telefondan fan kapatilamaz.
  bool fanOn = alarm || buttonPressed || remoteFan;

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

  // Susturma sadece sesi keser; fan ve LED calismaya devam eder.
  if (alarm && !remoteMute) {
    tone(BUZZER_PIN, 1000);
  } else {
    noTone(BUZZER_PIN);
  }

  // -------------------------
  // LCD
  // -------------------------

  // Nem sinir disindaysa LCD 2 saniye normal degerleri, 2 saniye
  // uyariyi gosterir. Alarm varken uyari ekrani devre disi:
  // o anda ekranda sensor degerleri durmali.
  bool humidityWarn =
    !isnan(humidity) &&
    (humidity >= HUMIDITY_HIGH || humidity <= HUMIDITY_LOW);

  if (!humidityWarn || alarm) {

    showHumidityScreen = false;
    lastScreenSwap = millis();

  } else if (millis() - lastScreenSwap >= SCREEN_SWAP_MS) {

    lastScreenSwap = millis();
    showHumidityScreen = !showHumidityScreen;
  }

  if (millis() - lastLCD >= 1000) {

    lastLCD = millis();

    lcd.clear();

    if (showHumidityScreen) {

      lcd.setCursor(0, 0);
      lcd.print(F("NEM UYARISI"));

      lcd.setCursor(0, 1);
      lcd.print(F("%"));
      lcd.print(humidity, 0);

      if (humidity >= HUMIDITY_HIGH) {
        lcd.print(F(" COK NEMLI"));
      } else {
        lcd.print(F(" COK KURU"));
      }

    } else {

      lcd.setCursor(0, 0);

      if (isnan(temperature)) {
        lcd.print(F("T:ERR"));
      } else {
        lcd.print(F("T:"));
        lcd.print(temperature, 1);
        lcd.print(F("C"));
      }

      lcd.print(F(" "));

      if (isnan(humidity)) {
        lcd.print(F("H:ERR"));
      } else {
        lcd.print(F("H:"));
        lcd.print(humidity, 0);
        lcd.print(F("%"));
      }

      lcd.setCursor(0, 1);

      lcd.print(F("G:"));
      lcd.print(gasValue);

      lcd.print(F(" F:"));
      lcd.print(flameValue);

      if (alarm) {
        lcd.setCursor(15, 1);
        lcd.print(remoteMute ? "M" : "!");
      }
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
  // NODEMCU'YA VERI GONDER
  // FN son alan olmali, NodeMCU satir sonuna kadar okuyor.
  // -------------------------

  if (millis() - lastNodeSend >= 1000) {

    lastNodeSend = millis();

    nodeSerial.print(F("@"));

    nodeSerial.print(F("T="));
    nodeSerial.print(temperature);

    nodeSerial.print(F(",H="));
    nodeSerial.print(humidity);

    nodeSerial.print(F(",G="));
    nodeSerial.print(gasValue);

    nodeSerial.print(F(",F="));
    nodeSerial.print(flameValue);

    nodeSerial.print(F(",A="));
    nodeSerial.print(alarm);

    nodeSerial.print(F(",B="));
    nodeSerial.print(buttonPressed);

    nodeSerial.print(F(",FN="));
    nodeSerial.println(fanOn);
  }

  // -------------------------
  // ARDUINO SERIAL MONITOR
  // -------------------------

  if (millis() - lastPrint >= 1000) {

    lastPrint = millis();

    Serial.print(F("Temperature: "));
    Serial.print(temperature);

    Serial.print(F(" | Humidity: "));
    Serial.print(humidity);

    Serial.print(F(" | Gas: "));
    Serial.print(gasValue);
    Serial.print(F("/"));
    Serial.print(gasThreshold);

    Serial.print(F(" | Flame: "));
    Serial.print(flameValue);
    Serial.print(F("/"));
    Serial.print(flameThreshold);

    Serial.print(F(" | Button: "));
    Serial.print(buttonPressed);

    Serial.print(F(" | Fan: "));
    Serial.print(fanOn);

    Serial.print(F(" | RemoteFan: "));
    Serial.print(remoteFan);

    Serial.print(F(" | Mute: "));
    Serial.print(remoteMute);

    Serial.print(F(" | TempAlarm: "));
    Serial.print(tempAlarm);

    Serial.print(F(" | Alarm: "));
    Serial.println(alarm);
  }
}
