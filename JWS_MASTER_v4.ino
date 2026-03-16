#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266WebServer.h>
#include <Adafruit_NeoPixel.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <TimeLib.h>
#include <RTClib.h>
#include <PrayerTimes.h>
#include <Wire.h>
#include <SoftwareSerial.h>
#include <DFRobotDFPlayerMini.h>
#include <EEPROM.h>
#include <ArduinoJson.h>

// --- KONFIGURASI DATA ---
struct ConfigLoc {
  double lat;
  double lon;
  uint32_t valid;
  int iqomahDur[5];
  int ihtiyati[6];
};
ConfigLoc userLoc;

#define EEPROM_ADDR 0
#define EEPROM_VALID_KEY 12345
#define PIXEL_PER_SEGMENT 2
#define PIXEL_DIGITS 4
#define PIXEL_PIN D6
#define PIXEL_DASH 1
#define SDA_PIN D2
#define SCL_PIN D1
#define DEFAULT_PANEL_X 1

SoftwareSerial mp3Serial(D7, D5);
DFRobotDFPlayerMini myDFPlayer;
Adafruit_NeoPixel strip = Adafruit_NeoPixel((PIXEL_PER_SEGMENT * 7 * PIXEL_DIGITS) + (PIXEL_DASH * 2), PIXEL_PIN, NEO_GRB + NEO_KHZ800);

ESP8266WebServer server(80);
RTC_DS3231 rtc;
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "time.nist.gov", 25200, 60000);

const char* ssid_ntp = "error2";
const char* pass_ntp = "jangantanya";

int Hour = -1, Minute = -1, Second = -1;
double prayerTimes[7];
byte wheelPos = 0;
byte digits[] = { 0b1111110, 0b0011000, 0b0110111, 0b0111101, 0b1011001, 0b1101101, 0b1101111, 0b0111000, 0b1111111, 0b1111101 };
bool isAPMode = false;
bool waitingForAck = false;
unsigned long lastSendTime = 0;
int retryCount = 0;
bool adzanSedangDiputar = false;  // Flag untuk memantau status adzan
bool isIqomahMode = false;
int iqomahCountdown = 0;
const char* daftarPesan[] = {
  "SMART PEOPLE NEVER FEEL STRONGEST",
  "I CAN AND I WILL",
  "TEKNIK ELEKTRONIKA SMK - DISIPLIN & KREATIF",
  "BELAJARLAH DENGAN GIAT UNTUK MASA DEPAN",
  "KESEHATAN ADALAH KEKAYAAN TERBESAR",
  "KERJAKAN DENGAN HATI, HASIL AKAN MENGIKUTI"
};

// --- FUNGSI HELPER ---
void addMinutes(int& h, int& m, int offset) {
  m += offset;
  while (m >= 60) {
    m -= 60;
    h++;
  }
  while (m < 0) {
    m += 60;
    h--;
  }
  if (h >= 24) h = 0;
  if (h < 0) h = 23;
}

bool isTimeValid(DateTime dt) {
  return (dt.year() >= 2025);
}

void errorBlink(uint32_t color) {
  strip.fill(color);
  strip.show();
  delay(300);
  strip.clear();
  strip.show();
  delay(300);
}

void update_Prayers() {
  DateTime now = rtc.now();
  set_calc_method(Karachi);
  set_asr_method(Shafii);
  set_fajr_angle(20.0);
  set_isha_angle(18.0);
  get_prayer_times(now.year(), now.month(), now.day(), userLoc.lat, userLoc.lon, 7, prayerTimes);
}

void sendDataToSlave(int h, int m, int s) {
  DateTime now = rtc.now();
  int sh, sm, dh, dm, ah, am, mh, mm, ih, im;
  get_float_time_parts(prayerTimes[0], sh, sm);
  get_float_time_parts(prayerTimes[2], dh, dm);
  get_float_time_parts(prayerTimes[3], ah, am);
  get_float_time_parts(prayerTimes[5], mh, mm);
  get_float_time_parts(prayerTimes[6], ih, im);

  // GANTI BAGIAN INI: Gunakan nilai dari userLoc.ihtiyati
  addMinutes(sh, sm, userLoc.ihtiyati[1]);  // Subuh
  addMinutes(dh, dm, userLoc.ihtiyati[2]);  // Dzuhur
  addMinutes(ah, am, userLoc.ihtiyati[3]);  // Ashar
  addMinutes(mh, mm, userLoc.ihtiyati[4]);  // Maghrib
  addMinutes(ih, im, userLoc.ihtiyati[5]);  // Isya

  int imH = sh, imM = sm;
  addMinutes(imH, imM, -10 + (userLoc.ihtiyati[0] - userLoc.ihtiyati[1]));  // Imsak (relatif terhadap subuh)
  char content[150];
  sprintf(content, "%d,%d,%d,%d,%d,%d,%02d:%02d,%02d:%02d,%02d:%02d,%02d:%02d,%02d:%02d,%02d:%02d",
          h, m, s, now.day(), now.month(), now.year(),
          imH, imM, sh, sm, dh, dm, ah, am, mh, mm, ih, im);

  byte cs = 0;
  for (int i = 0; content[i] != '\0'; i++) { cs ^= content[i]; }
  Serial.print("<");
  Serial.print(content);
  Serial.print("*");
  Serial.print(cs);
  Serial.println(">");
  waitingForAck = true;
  lastSendTime = millis();
}

