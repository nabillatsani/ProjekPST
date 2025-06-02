#include <Servo.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ESP8266WiFi.h>
#include <time.h>

// --- Konfigurasi pin ---
const int motionPin = D7;
const int servoPin = D4;
const int trigPin = D0;
const int echoPin = D1;
const int ledHijauPin = D3;
const int ledMerahPin = D2;
const int buzzerPin = D8;

// --- Wifi Credentials ---
const char* ssid = "Billa";
const char* password = "hahahahi";

Servo myServo;
LiquidCrystal_I2C lcd(0x27, 16, 2);

bool palangTerbuka = false;
bool motionTerdeteksiSebelumnya = false;

unsigned long waktuTerakhirGerak = 0;
const unsigned long jedaSensorGerak = 2000;

bool objekTerdeteksi = false;
unsigned long waktuObjekTerakhirTerdeteksi = 0;
const unsigned long jedaTungguObjekHilang = 1000;

AsyncWebServer server(80);

struct RiwayatKendaraan {
  int nomor;
  String id;
  String tanggal;
  String waktu;
};

const int maxRiwayat = 10;
RiwayatKendaraan riwayat[maxRiwayat];
int jumlahRiwayat = 0;

float readUltrasonicDistance() {
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);
  long duration = pulseIn(echoPin, HIGH, 30000);
  if (duration == 0) return 999;
  return duration * 0.0343 / 2;
}

void bukaPalang() {
  for (int pos = 0; pos <= 180; pos += 5) {
    myServo.write(pos);
    delay(15);
  }
}

void tutupPalang() {
  for (int pos = 180; pos >= 0; pos -= 5) {
    myServo.write(pos);
    delay(15);
  }
}

void tambahRiwayat(const String& kendaraanId) {
  if (jumlahRiwayat >= maxRiwayat) {
    for (int i = 1; i < maxRiwayat; i++) {
      riwayat[i - 1] = riwayat[i];
    }
    jumlahRiwayat = maxRiwayat - 1;
  }

  riwayat[jumlahRiwayat].nomor = jumlahRiwayat + 1;
  riwayat[jumlahRiwayat].id = kendaraanId;

  time_t now = time(nullptr);
  struct tm* timeinfo = localtime(&now);

  char bufferTanggal[11];
  char bufferWaktu[9];
  sprintf(bufferTanggal, "%04d-%02d-%02d", timeinfo->tm_year + 1900, timeinfo->tm_mon + 1, timeinfo->tm_mday);
  sprintf(bufferWaktu, "%02d:%02d:%02d", timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);

  riwayat[jumlahRiwayat].tanggal = String(bufferTanggal);
  riwayat[jumlahRiwayat].waktu = String(bufferWaktu);

  jumlahRiwayat++;
}

void setupWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConnected to WiFi");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());
}

void setupTime() {
  configTime(7 * 3600, 0, "pool.ntp.org", "time.nist.gov");  // UTC+7 for WIB
  Serial.print("Menunggu sinkronisasi waktu");
  while (time(nullptr) < 100000) {
    Serial.print(".");
    delay(500);
  }
  Serial.println("\nWaktu tersinkronisasi.");
}

void setup() {
  Serial.begin(115200);
  pinMode(motionPin, INPUT);
  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);
  pinMode(ledHijauPin, OUTPUT);
  pinMode(ledMerahPin, OUTPUT);
  pinMode(buzzerPin, OUTPUT);

  myServo.attach(servoPin);
  tutupPalang();
  palangTerbuka = false;

  Wire.begin(D6, D5);
  lcd.begin(16, 2);
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Selamat Datang");

  digitalWrite(ledHijauPin, LOW);
  digitalWrite(ledMerahPin, HIGH);

  motionTerdeteksiSebelumnya = (digitalRead(motionPin) == HIGH);

  setupWiFi();
  setupTime();

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    String html = "<!DOCTYPE html><html><head><title>Kontrol Palang</title>";
    html += "<meta http-equiv='refresh' content='5'>";
    html += "<style>table { border-collapse: collapse; } td, th { border: 1px solid black; padding: 5px; }</style>";
    html += "</head><body>";
    html += "<h2>Status Palang: ";
    html += (palangTerbuka ? "Terbuka" : "Tertutup");
    html += "</h2>";

    html += "<p>Sensor Gerak: ";
    html += (digitalRead(motionPin) == HIGH ? "Terdeteksi" : "Tidak");
    html += "</p>";

    float dist = readUltrasonicDistance();
    html += "<p>Jarak Ultrasonic: " + String(dist, 1) + " cm</p>";

    html += "<h3>Riwayat Kendaraan Masuk</h3>";
    html += "<table>";
    html += "<tr><th>Nomor</th><th>ID</th><th>Tanggal</th><th>Waktu</th></tr>";
    for (int i = 0; i < jumlahRiwayat; i++) {
      html += "<tr>";
      html += "<td>" + String(riwayat[i].nomor) + "</td>";
      html += "<td>" + riwayat[i].id + "</td>";
      html += "<td>" + riwayat[i].tanggal + "</td>";
      html += "<td>" + riwayat[i].waktu + "</td>";
      html += "</tr>";
    }
    html += "</table>";
    html += "</body></html>";
    request->send(200, "text/html", html);
  });

  server.begin();
  Serial.println("Web server started.");
}

void loop() {
  int motion = digitalRead(motionPin);
  unsigned long sekarang = millis();

  if (motion == HIGH && !motionTerdeteksiSebelumnya &&
      (sekarang - waktuTerakhirGerak > jedaSensorGerak)) {

    if (!palangTerbuka) {
      tone(buzzerPin, 1000);
      delay(100);
      noTone(buzzerPin);
      Serial.println("Buzzer aktif: gerakan terdeteksi saat palang tertutup");

      Serial.println("Gerakan terdeteksi → Membuka palang");
      bukaPalang();
      palangTerbuka = true;
      waktuTerakhirGerak = sekarang;

      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Silahkan Masuk");

      digitalWrite(ledHijauPin, HIGH);
      digitalWrite(ledMerahPin, LOW);

      tambahRiwayat("SENSOR");
    }
  }
  motionTerdeteksiSebelumnya = (motion == HIGH);

  if (palangTerbuka) {
    float distance = readUltrasonicDistance();
    Serial.print("Jarak ultrasonik: ");
    Serial.print(distance);
    Serial.println(" cm");

    if (distance > 0 && distance < 10.0) {
      objekTerdeteksi = true;
      waktuObjekTerakhirTerdeteksi = sekarang;
    }

    if (objekTerdeteksi && distance >= 15.0 &&
        (sekarang - waktuObjekTerakhirTerdeteksi > jedaTungguObjekHilang)) {
      Serial.println("Objek sudah pergi → Menutup palang");
      tutupPalang();
      palangTerbuka = false;
      objekTerdeteksi = false;

      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Selamat Datang");

      digitalWrite(ledMerahPin, HIGH);
      digitalWrite(ledHijauPin, LOW);
    }
  }

  delay(100);
}
