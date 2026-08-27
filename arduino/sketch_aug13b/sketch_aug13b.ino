#include <ESP8266WiFi.h>
#include <SoftwareSerial.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecureBearSSL.h>

// =================================================
// WIFI
// =================================================

// Kendi ag bilgilerini gir
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

// =================================================
// RENDER API
// =================================================

const char* serverURL =
  "https://smart-safety-system-y11a.onrender.com/api/data";

// Render'daki API_TOKEN ortam degiskeniyle ayni olmali.
const char* apiToken = "YOUR_API_TOKEN";

// =================================================
// ARDUINO <-> NODEMCU
//
// D7 = RX  <- Arduino D9 (TX), gerilim bolucu uzerinden
//            Bolucu sart: Arduino 5V veriyor, ESP8266 pinleri
//            5V toleransli degil.
//
// D6 = TX  -> Arduino D8 (RX), dogrudan
//            Bolucu YOK: 3.3V zaten Arduino'nun HIGH esigi olan
//            3.0V'un ustunde. Bolunse esigin altina duserdi.
// =================================================

SoftwareSerial arduinoSerial(D7, D6);

// =================================================
// SENSOR VALUES
// =================================================

String temperature = "--";
String humidity = "--";
String gas = "--";
String flame = "--";
String alarm = "0";
String fan = "0";
String button = "0";

// =================================================
// KOMUT (sunucudan inen istenen durum)
// =================================================

int cmdFan = 0;
int cmdMute = 0;
int cmdGas = 400;
int cmdFlame = 80;
int cmdTempRise = 5;
int cmdTempMax = 45;

unsigned long lastSend = 0;
unsigned long lastBlink = 0;

bool ledState = HIGH;


// =================================================
// LOG
//
// Sadece USB seri porta yaziyor. Eskiden log RAM'de
// birikip yerel web sayfasinda gosteriliyordu; o sayfa
// kaldirildi cunku HTTPS el sikismasi tek basina ~20 KB
// heap istiyor ve biriken String'ler heap'i parcaliyordu.
// =================================================

void addLog(String text) {
  Serial.println(text);
}


// =================================================
// JSON YARDIMCISI
//
// Arduino DHT okuyamazsa "nan" gonderiyor. Ham haliyle JSON'a
// girerse gecersiz JSON olur, Flask 400 doner ve o turun TAMAMI
// -- gaz ve alev dahil -- kaybolur. Sayi degilse null yaz.
// =================================================

String jsonNumber(String value) {

  value.trim();

  if (value.length() == 0)
    return "null";

  char first = value.charAt(0);

  if (!isDigit(first) && first != '-' && first != '+')
    return "null";

  return value;
}


// =================================================
// SUNUCU CEVABINDAN SAYI CEK
// =================================================

int extractInt(String src, String key, int fallback) {

  int start = src.indexOf("\"" + key + "\":");

  if (start < 0)
    return fallback;

  start += key.length() + 3;

  int end = start;

  while (end < (int)src.length()) {

    char c = src.charAt(end);

    if (!isDigit(c) && c != '-')
      break;

    end++;
  }

  if (end == start)
    return fallback;

  return src.substring(start, end).toInt();
}


// =================================================
// ARDUINO DATA READER
// =================================================

void readArduinoData() {

  while (arduinoSerial.available()) {

    String data =
      arduinoSerial.readStringUntil('\n');

    data.trim();

    if (data.length() == 0)
      continue;

    // Ham satiri her zaman bas. Bu satir olmadan "hic byte gelmiyor" ile
    // "bozuk byte geliyor" ayirt edilemiyor.
    addLog("ARDUINO: " + data);

    // ---------------------------------------------
    // Beklenen format:
    //
    // @T=25.9,H=74,G=76,F=160,A=0,B=0,FN=0
    // ---------------------------------------------

    if (!data.startsWith("@")) {
      addLog("  (@ ile baslamiyor, atlandi)");
      continue;
    }

    data.remove(0, 1);

    int start;
    int end;

    // ---------------------------------------------
    // TEMPERATURE
    // ---------------------------------------------

    start = data.indexOf("T=");

    if (start >= 0) {

      end = data.indexOf(',', start);

      if (end >= 0) {
        temperature = data.substring(start + 2, end);
      }
    }

    // ---------------------------------------------
    // HUMIDITY
    // ---------------------------------------------

    start = data.indexOf("H=");

    if (start >= 0) {

      end = data.indexOf(',', start);

      if (end >= 0) {
        humidity = data.substring(start + 2, end);
      }
    }

    // ---------------------------------------------
    // GAS
    // ---------------------------------------------

    start = data.indexOf("G=");

    if (start >= 0) {

      end = data.indexOf(',', start);

      if (end >= 0) {
        gas = data.substring(start + 2, end);
      }
    }

    // ---------------------------------------------
    // FLAME
    // ---------------------------------------------

    start = data.indexOf("F=");

    if (start >= 0) {

      end = data.indexOf(',', start);

      if (end >= 0) {
        flame = data.substring(start + 2, end);
      }
    }

    // ---------------------------------------------
    // ALARM
    // ---------------------------------------------

    start = data.indexOf("A=");

    if (start >= 0) {

      end = data.indexOf(',', start);

      if (end >= 0) {
        alarm = data.substring(start + 2, end);
      }
    }

    // ---------------------------------------------
    // BUTTON
    // ---------------------------------------------

    start = data.indexOf("B=");

    if (start >= 0) {

      end = data.indexOf(',', start);

      if (end >= 0) {
        button = data.substring(start + 2, end);
      }
    }

    // ---------------------------------------------
    // FAN (son alan, satir sonuna kadar)
    // ---------------------------------------------

    start = data.indexOf("FN=");

    if (start >= 0) {
      fan = data.substring(start + 3);
    }

    addLog(
      "PARSED -> T:" + temperature +
      " H:" + humidity +
      " G:" + gas +
      " F:" + flame +
      " BTN:" + button +
      " FAN:" + fan +
      " ALARM:" + alarm
    );
  }
}


