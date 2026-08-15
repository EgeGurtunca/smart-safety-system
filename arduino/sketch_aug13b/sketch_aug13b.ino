#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <SoftwareSerial.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecureBearSSL.h>

// =================================================
// WIFI
// =================================================

// Kendi ağ bilgilerini gir
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

// =================================================
// RENDER API
// =================================================

const char* serverURL =
  "https://smart-safety-system-y11a.onrender.com/api/data";

// =================================================
// ARDUINO -> NODEMCU
// D6 = RX
// D7 = TX
// =================================================

SoftwareSerial arduinoSerial(D6, D7);

// =================================================
// LOCAL WEB SERVER
// =================================================

ESP8266WebServer server(80);

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
// SERIAL LOG
// =================================================

String serialLog = "";

unsigned long lastSend = 0;
unsigned long lastBlink = 0;

bool ledState = HIGH;


// =================================================
// LOG FUNCTION
// =================================================

void addLog(String text) {

  Serial.println(text);

  serialLog += text;
  serialLog += "\n";

  if (serialLog.length() > 6000) {

    serialLog =
      serialLog.substring(
        serialLog.length() - 6000
      );
  }
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

    addLog("ARDUINO: " + data);

    // ---------------------------------------------
    // Beklenen format:
    //
    // @T=25.9,H=74,G=76,F=160,A=0,FN=0
    // ---------------------------------------------

    if (!data.startsWith("@"))
      continue;

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

        temperature =
          data.substring(
            start + 2,
            end
          );
      }
    }

    // ---------------------------------------------
    // HUMIDITY
    // ---------------------------------------------

    start = data.indexOf("H=");

    if (start >= 0) {

      end = data.indexOf(',', start);

      if (end >= 0) {

        humidity =
          data.substring(
            start + 2,
            end
          );
      }
    }

    // ---------------------------------------------
    // GAS
    // ---------------------------------------------

    start = data.indexOf("G=");

    if (start >= 0) {

      end = data.indexOf(',', start);

      if (end >= 0) {

        gas =
          data.substring(
            start + 2,
            end
          );
      }
    }

    // ---------------------------------------------
    // FLAME
    // ---------------------------------------------

    start = data.indexOf("F=");

    if (start >= 0) {

      end = data.indexOf(',', start);

      if (end >= 0) {

        flame =
          data.substring(
            start + 2,
            end
          );
      }
    }

    // ---------------------------------------------
    // ALARM
    // ---------------------------------------------

    start = data.indexOf("A=");

    if (start >= 0) {

      end = data.indexOf(',', start);

      if (end >= 0) {

        alarm =
          data.substring(
            start + 2,
            end
          );
      }
    }

    // ---------------------------------------------
    // FAN
    // ---------------------------------------------

    start = data.indexOf("FN=");

    if (start >= 0) {

      fan =
        data.substring(
          start + 3
        );
    }

    addLog(
      "PARSED -> T:" + temperature +
      " H:" + humidity +
      " G:" + gas +
      " F:" + flame +
      " FAN:" + fan +
      " ALARM:" + alarm
    );
  }
}


// =================================================
// SEND DATA TO RENDER
// =================================================

void sendToServer() {

  if (WiFi.status() != WL_CONNECTED) {

    addLog("ERROR: WiFi disconnected");

    return;
  }

  addLog("Sending data to server...");

  // HTTPS client
  std::unique_ptr<BearSSL::WiFiClientSecure> client(
    new BearSSL::WiFiClientSecure
  );

  // Test/prototype için certificate verification kapalı
  client->setInsecure();

  HTTPClient https;

  if (!https.begin(*client, serverURL)) {

    addLog("ERROR: HTTPS connection failed");

    return;
  }

  https.addHeader(
    "Content-Type",
    "application/json"
  );

  // ---------------------------------------------
  // JSON
  // ---------------------------------------------

  String json = "{";

  json += "\"temperature\":";
  json += temperature;
  json += ",";

  json += "\"humidity\":";
  json += humidity;
  json += ",";

  json += "\"gas\":";
  json += gas;
  json += ",";

  json += "\"flame\":";
  json += flame;
  json += ",";

  json += "\"button\":";
  json += button;
  json += ",";

  json += "\"fan\":";
  json += fan;
  json += ",";

  json += "\"alarm\":";
  json += alarm;

  json += "}";

  addLog(
    "JSON: " + json
  );

  // ---------------------------------------------
  // POST
  // ---------------------------------------------

  int httpCode =
    https.POST(json);

  if (httpCode > 0) {

    addLog(
      "HTTP CODE: " +
      String(httpCode)
    );

    String response =
      https.getString();

    addLog(
      "SERVER: " +
      response
    );

  } else {

    addLog(
      "POST ERROR: " +
      https.errorToString(httpCode)
    );
  }

  https.end();
}


// =================================================
// LOCAL WEB PAGE
// =================================================

