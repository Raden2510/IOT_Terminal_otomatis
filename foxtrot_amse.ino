#include <ESP8266WiFi.h>
#include <WiFiManager.h>
#include <ESP8266WebServer.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <RTClib.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define LED_HIJAU 0  // D3 (GPIO0)

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
RTC_DS3231 rtc;
ESP8266WebServer server(80);

const int relayPins[4] = {
  14, // D5 -> Relay 1 -> No. 1
  15, // D8 -> Relay 4 -> No. 2
  12, // D6 -> Relay 2 -> No. 3
  13  // D7 -> Relay 3 -> No. 4
};

int jadwalOn[4][2] = {{-1, -1}, {-1, -1}, {-1, -1}, {-1, -1}};
int gasPin = A0;
int ambangAsap = 300;
bool wifiConnected = false;

void setup() {
  Serial.begin(115200);

  for (int i = 0; i < 4; i++) {
    pinMode(relayPins[i], OUTPUT);
    digitalWrite(relayPins[i], HIGH); // OFF awal (aktif LOW)
  }

  pinMode(LED_HIJAU, OUTPUT);
  analogWriteFreq(1000); // Atur frekuensi PWM
  analogWrite(LED_HIJAU, 0); // LED mati awal

  Wire.begin();
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  rtc.begin();
  tampilStatus("Mencoba koneksi\nWiFi...\n192.168.4.1");

  setupWiFi();
  if (wifiConnected) {
    setupWebServer();
  }
}

void loop() {
  if (wifiConnected) {
    server.handleClient();
    tampilOLED();
    cekJadwal();
    cekAsap();
  } else {
    // Jika WiFi tidak terhubung, restart setiap 3 detik
    delay(3000);
    ESP.restart();
  }

  delay(1000);
}

void setupWiFi() {
  WiFiManager wm;
  wm.setConfigPortalTimeout(180);

  if (wm.autoConnect("AutoTerminal")) {
    wifiConnected = true;
    analogWrite(LED_HIJAU, 512); // 50% terang saat WiFi terhubung
    tampilStatus("WiFi OK:\n" + WiFi.localIP().toString());
    delay(3000);
  } else {
    analogWrite(LED_HIJAU, 0); // Mati jika gagal
    tampilStatus("WiFi Gagal!");
    delay(3000);
    wifiConnected = false;
  }
}

void setupWebServer() {
  server.on("/", []() {
    String html = R"rawliteral(
      <!DOCTYPE html>
      <html>
      <head>
        <title>Kontrol Relay</title>
        <style>
          body {
            font-family: Arial, sans-serif;
            background-color: #f0f4f8;
            color: #333;
            margin: 0;
            padding: 20px;
          }
          h1, h2 {
            text-align: center;
          }
          .relay-container {
            display: flex;
            flex-direction: column;
            align-items: center;
            gap: 10px;
            margin-bottom: 20px;
          }
          .relay-button {
            display: inline-block;
            padding: 10px 20px;
            margin: 5px;
            background-color: #007BFF;
            color: #fff;
            text-decoration: none;
            border-radius: 5px;
            font-weight: bold;
          }
          .relay-button:hover {
            background-color: #0056b3;
          }
          form {
            text-align: center;
            margin-top: 20px;
          }
          input[type='time'] {
            padding: 5px;
            margin: 5px;
            font-size: 14px;
          }
          input[type='submit'] {
            padding: 8px 16px;
            background-color: #28a745;
            color: white;
            border: none;
            border-radius: 4px;
            font-size: 16px;
            cursor: pointer;
          }
          input[type='submit']:hover {
            background-color: #218838;
          }
          .footer {
            margin-top: 30px;
            text-align: center;
            font-size: 12px;
            color: #777;
          }
        </style>
      </head>
      <body>
        <h1>Kontrol Relay (LOW = ON)</h1>
        <div class='relay-container'>
    )rawliteral";

    for (int i = 0; i < 4; i++) {
      html += "Relay " + String(i + 1) + ": ";
      html += "<a class='relay-button' href='/on?r=" + String(i) + "'>ON</a>";
      html += "<a class='relay-button' href='/off?r=" + String(i) + "'>OFF</a><br>";
    }

    html += R"rawliteral(
        </div>
        <h2>Jadwal Otomatis (HH:MM)</h2>
        <form method='GET' action='/set'>
    )rawliteral";

    for (int i = 0; i < 4; i++) {
      html += "Relay " + String(i + 1) + ": <input name='r" + String(i) + "' type='time'><br>";
    }

    html += R"rawliteral(
          <input type='submit' value='Simpan Jadwal'>
        </form>
        <div class='footer'>ESP8266 Web Controller</div>
      </body>
      </html>
    )rawliteral";

    server.send(200, "text/html", html);
  });

  server.on("/on", []() {
    int r = server.arg("r").toInt();
    if (r >= 0 && r < 4) digitalWrite(relayPins[r], LOW); // Aktif LOW
    server.sendHeader("Location", "/");
    server.send(303);
  });

  server.on("/off", []() {
    int r = server.arg("r").toInt();
    if (r >= 0 && r < 4) digitalWrite(relayPins[r], HIGH);
    server.sendHeader("Location", "/");
    server.send(303);
  });

  server.on("/set", []() {
    for (int i = 0; i < 4; i++) {
      if (server.hasArg("r" + String(i))) {
        String timeStr = server.arg("r" + String(i));
        if (timeStr.length() == 5 && timeStr.indexOf(':') == 2) {
          jadwalOn[i][0] = timeStr.substring(0, 2).toInt();
          jadwalOn[i][1] = timeStr.substring(3).toInt();
        }
      }
    }
    server.sendHeader("Location", "/");
    server.send(303);
  });

  server.begin();
}

void cekJadwal() {
  DateTime now = rtc.now();
  for (int i = 0; i < 4; i++) {
    if (jadwalOn[i][0] == now.hour() && jadwalOn[i][1] == now.minute()) {
      digitalWrite(relayPins[i], LOW); // Aktifkan relay (LOW = ON)
    }
  }
}

void cekAsap() {
  int gasValue = analogRead(gasPin);
  if (gasValue > ambangAsap) {
    for (int i = 0; i < 4; i++) {
      digitalWrite(relayPins[i], HIGH); // Matikan relay (HIGH = OFF)
    }
    Serial.println("ASAP TERDETEKSI! Semua relay OFF!");
  }
}

void tampilOLED() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);

  DateTime now = rtc.now();
  display.setCursor(0, 0);
  display.printf("Waktu: %02d:%02d:%02d\n", now.hour(), now.minute(), now.second());

  for (int i = 0; i < 4; i++) {
    display.setCursor(0, 16 + i * 10);
    display.print("Relay ");
    display.print(i + 1);
    display.print(": ");
    display.println(digitalRead(relayPins[i]) == LOW ? "ON" : "OFF");
  }

  display.setCursor(0, 56);
  display.print("IP: ");
  display.print(wifiConnected ? WiFi.localIP().toString() : "192.168.4.1");

  display.display();
}

void tampilStatus(String teks) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);

  int y = 0;
  int lastBreak = 0;
  for (int i = 0; i < teks.length(); i++) {
    if (teks[i] == '\n' || i - lastBreak > 18) {
      display.setCursor(0, y);
      display.println(teks.substring(lastBreak, i));
      lastBreak = i + 1;
      y += 10;
    }
  }
  if (lastBreak < teks.length()) {
    display.setCursor(0, y);
    display.println(teks.substring(lastBreak));
  }

  display.display();
}
