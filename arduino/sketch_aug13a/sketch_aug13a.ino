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
//
// DHT11 nemi 20-90% RH araliginda olcuyor. Bu araligin disinda kalan
// bir sinir sensorden hic gelmeyecegi icin uyariyi olu birakir; bu
// yuzden varsayilanlar aralik icinde. Uzaktan ayarlanabilir.
#define HUMIDITY_HIGH_DEFAULT 80
#define HUMIDITY_LOW_DEFAULT 25
#define SCREEN_SWAP_MS 2000

// EEPROM: esikler elektrik kesintisinde kaybolmasin.
//
// Sihirli sayi 2 bayt: tek baytlik bir imzanin kartta kalmis rastgele
// bir bayta denk gelme ihtimali yuksek, o durumda cop degerler gecerli
// sanilir. Kayit duzeni degistiginde magic artiriliyor; boylece eski
// karttaki yarim veri ilk aciliste kendiliginden sifirlanir.
#define EEPROM_MAGIC 0xA55D
#define EEPROM_ADDR_MAGIC 0
#define EEPROM_ADDR_GAS 2
#define EEPROM_ADDR_FLAME 4
#define EEPROM_ADDR_TEMP_RISE 10
#define EEPROM_ADDR_TEMP_MAX 12
#define EEPROM_ADDR_TEMP_RISE_DEF 14
#define EEPROM_ADDR_TEMP_MAX_DEF 16
#define EEPROM_ADDR_HUM_HIGH 18
#define EEPROM_ADDR_HUM_LOW 20
#define EEPROM_ADDR_HUM_HIGH_DEF 22
#define EEPROM_ADDR_HUM_LOW_DEF 24

// Son damgalamada gecerli olan derleme varsayilanlari da saklaniyor.
// Kodda varsayilan degistirilip kart yeniden yuklendiginde EEPROM'daki
// eski deger degil derlenen deger kazansin diye: aksi halde #define
// degisikligi calisan sisteme hicbir zaman yansimaz.
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
int humidityHigh = HUMIDITY_HIGH_DEFAULT;
int humidityLow = HUMIDITY_LOW_DEFAULT;

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

// Buzzer su an caliyor mu? tone() gereksiz yere tekrar cagrilmasin.
bool buzzerOn = false;

// NodeMCU'dan gelen komut satiri. Uno'da RAM kisitli:
// SD + LCD + DHT + SoftwareSerial zaten yer yiyor, String kullanilmiyor.
char cmdBuf[48];
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
  int humHighDefault = HUMIDITY_HIGH_DEFAULT;
  int humLowDefault = HUMIDITY_LOW_DEFAULT;

  EEPROM.put(EEPROM_ADDR_MAGIC, magic);
  EEPROM.put(EEPROM_ADDR_GAS, gasThreshold);
  EEPROM.put(EEPROM_ADDR_FLAME, flameThreshold);
  EEPROM.put(EEPROM_ADDR_TEMP_RISE, tempRise);
  EEPROM.put(EEPROM_ADDR_TEMP_MAX, tempMax);
  EEPROM.put(EEPROM_ADDR_HUM_HIGH, humidityHigh);
  EEPROM.put(EEPROM_ADDR_HUM_LOW, humidityLow);

  EEPROM.put(EEPROM_ADDR_GAS_DEF, gasDefault);
  EEPROM.put(EEPROM_ADDR_FLAME_DEF, flameDefault);
  EEPROM.put(EEPROM_ADDR_TEMP_RISE_DEF, riseDefault);
  EEPROM.put(EEPROM_ADDR_TEMP_MAX_DEF, maxDefault);
  EEPROM.put(EEPROM_ADDR_HUM_HIGH_DEF, humHighDefault);
  EEPROM.put(EEPROM_ADDR_HUM_LOW_DEF, humLowDefault);
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
  int humHighDefault;
  int humLowDefault;

  EEPROM.get(EEPROM_ADDR_GAS_DEF, gasDefault);
  EEPROM.get(EEPROM_ADDR_FLAME_DEF, flameDefault);
  EEPROM.get(EEPROM_ADDR_TEMP_RISE_DEF, riseDefault);
  EEPROM.get(EEPROM_ADDR_TEMP_MAX_DEF, maxDefault);
  EEPROM.get(EEPROM_ADDR_HUM_HIGH_DEF, humHighDefault);
  EEPROM.get(EEPROM_ADDR_HUM_LOW_DEF, humLowDefault);

  if (gasDefault != GAS_THRESHOLD_DEFAULT ||
      flameDefault != FLAME_THRESHOLD_DEFAULT ||
      riseDefault != TEMP_RISE_DEFAULT ||
      maxDefault != TEMP_MAX_DEFAULT ||
      humHighDefault != HUMIDITY_HIGH_DEFAULT ||
      humLowDefault != HUMIDITY_LOW_DEFAULT) {

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
  int hh;
  int hl;

  EEPROM.get(EEPROM_ADDR_GAS, g);
  EEPROM.get(EEPROM_ADDR_FLAME, f);
  EEPROM.get(EEPROM_ADDR_TEMP_RISE, r);
  EEPROM.get(EEPROM_ADDR_TEMP_MAX, m);
  EEPROM.get(EEPROM_ADDR_HUM_HIGH, hh);
  EEPROM.get(EEPROM_ADDR_HUM_LOW, hl);

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

  // DHT11 araligi disinda bir esik uyariyi olu birakir.
  if (hh >= 20 && hh <= 90 && hl >= 20 && hl <= 90 && hh > hl) {
    humidityHigh = hh;
    humidityLow = hl;
  }
}