// --- WEB HANDLERS ---
void handleUpdateIqomah() {
  if (server.hasArg("s")) {
    ConfigLoc temp;
    EEPROM.get(EEPROM_ADDR, temp);
    temp.iqomahDur[0] = server.arg("s").toInt();
    temp.iqomahDur[1] = server.arg("d").toInt();
    temp.iqomahDur[2] = server.arg("a").toInt();
    temp.iqomahDur[3] = server.arg("m").toInt();
    temp.iqomahDur[4] = server.arg("i").toInt();

    // Hanya tulis jika ada perubahan data
    if (memcmp(&temp, &userLoc, sizeof(ConfigLoc)) != 0) {
      userLoc = temp;
      EEPROM.put(EEPROM_ADDR, userLoc);
      EEPROM.commit();
    }
    server.send(200, "text/plain", "OK");
  }
}

void handleUpdateMessage() {
  if (server.hasArg("msg")) {
    String m = server.arg("msg");
    m.replace(",", " ");  // PROTEKSI: Ganti koma dengan spasi agar parsing Slave tidak rusak
    if (m.length() > 100) m = m.substring(0, 100);
    m.toUpperCase();
    Serial.printf("<CONFIG,MSG,%s>\n", m.c_str());
    server.send(200, "text/plain", "OK");
  }
}

void handleUpdateLocation() {
  if (server.hasArg("Lat")) {
    userLoc.lat = server.arg("Lat").toDouble();
    userLoc.lon = server.arg("Lon").toDouble();
    EEPROM.put(EEPROM_ADDR, userLoc);
    EEPROM.commit();
    update_Prayers();
    server.send(200, "text/plain", "OK");
  }
}

void handleUpdateIhtiyati() {
  if (server.hasArg("i0")) {  // i0=imsak, i1=subuh, dst.
    userLoc.ihtiyati[0] = server.arg("i0").toInt();
    userLoc.ihtiyati[1] = server.arg("i1").toInt();
    userLoc.ihtiyati[2] = server.arg("i2").toInt();
    userLoc.ihtiyati[3] = server.arg("i3").toInt();
    userLoc.ihtiyati[4] = server.arg("i4").toInt();
    userLoc.ihtiyati[5] = server.arg("i5").toInt();
    EEPROM.put(EEPROM_ADDR, userLoc);
    EEPROM.commit();
    update_Prayers();  // Refresh jadwal
    server.send(200, "text/plain", "Ihtiyati Updated");
  }
}

void handleWifiConfig() {
  String html = "<html><body><h2>JWS WIFI SETTING</h2><form action='/setwifi'>SSID: <input name='ssid'><br>PASS: <input name='pass'><br><input type='submit' value='SIMPAN'></form></body></html>";
  server.send(200, "text/html", html);
}