void handleRoot() {

  String page = "";

  page += "<!DOCTYPE html>";
  page += "<html>";
  page += "<head>";

  page +=
    "<meta name='viewport' "
    "content='width=device-width,initial-scale=1'>";

  page +=
    "<meta http-equiv='refresh' content='3'>";

  page +=
    "<title>Smart Safety System</title>";

  page += "<style>";

  page +=
    "body{font-family:Arial;"
    "background:#f4f4f4;"
    "margin:20px;"
    "text-align:center;}";

  page +=
    ".box{background:white;"
    "padding:15px;"
    "margin:10px auto;"
    "max-width:600px;"
    "border-radius:10px;}";

  page +=
    ".value{font-size:25px;"
    "font-weight:bold;}";

  page +=
    ".serial{background:#111;"
    "color:#00ff00;"
    "text-align:left;"
    "padding:15px;"
    "height:300px;"
    "overflow:auto;"
    "font-family:monospace;"
    "white-space:pre-wrap;"
    "border-radius:8px;}";

  page += "</style>";

  page += "</head>";
  page += "<body>";

  page +=
    "<h1>Smart Safety System</h1>";

  // ---------------------------------------------
  // TEMPERATURE
  // ---------------------------------------------

  page += "<div class='box'>";
  page += "<h2>Temperature</h2>";
  page += "<div class='value'>";
  page += temperature;
  page += " °C";
  page += "</div>";
  page += "</div>";

  // ---------------------------------------------
  // HUMIDITY
  // ---------------------------------------------

  page += "<div class='box'>";
  page += "<h2>Humidity</h2>";
  page += "<div class='value'>";
  page += humidity;
  page += " %";
  page += "</div>";
  page += "</div>";

  // ---------------------------------------------
  // GAS
  // ---------------------------------------------

  page += "<div class='box'>";
  page += "<h2>Gas</h2>";
  page += "<div class='value'>";
  page += gas;
  page += "</div>";
  page += "</div>";

  // ---------------------------------------------
  // FLAME
  // ---------------------------------------------

  page += "<div class='box'>";
  page += "<h2>Flame</h2>";
  page += "<div class='value'>";
  page += flame;
  page += "</div>";
  page += "</div>";

  // ---------------------------------------------
  // FAN
  // ---------------------------------------------

  page += "<div class='box'>";
  page += "<h2>Fan</h2>";
  page += "<div class='value'>";

  if (fan == "1")
    page += "ON";
  else
    page += "OFF";

  page += "</div>";
  page += "</div>";

  // ---------------------------------------------
  // ALARM
  // ---------------------------------------------

  page += "<div class='box'>";
  page += "<h2>Alarm</h2>";
  page += "<div class='value'>";

  if (alarm == "1")
    page += "ACTIVE";
  else
    page += "NORMAL";

  page += "</div>";
  page += "</div>";

  // ---------------------------------------------
  // SERIAL MONITOR
  // ---------------------------------------------

  page += "<div class='box'>";

  page +=
    "<h2>NodeMCU Serial Monitor</h2>";

  page += "<div class='serial'>";

  page += serialLog;

  page += "</div>";

  page += "</div>";

  page += "</body>";
  page += "</html>";

  server.send(
    200,
    "text/html",
    page
  );
}


// =================================================
// SETUP
// =================================================

void setup() {

  // USB serial
  Serial.begin(9600);

  // Arduino serial
  arduinoSerial.begin(9600);

  // Onboard LED
  pinMode(
    LED_BUILTIN,
    OUTPUT
  );

  digitalWrite(
    LED_BUILTIN,
    HIGH
  );

  delay(500);

  addLog("==========================");
  addLog("SMART SAFETY NODEMCU");
  addLog("==========================");

  // ---------------------------------------------
  // WIFI
  // ---------------------------------------------

  WiFi.mode(WIFI_STA);

  WiFi.begin(
    ssid,
    password
  );

  addLog("Connecting to WiFi...");

  while (
    WiFi.status() != WL_CONNECTED
  ) {

    delay(500);

    Serial.print(".");
  }

  Serial.println();

  addLog("WiFi CONNECTED!");

  addLog(
    "IP: " +
    WiFi.localIP().toString()
  );

  // ---------------------------------------------
  // LOCAL WEB SERVER
  // ---------------------------------------------

  server.on(
    "/",
    handleRoot
  );

  server.begin();

  addLog(
    "Local web server started"
  );

  addLog(
    "Waiting for Arduino data..."
  );
}


// =================================================
// LOOP
// =================================================

void loop() {

  // Arduino'dan veri al
  readArduinoData();

  // Local web server
  server.handleClient();

  // Her 5 saniyede Render'a gönder
  if (
    millis() - lastSend >= 5000
  ) {

    lastSend = millis();

    // Eğer henüz veri alınmadıysa gönderme
    if (temperature != "--") {

      sendToServer();

    } else {

      addLog(
        "Waiting for Arduino data..."
      );
    }
  }

  // 3 saniyede bir LED
  if (
    millis() - lastBlink >= 1000
  ) {

    lastBlink = millis();

    ledState = !ledState;

    digitalWrite(
      LED_BUILTIN,
      ledState
    );
  }
}