// -------------------------
// KOMUT: #F1,M0,G400,L80,R5,X45,H80,W25
// -------------------------

// Anahtar yoksa ya da ardindan rakam gelmiyorsa bu deger doner.
// Mevcut degere sessizce dusmek, kesilmis bir satirin yarim
// uygulanmasi anlamina gelir.
#define FIELD_MISSING -32000

int fieldValue(const char *line, char key) {

  const char *p = strchr(line, key);

  if (!p) {
    return FIELD_MISSING;
  }

  p++;

  if (*p == '-' || *p == '+') {
    p++;
  }

  if (!isdigit((unsigned char)*p)) {
    return FIELD_MISSING;
  }

  return atoi(strchr(line, key) + 1);
}

/**
 * XOR sagalamasi: '#' ile '*' arasindaki karakterlerin XOR'u.
 *
 * SoftwareSerial yari cift yonlu -- Arduino nodeSerial.print() ile
 * veri gonderirken (saniyede bir, kesmeler kapali) ayni anda alamiyor.
 * Komut tam o pencerede gelirse baytlar kayboluyor, satir sonu da
 * kaybolunca iki komut birbirine yapisiyor. Bozuk satiri uygulamak
 * cihaza sacma esik yazar; sagalama tutmuyorsa satiri atiyoruz.
 * NodeMCU 5 saniyede bir tekrar gonderiyor, kayip sorun degil.
 */
bool checksumOk(const char *line) {

  const char *star = strrchr(line, '*');

  if (!star || star == line) {
    return false;
  }

  byte sum = 0;

  for (const char *p = line + 1; p < star; p++) {
    sum ^= (byte)(*p);
  }

  return (byte)strtol(star + 1, NULL, 16) == sum;
}

void applyCommand(const char *line) {

  if (line[0] != '#') {
    return;
  }

  if (!checksumOk(line)) {
    Serial.print(F("CMD BOZUK, atlandi: "));
    Serial.println(line);
    return;
  }

  int fanValue = fieldValue(line, 'F');
  int muteValue = fieldValue(line, 'M');

  int g = fieldValue(line, 'G');
  int f = fieldValue(line, 'L');
  int r = fieldValue(line, 'R');
  int x = fieldValue(line, 'X');
  int hh = fieldValue(line, 'H');
  int hl = fieldValue(line, 'W');

  // Sekiz alanin biri bile eksikse satir kesilmis demektir.
  if (fanValue == FIELD_MISSING || muteValue == FIELD_MISSING ||
      g == FIELD_MISSING || f == FIELD_MISSING ||
      r == FIELD_MISSING || x == FIELD_MISSING ||
      hh == FIELD_MISSING || hl == FIELD_MISSING) {

    Serial.print(F("CMD EKSIK ALAN, atlandi: "));
    Serial.println(line);
    return;
  }

  remoteFan = fanValue != 0;
  remoteMute = muteValue != 0;

  if (g >= 0 && g <= 1023 &&
      f >= 0 && f <= 1023 &&
      r >= 1 && r <= 30 &&
      x >= 0 && x <= 50 &&
      hh >= 20 && hh <= 90 && hl >= 20 && hl <= 90 && hh > hl &&
      (g != gasThreshold || f != flameThreshold ||
       r != tempRise || x != tempMax ||
       hh != humidityHigh || hl != humidityLow)) {

    gasThreshold = g;
    flameThreshold = f;
    tempRise = r;
    tempMax = x;
    humidityHigh = hh;
    humidityLow = hl;

    saveThresholds();

    Serial.print(F("EEPROM yazildi G:"));
    Serial.print(gasThreshold);
    Serial.print(F(" L:"));
    Serial.print(flameThreshold);
    Serial.print(F(" R:"));
    Serial.print(tempRise);
    Serial.print(F(" X:"));
    Serial.print(tempMax);
    Serial.print(F(" H:"));
    Serial.print(humidityHigh);
    Serial.print(F(" W:"));
    Serial.println(humidityLow);
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
  Serial.print(tempMax);
  Serial.print(F(" H:"));
  Serial.print(humidityHigh);
  Serial.print(F(" W:"));
  Serial.println(humidityLow);

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
  //
  // tone() her cagrildiginda Timer2'yi bastan kuruyor. Dongu saniyede
  // binlerce kez dondugu icin surekli cagrilirsa dalga formu hic
  // tamamlanamaz ve buzzer ya hic otmez ya cilizca oter. Bu yuzden
  // sadece durum degistiginde cagriliyor.
  bool wantBuzzer = alarm && !remoteMute;

  if (wantBuzzer != buzzerOn) {

    buzzerOn = wantBuzzer;

    if (buzzerOn) {
      tone(BUZZER_PIN, 1000);
    } else {
      noTone(BUZZER_PIN);
    }
  }

  // -------------------------
  // LCD
  // -------------------------

  // Nem sinir disindaysa LCD 2 saniye normal degerleri, 2 saniye
  // uyariyi gosterir. Alarm varken uyari ekrani devre disi:
  // o anda ekranda sensor degerleri durmali.
  bool humidityWarn =
    !isnan(humidity) &&
    (humidity >= humidityHigh || humidity <= humidityLow);

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

      if (humidity >= humidityHigh) {
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