void handleStatus() {
  DynamicJsonDocument doc(512);
  DateTime now = rtc.now();
  doc["time"] = now.timestamp();
  doc["lat"] = userLoc.lat;
  doc["lon"] = userLoc.lon;
  doc["ap"] = isAPMode;
  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

void handleSaveWifi() {
  server.send(200, "text/html", "Restarting...");
  delay(2000);
  ESP.restart();
}

void autoUpdateLocation() {
  if (WiFi.status() == WL_CONNECTED) {
    WiFiClient client;
    HTTPClient http;
    http.begin(client, "http://ip-api.com/json/");
    if (http.GET() > 0) {
      DynamicJsonDocument doc(1024);
      deserializeJson(doc, http.getString());
      if (doc["status"] == "success") {
        userLoc.lat = doc["lat"];
        userLoc.lon = doc["lon"];
        EEPROM.put(EEPROM_ADDR, userLoc);
        EEPROM.commit();
        update_Prayers();
      }
    }
    http.end();
  }
}

void handleRoot() {
  String html = "<html><head><meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<style>body{font-family:sans-serif;text-align:center;padding:20px;} input{width:100%;padding:10px;margin:10px 0;}</style></head>";
  html += "<body><h2>KIRIM PESAN JWS</h2>";
  html += "<form action='/set_msg'><input name='msg' placeholder='Tulis pesan di sini...'><br>";
  html += "<input type='submit' value='KIRIM KE PANEL'></form></body></html>";
  server.send(200, "text/html", html);
}

// --- PIXEL DISPLAY ---
uint32_t colorWheel(byte WheelPos) {
  WheelPos = 255 - WheelPos;
  if (WheelPos < 85) return strip.Color(255 - WheelPos * 3, 0, WheelPos * 3);
  if (WheelPos < 170) {
    WheelPos -= 85;
    return strip.Color(0, WheelPos * 3, 255 - WheelPos * 3);
  }
  WheelPos -= 170;
  return strip.Color(WheelPos * 3, 255 - WheelPos * 3, 0);
}

void writeDigitRainbow(int index, int val) {
  int margin = (index >= 2) ? (PIXEL_DASH * 2) : 0;
  int offset = index * (PIXEL_PER_SEGMENT * 7) + margin;
  byte digit = digits[val];
  for (int i = 0; i < 7; i++) {
    uint32_t segCol = colorWheel((wheelPos + (i * 15) + (index * 40)) & 255);
    if (digit & (0x40 >> i)) {
      for (int j = 0; j < PIXEL_PER_SEGMENT; j++) strip.setPixelColor(offset + (i * PIXEL_PER_SEGMENT) + j, segCol);
    }
  }
}

void checkTarhimAndSalam() {
  int waktuSholatIdx[] = { 0, 2, 3, 5, 6 };
  const char* nm[] = { "SUBUH", "DZUHUR", "ASHAR", "MAGRIB", "ISYA" };

  // Ambil status DFPlayer: 512 = Berhenti, 513 = Memutar
  int mp3Status = myDFPlayer.readState();

  for (int i = 0; i < 5; i++) {
    int sh, sm;
    get_float_time_parts(prayerTimes[waktuSholatIdx[i]], sh, sm);
    addMinutes(sh, sm, userLoc.ihtiyati[i + 1]);

    // --- LOGIKA 1: WAKTU ADZAN (PRIORITAS TERTINGGI) ---
    if (Hour == sh && Minute == sm && Second == 0) {
      isIqomahMode = true;
      iqomahCountdown = userLoc.iqomahDur[i] * 60;
      Serial.printf("<CONFIG,MSG,WAKTU %s TIBA>\n", nm[i]);

      myDFPlayer.stop();          // Hentikan apa pun (termasuk tarhim)
      delay(100);                 // Beri jeda modul bernapas
      myDFPlayer.play(2);         // Putar Adzan (File 0002.mp3)
      adzanSedangDiputar = true;  // <--- TAMBAHKAN INI
      return;                     // Keluar agar tidak menjalankan logika tarhim
    }

    // --- LOGIKA 2: WAKTU TARHIM (10 MENIT SEBELUM) ---
    int tarH = sh, tarM = sm;
    addMinutes(tarH, tarM, -10);

    if (Hour == tarH && Minute == tarM && Second == 0) {
      // Hanya putar jika tidak sedang memutar suara lain
      if (mp3Status != 513) {
        myDFPlayer.play(1);  // Putar Tarhim (File 0001.mp3)
      }
    }
  }
}

// --- SETUP & LOOP ---
void setup() {
  Serial.begin(9600);
  mp3Serial.begin(9600);
  EEPROM.begin(512);
  EEPROM.get(EEPROM_ADDR, userLoc);

  if (userLoc.valid != EEPROM_VALID_KEY) {
    userLoc.lat = -7.500701;
    userLoc.lon = 110.204332;
    userLoc.valid = EEPROM_VALID_KEY;
    for (int i = 0; i < 5; i++) userLoc.iqomahDur[i] = 10;
    for (int i = 0; i < 6; i++) userLoc.ihtiyati[i] = 2;
  }

  strip.begin();
  strip.setBrightness(120);
  strip.show();
  Wire.begin(SDA_PIN, SCL_PIN);
  if (rtc.begin()) {
    if (rtc.lostPower() || !isTimeValid(rtc.now())) rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  } else {
    for (int i = 0; i < 3; i++) errorBlink(strip.Color(255, 255, 0));
  }

  WiFi.mode(WIFI_AP_STA);
  // Nyalakan AP (WiFi HP) tanpa syarat agar selalu muncul
  WiFi.softAP("JWS-CONFIG", "12345678");
  Serial.println("WiFi AP JWS-CONFIG Aktif");
  WiFi.begin(ssid_ntp, pass_ntp);
  int attempt = 0;
  while (WiFi.status() != WL_CONNECTED && attempt < 20) {
    delay(500);
    attempt++;
    ESP.wdtFeed();
  }

  if (WiFi.status() == WL_CONNECTED) {
    autoUpdateLocation();
  }

  if (myDFPlayer.begin(mp3Serial)) myDFPlayer.volume(25);

  server.on("/", handleRoot);  // Tambahkan ini agar saat buka 192.168.4.1 tidak "Not Found"
  server.on("/set", handleUpdateLocation);
  server.on("/set_iqomah", handleUpdateIqomah);
  server.on("/set_ihtiyati", handleUpdateIhtiyati);
  server.on("/set_msg", handleUpdateMessage);
  server.on("/status", handleStatus);
  server.on("/config", handleWifiConfig);
  server.on("/setwifi", handleSaveWifi);
  server.begin();
  update_Prayers();
}

void loop() {
  ESP.wdtFeed();
  server.handleClient();
  DateTime now = rtc.now();

  if (WiFi.status() == WL_CONNECTED && timeClient.update()) {
    if (year(timeClient.getEpochTime()) > 2024) rtc.adjust(DateTime(timeClient.getEpochTime()));
  }

  if (Second != now.second()) {
    Second = now.second();
    Minute = now.minute();
    Hour = now.hour();

    // --- LOGIKA BRIGHTNESS SCHEDULING (MASTER) ---
    static int lastBright = -1;
    int currentBright = 40;  // Default kecerahan siang (50%)

    if (Hour >= 22 || Hour < 4) {
      currentBright = 5;  // Redup saat malam (jam 10 malam - 4 pagi)
    } else {
      currentBright = 40;  // Terang saat siang
    }

    // Hanya kirim data jika ada perubahan status kecerahan
    if (currentBright != lastBright) {
      lastBright = currentBright;
      Serial.printf("<CONFIG,BRIGHT,%d>\n", currentBright);
    }

    if (isIqomahMode) {
      if (iqomahCountdown > 0) {
        iqomahCountdown--;
        // Gunakan pesan tanpa spasi berlebih untuk stabilitas
        Serial.printf("<CONFIG,TYPE,IQOMAH %02d:%02d>\n", iqomahCountdown / 60, iqomahCountdown % 60);
      } else {
        isIqomahMode = false;
        Serial.println("<CONFIG,TYPE,LURUSKAN SHAF>");  // Ganti pesan saat iqomah habis
      }
      // CRITICAL: Jangan kirim data jam (sendDataToSlave) jika sedang mode Iqomah
      // agar Slave tidak dipaksa balik ke mode jam (mode 0) setiap detik.
    } else {
      sendDataToSlave(Hour, Minute, Second);  // Kirim data jam HANYA jika TIDAK sedang Iqomah
      checkTarhimAndSalam();
    }
    // Handle Serial ACK
    if (Serial.available()) {
      if (Serial.readStringUntil('\n').indexOf("ACK") >= 0) {
        waitingForAck = false;
        retryCount = 0;
      }
    }
    if (waitingForAck && (millis() - lastSendTime > 500) && retryCount < 3) {
      retryCount++;
      sendDataToSlave(Hour, Minute, Second);
    }

    // Update LED
    static unsigned long lastPixel = 0;
    if (millis() - lastPixel > 15) {
      wheelPos++;
      strip.clear();
      writeDigitRainbow(0, Hour / 10);
      writeDigitRainbow(1, Hour % 10);
      writeDigitRainbow(2, Minute / 10);
      writeDigitRainbow(3, Minute % 10);
      if (Second % 2 == 0) {
        for (int i = 0; i < 2; i++) strip.setPixelColor(28 + i, colorWheel(wheelPos + 128));
      }
      strip.show();
      lastPixel = millis();
      yield();
    }

    static unsigned long lastPesanOtomatis = 0;
    if (millis() - lastPesanOtomatis > 300000) {  // Setiap 5 Menit (300.000 ms)
      int randIdx = random(0, 6);
      Serial.printf("<CONFIG,TYPE,%s>\n", daftarPesan[randIdx]);
      lastPesanOtomatis = millis();
    }
  }
  // --- LOGIKA ANTREAN AUDIO (TARUH DI SINI) ---
  static unsigned long lastAudioCheck = 0;
  if (adzanSedangDiputar && millis() - lastAudioCheck > 2000) {  // Cek setiap 2 detik agar tidak membebani
    lastAudioCheck = millis();

    // Cek status DFPlayer: 512 artinya Sedang Berhenti/Idle
    if (myDFPlayer.readState() == 512) {
      adzanSedangDiputar = false;  // Reset flag
      delay(1000);                 // Jeda 1 detik setelah adzan selesai
      myDFPlayer.play(3);          // Putar Doa (File 0003.mp3)
      Serial.println("Adzan Selesai, Memutar Doa...");
    }
  }
}