// =================================================
// KOMUTU ARDUINO'YA GONDER
// =================================================

void sendCommandToArduino() {

  String line =
    "#F" + String(cmdFan) +
    ",M" + String(cmdMute) +
    ",G" + String(cmdGas) +
    ",L" + String(cmdFlame) +
    ",R" + String(cmdTempRise) +
    ",X" + String(cmdTempMax);

  arduinoSerial.println(line);

  addLog("CMD -> " + line);
}


// =================================================
// SEND DATA TO RENDER
// =================================================

void sendToServer() {

  if (WiFi.status() != WL_CONNECTED) {
    addLog("ERROR: WiFi disconnected");
    return;
  }

  // HTTPS client
  std::unique_ptr<BearSSL::WiFiClientSecure> client(
    new BearSSL::WiFiClientSecure
  );

  // Test/prototype icin certificate verification kapali
  client->setInsecure();

  HTTPClient https;

  if (!https.begin(*client, serverURL)) {
    addLog("ERROR: HTTPS connection failed");
    return;
  }

  https.addHeader("Content-Type", "application/json");
  https.addHeader("X-Auth", apiToken);

  // ---------------------------------------------
  // JSON
  // ---------------------------------------------

  String json = "{";

  json += "\"temperature\":";
  json += jsonNumber(temperature);
  json += ",";

  json += "\"humidity\":";
  json += jsonNumber(humidity);
  json += ",";

  json += "\"gas\":";
  json += jsonNumber(gas);
  json += ",";

  json += "\"flame\":";
  json += jsonNumber(flame);
  json += ",";

  json += "\"button\":";
  json += jsonNumber(button);
  json += ",";

  json += "\"fan\":";
  json += jsonNumber(fan);
  json += ",";

  json += "\"alarm\":";
  json += jsonNumber(alarm);

  json += "}";

  addLog("JSON: " + json);

  // ---------------------------------------------
  // POST
  // ---------------------------------------------

  int httpCode = https.POST(json);

  if (httpCode > 0) {

    addLog("HTTP CODE: " + String(httpCode));

    String response = https.getString();

    addLog("SERVER: " + response);

    // -------------------------------------------
    // Komut cevaba binmis halde geliyor:
    // {"success":true,"cmd":{"fan":0,"mute":0,"gt":400,"ft":80,"tr":5,"tm":45}}
    // -------------------------------------------

    if (httpCode == 200 && response.indexOf("\"cmd\"") >= 0) {

      int fanValue = extractInt(response, "fan", -1);
      int muteValue = extractInt(response, "mute", -1);
      int gasValue = extractInt(response, "gt", -1);
      int flameValue = extractInt(response, "ft", -1);
      int riseValue = extractInt(response, "tr", -1);
      int maxValue = extractInt(response, "tm", -1);

      if (fanValue >= 0 && muteValue >= 0 &&
          gasValue >= 0 && flameValue >= 0 &&
          riseValue >= 0 && maxValue >= 0) {

        cmdFan = fanValue;
        cmdMute = muteValue;
        cmdGas = gasValue;
        cmdFlame = flameValue;
        cmdTempRise = riseValue;
        cmdTempMax = maxValue;

        sendCommandToArduino();

      } else {
        addLog("CMD: eksik alan, gonderilmedi");
      }
    }

  } else {

    addLog("POST ERROR: " + https.errorToString(httpCode));
  }

  https.end();
}


// =================================================
// SETUP
// =================================================

void setup() {

  // USB serial
  Serial.begin(9600);

  // Arduino serial
  arduinoSerial.begin(9600);

  // Satir sonu gelmezse 1 saniye kilitlenmesin.
  arduinoSerial.setTimeout(200);

  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);

  delay(500);

  addLog("==========================");
  addLog("SMART SAFETY NODEMCU");
  addLog("==========================");

  // ---------------------------------------------
  // WIFI
  // ---------------------------------------------

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  addLog("Connecting to WiFi...");

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();

  addLog("WiFi CONNECTED!");
  addLog("IP: " + WiFi.localIP().toString());

  addLog("Waiting for Arduino data...");
}


// =================================================
// LOOP
// =================================================

void loop() {

  // Arduino'dan veri al
  readArduinoData();

  // Her 5 saniyede Render'a gonder
  if (millis() - lastSend >= 5000) {

    lastSend = millis();

    // Eger henuz veri alinmadiysa gonderme
    if (temperature != "--") {

      sendToServer();

    } else {

      addLog("Waiting for Arduino data...");
    }
  }

  // 1 saniyede bir LED
  if (millis() - lastBlink >= 1000) {

    lastBlink = millis();

    ledState = !ledState;

    digitalWrite(LED_BUILTIN, ledState);
  }
}
