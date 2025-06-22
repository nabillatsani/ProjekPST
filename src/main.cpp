#include <Servo.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ESP8266WiFi.h>
#include <time.h>
#include <EEPROM.h>
#include <vector> // pastikan ini ada di paling atas
#include <stdint.h>

#define EEPROM_SIZE 512 // ukuran tergantung board, esp8266: 512 atau lebih
#define MAX_ENTRIES 50  // Bisa disesuaikan dengan kapasitas EEPROM

// --- Konfigurasi pin ---
const int motionPin = D7;
const int servoPin = D4;
const int trigPin = D0;
const int echoPin = D1;
const int buzzerPin = D2;

// --- Wifi Credentials ---
const char *ssid = "NUANSA";
const char *password = "dahlia2_nuansa";

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

struct Riwayat
{
  int id;
  char tanggal[11]; // "YYYY-MM-DD"
  char waktu[9];    // "HH:MM:SS"
} _attribute_((packed));

Riwayat riwayatEEPROM[MAX_ENTRIES];
int jumlahRiwayatEEPROM = 0;
int idCounter = 1;

std::vector<Riwayat> riwayatList;

float readUltrasonicDistance()
{
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);
  long duration = pulseIn(echoPin, HIGH, 30000);
  if (duration == 0)
    return 999;
  return duration * 0.0343 / 2;
}

void bukaPalang()
{
  for (int pos = 0; pos <= 180; pos += 5)
  {
    myServo.write(pos);
    delay(15);
  }
}

void tutupPalang()
{
  for (int pos = 180; pos >= 0; pos -= 5)
  {
    myServo.write(pos);
    delay(15);
  }
}

void simpanRiwayat(Riwayat data, int index)
{
  int base = index * sizeof(Riwayat);
  EEPROM.put(base, data);
  EEPROM.commit();
}

Riwayat bacaRiwayat(int index)
{
  Riwayat data;
  int base = index * sizeof(Riwayat);
  EEPROM.get(base, data);
  return data;
}

void tambahRiwayatEEPROM()
{
  if (jumlahRiwayatEEPROM >= MAX_ENTRIES)
  {
    // FIFO: Geser semua data satu ke bawah
    for (int i = 1; i < MAX_ENTRIES; i++)
    {
      riwayatEEPROM[i - 1] = riwayatEEPROM[i];
      simpanRiwayat(riwayatEEPROM[i - 1], i - 1);
    }
    jumlahRiwayatEEPROM = MAX_ENTRIES - 1;
  }

  Riwayat baru;
  baru.id = idCounter++;

  time_t now = time(nullptr);
  struct tm *timeinfo = localtime(&now);

  sprintf(baru.tanggal, "%04d-%02d-%02d", timeinfo->tm_year + 1900, timeinfo->tm_mon + 1, timeinfo->tm_mday);
  sprintf(baru.waktu, "%02d:%02d:%02d", timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);

  riwayatEEPROM[jumlahRiwayatEEPROM] = baru;
  simpanRiwayat(baru, jumlahRiwayatEEPROM);
  jumlahRiwayatEEPROM++;
}

// Tanpa pakai vector
void loadRiwayatDariEEPROM()
{
  // Asumsikan data dimulai dari index 0
  jumlahRiwayatEEPROM = 0;
  for (int i = 0; i < MAX_ENTRIES; i++)
  {
    Riwayat data = bacaRiwayat(i);
    if (data.id > 0)
    {
      riwayatEEPROM[jumlahRiwayatEEPROM++] = data;
      if (data.id >= idCounter)
        idCounter = data.id + 1;
    }
  }
  Serial.print("Jumlah data di EEPROM: ");
  Serial.println(jumlahRiwayatEEPROM);
}

void resetEEPROM()
{
  for (int i = 0; i < EEPROM_SIZE; i++)
  {
    EEPROM.write(i, 0);
  }
  EEPROM.commit();
  Serial.println("EEPROM reset to zero.");
}

// Fungsi untuk tukar posisi dua elemen Riwayat
void swapRiwayat(Riwayat &a, Riwayat &b)
{
  Riwayat temp = a;
  a = b;
  b = temp;
}

// Sorting array riwayatEEPROM berdasarkan id secara ascending (bubble sort sederhana)
void sortRiwayatById()
{
  for (int i = 0; i < jumlahRiwayatEEPROM - 1; i++)
  {
    for (int j = 0; j < jumlahRiwayatEEPROM - i - 1; j++)
    {
      if (riwayatEEPROM[j].id > riwayatEEPROM[j + 1].id)
      {
        swapRiwayat(riwayatEEPROM[j], riwayatEEPROM[j + 1]);
      }
    }
  }
}

void setupWiFi()
{
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConnected to WiFi");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());
}

void setupTime()
{
  configTime(7 * 3600, 0, "pool.ntp.org", "time.nist.gov"); // UTC+7 for WIB
  Serial.print("Menunggu sinkronisasi waktu");
  while (time(nullptr) < 100000)
  {
    Serial.print(".");
    delay(500);
  }
  Serial.println("\nWaktu tersinkronisasi.");
}

