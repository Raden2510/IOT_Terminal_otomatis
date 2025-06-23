#include <ESP8266WiFi.h>
#include <WiFiManager.h>         // https://github.com/tzapu/WiFiManager
#include <ESP8266WebServer.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <RTClib.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
RTC_DS3231 rtc;
ESP8266WebServer server(80);

const int relayPins[4] = {14, 12, 13, 15}; // D5=14, D6=12, D7=13, D8=15 pada ESP8266
int jadwalOn[4][2] = {{-1, -1}, {-1, -1}, {-1, -1}, {-1, -1}}; // jam, menit
int gasPin = A0;
int ambangAsap = 300;

bool wifiConnected = false;

void setup() {
  Serial.begin(115200);

  for (int i = 0; i < 4; i++) {
    pinMode(relayPins[i], OUTPUT);
    digitalWrite(relayPins[i], LOW); // relay OFF awal
  }

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
  }
  delay(1000);
}

void setupWiFi() {
  WiFiManager wm;
  wm.setConfigPortalTimeout(180); // timeout 3 menit

  if (wm.autoConnect("AutoTerminal")) {
    wifiConnected = true;
    String ipStr = WiFi.localIP().toString();
    Serial.println("WiFi Terhubung: " + ipStr);
    tampilStatus("WiFi OK:\n" + ipStr);
    delay(3000);
  } else {
    Serial.println("Gagal konek WiFi. Restart...");
    tampilStatus("WiFi Gagal!");
    delay(3000);
    ESP.restart();
  }
}

void setupWebServer() {
  server.on("/", []() {
    String html = "<h1>Kontrol Relay</h1>";
    for (int i = 0; i < 4; i++) {
      html += "Relay " + String(i + 1) + ": ";
      html += "<a href='/on?r=" + String(i) + "'>ON</a> ";
      html += "<a href='/off?r=" + String(i) + "'>OFF</a><br>";
    }

    html += "<h2>Jadwal Otomatis (HH:MM)</h2>";
    html += "<form method='GET' action='/set'>";
    for (int i = 0; i < 4; i++) {
      html += "Relay " + String(i + 1) + ": ";
      html += "<input name='r" + String(i) + "' type='time'><br>";
    }
    html += "<input type='submit' value='Simpan Jadwal'>";
    html += "</form>";

    server.send(200, "text/html", html);
  });

  server.on("/on", []() {
    int r = server.arg("r").toInt();
    if (r >= 0 && r < 4) digitalWrite(relayPins[r], HIGH);
    server.sendHeader("Location", "/");
    server.send(303);
  });

  server.on("/off", []() {
    int r = server.arg("r").toInt();
    if (r >= 0 && r < 4) digitalWrite(relayPins[r], LOW);
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
      digitalWrite(relayPins[i], HIGH);
    }
  }
}

void cekAsap() {
  int gasValue = analogRead(gasPin);
  if (gasValue > ambangAsap) {
    for (int i = 0; i < 4; i++) {
      digitalWrite(relayPins[i], LOW);
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
  display.print("Waktu: ");
  if (now.hour() < 10) display.print("0");
  display.print(now.hour()); display.print(":");
  if (now.minute() < 10) display.print("0");
  display.print(now.minute()); display.print(":");
  if (now.second() < 10) display.print("0");
  display.print(now.second());

  for (int i = 0; i < 4; i++) {
    display.setCursor(0, 16 + i * 10);
    display.print("Relay ");
    display.print(i + 1);
    display.print(": ");
    display.println(digitalRead(relayPins[i]) ? "ON" : "OFF");
  }

  display.setCursor(0, 56);
  display.print("IP: ");
  if (wifiConnected) {
    display.print(WiFi.localIP().toString());
  } else {
    display.print("192.168.4.1");
  }

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