void setup()
{
  Serial.begin(115200);
  pinMode(motionPin, INPUT);
  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);
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

  motionTerdeteksiSebelumnya = (digitalRead(motionPin) == HIGH);

  EEPROM.begin(EEPROM_SIZE);
  loadRiwayatDariEEPROM();
  setupWiFi();
  setupTime();

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
            {
    String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <meta http-equiv='refresh' content='5'>
  <title>Admin Panel - Sistem Parkir</title>
  <style>
    body {
      font-family: Arial, sans-serif;
      background: #f4f6f8;
      margin: 0;
      padding: 20px;
    }
    .container {
      max-width: 900px;
      margin: auto;
      background: white;
      padding: 30px;
      border-radius: 8px;
      box-shadow: 0 0 10px rgba(0,0,0,0.1);
    }
    h2 {
      color: #333;
    }
    .status {
      padding: 10px;
      margin-bottom: 20px;
      border-radius: 5px;
      font-weight: bold;
    }
    .open {
      background-color: #d4edda;
      color: #155724;
    }
    .closed {
      background-color: #f8d7da;
      color: #721c24;
    }
    .info {
      margin: 10px 0;
    }
    table {
      width: 100%;
      border-collapse: collapse;
      margin-top: 20px;
    }
    table, th, td {
      border: 1px solid #ccc;
    }
    th {
      background-color: #007BFF;
      color: white;
    }
    th, td {
      padding: 10px;
      text-align: center;
    }
    @media (max-width: 600px) {
      table, thead, tbody, th, td, tr {
        display: block;
      }
      td {
        padding: 8px;
        text-align: right;
        position: relative;
      }
      td::before {
        position: absolute;
        left: 10px;
        content: attr(data-label);
        font-weight: bold;
        text-align: left;
      }
    }
  </style>
</head>
<body>
  <div class="container">
    <h2>Admin Panel - Sistem Parkir</h2>
    <div class="status %PALANG_CLASS%">
      Status Palang: %PALANG_STATUS%
    </div>
    <div class="info">
      <strong>Sensor Gerak:</strong> %SENSOR_STATUS% <br>
      <strong>Jarak Ultrasonik:</strong> %JARAK% cm
    </div>
    <button onclick="resetEEPROM()">Reset Data</button>
    <h3>Riwayat Kendaraan Masuk</h3>
    <table>
      <thead>
        <tr><th>Nomor</th><th>ID</th><th>Tanggal</th><th>Waktu</th></tr>
      </thead>
      <tbody>
        %RIWAYAT%
      </tbody>
    </table>
  </div>
</body>
</html>
<script>
  function resetEEPROM() {
    if (confirm("Yakin ingin mereset semua data?")) {
      fetch("/reset")
        .then(response => {
          if (response.ok) {
            alert("Data berhasil direset!");
            location.reload();
          } else {
            alert("Gagal mereset data.");
          }
        })
        .catch(error => {
          alert("Terjadi kesalahan koneksi.");
          console.error(error);
        });
    }
  }
</script>
)rawliteral";
  sortRiwayatById();

     String riwayatHtml = "";
  for (int i = 0; i < jumlahRiwayatEEPROM; i++) {
    riwayatHtml += "<tr>";
    riwayatHtml += "<td>" + String(i + 1) + "</td>";
    
    char idFormatted[4];
    sprintf(idFormatted, "%03d", riwayatEEPROM[i].id);
    riwayatHtml += "<td>" + String(idFormatted) + "</td>";
    
    riwayatHtml += "<td>" + String(riwayatEEPROM[i].tanggal) + "</td>";
    riwayatHtml += "<td>" + String(riwayatEEPROM[i].waktu) + "</td>";
    riwayatHtml += "</tr>";
  }

    String palangStatus = palangTerbuka ? "Terbuka" : "Tertutup";
    String palangClass = palangTerbuka ? "open" : "closed";
    String sensorStatus = (digitalRead(motionPin) == HIGH) ? "Terdeteksi" : "Tidak Terdeteksi";
    float jarak = readUltrasonicDistance();

    html.replace("%PALANG_STATUS%", palangStatus);
    html.replace("%PALANG_CLASS%", palangClass);
    html.replace("%SENSOR_STATUS%", sensorStatus);
    html.replace("%JARAK%", String(jarak, 1));
    html.replace("%RIWAYAT%", riwayatHtml);

    request->send(200, "text/html", html); });
  server.on("/bukapalang", HTTP_GET, [](AsyncWebServerRequest *request)
            {
  bukaPalang();  // Fungsi servo buka palang
  request->send(200, "text/plain", "Palang dibuka"); });

  server.on("/reset", HTTP_GET, [](AsyncWebServerRequest *request)
            {
  Serial.println("Reset data EEPROM diminta dari web");

  // Reset EEPROM: isi semua ke 0
  for (int i = 0; i < EEPROM.length(); i++) {
    EEPROM.write(i, 0);
  }
  EEPROM.commit();

  request->send(200, "text/plain", "EEPROM berhasil direset."); });

  server.begin();
  Serial.println("Web server started.");
}

void loop()
{
  int motion = digitalRead(motionPin);
  unsigned long sekarang = millis();

  if (motion == HIGH && !motionTerdeteksiSebelumnya &&
      (sekarang - waktuTerakhirGerak > jedaSensorGerak))
  {

    if (!palangTerbuka)
    {
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

      tambahRiwayatEEPROM();
    }
  }
  motionTerdeteksiSebelumnya = (motion == HIGH);

  if (palangTerbuka)
  {
    float distance = readUltrasonicDistance();
    Serial.print("Jarak ultrasonik: ");
    Serial.print(distance);
    Serial.println(" cm");

    if (distance > 0 && distance < 10.0)
    {
      objekTerdeteksi = true;
      waktuObjekTerakhirTerdeteksi = sekarang;
    }

    if (objekTerdeteksi && distance >= 15.0 && (sekarang - waktuObjekTerakhirTerdeteksi > jedaTungguObjekHilang))
    {
      Serial.println("Objek sudah pergi → Menutup palang");
      tutupPalang();
      palangTerbuka = false;
      objekTerdeteksi = false;

      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Selamat Datang");
    }
  }

  delay(100);
}