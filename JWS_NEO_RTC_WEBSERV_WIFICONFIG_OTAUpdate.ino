/*
  Project : JWS NeoPixel Standalone Version
  Author  : [mR sugA] - Modifikasi NeoPixel Only
  Update  : JWS NEO V11 Web Browser Setting - FAST LED - RTC - WIFI CONFIG - Update OTA
  Year    : 2026
*/

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <DNSServer.h>  // Tambahan Library Captive Portal
#define FASTLED_ALLOW_INTERRUPTS 0
#define FASTLED_INTERRUPT_RETRY_COUNT 0
#include <FastLED.h>
#include <EEPROM.h>
#include <PrayerTimes.h>
#include <Wire.h>
#include "RTClib.h"  // Tambahkan Library RTC
#include <ElegantOTA.h>

#define PIXEL_PIN D6
#define N_PER_SEG 1
#define N_DIGITS 4
#define N_DOTS 2
#define TOTAL_LED ((N_PER_SEG * 7 * N_DIGITS) + N_DOTS)

const byte DNS_PORT = 53;
DNSServer dnsServer;
const char* webPassword = "adminjws";

CRGB leds[TOTAL_LED];
ESP8266WebServer server(80);
RTC_DS3231 rtc;  // Deklarasi objek RTC

int Hour, Minute, Second;  // Variabel waktu global
int Day, Month, Year;
unsigned long lastUpdateRTC = 0;
int jamSholatAktual[5];
int menitSholatAktual[5];
int jamImsakAktual;
int menitImsakAktual;
byte wheelPos = 0;
double prayerTimes[7];

bool isIqomah = false;
int hitungMundurIqomah = 0;
bool isAdzan = false;
int hitungMundurAdzan = 0;
int durasiAdzan = 10;

// Variabel untuk siklus tampilan jadwal sholat di NeoPixel
unsigned long lastCycleTime = 0;
int displayMode = 0;  // 0: Jam, 1: Imsak, 2: Subuh, 3: Dzuhur, 4: Ashar, 5: Maghrib, 6: Isya

// --- STRUKTUR EEPROM ---
struct Config {
  uint32_t magic;
  double lat;
  double lon;
  int ikh[5];
  int bright;
  int iqomah;
  char msg[205];
  char namaMasjid[51];
  char apSSID[32];
  char apPass[32];
  byte volume;
} conf;

byte digits[] = {
  0b1111110, 0b0011000, 0b0110111, 0b0111101, 0b1011001,
  0b1101101, 0b1101111, 0b0111000, 0b1111111, 0b1111101
};

void readRTC() {
  DateTime now = rtc.now();
  Hour = now.hour();
  Minute = now.minute();
  Second = now.second();
  Day = now.day();
  Month = now.month();
  Year = now.year();
}

String dapatkanNamaWilayah(double lat, double lon) {
  if (abs(lat - (-7.646505)) < 0.01 && abs(lon - 110.352867) < 0.01) return "Turi, Sleman, DIY (Default)";
  if (abs(lat - (-7.8487)) < 0.01 && abs(lon - 110.9209) < 0.01) return "Wonogiri / Sukoharjo";
  if (abs(lat - (-7.5666)) < 0.01 && abs(lon - 110.8167) < 0.01) return "Surakarta (Solo)";
  if (abs(lat - (-7.5008)) < 0.01 && abs(lon - 110.2048) < 0.01) return "SMKN 1 Magelang";
  if (abs(lat - (-7.7956)) < 0.01 && abs(lon - 110.3695) < 0.01) return "Yogyakarta Kota";
  return "Koordinat Luar Jangkauan";
}

// --- KUMPULAN FUNGSI LOGIN & CAPTIVE PORTAL ---
bool isAuthorized() {
  if (server.hasHeader("Cookie")) {
    String cookie = server.header("Cookie");
    if (cookie.indexOf("JWS_SESSION=LOGGED_IN") != -1) return true;
  }
  return false;
}

void handleLogin() {
  if (isAuthorized()) {
    server.sendHeader("Location", "/setting");
    server.send(303);
    return;
  }
  String msg = "";
  if (server.hasArg("error")) {
    msg = "<p style='color:#ff6b6b; font-size:14px; margin-bottom:20px; font-weight:bold;'>&#9888; Password Salah!</p>";
  }
  String html = "<!DOCTYPE html><html lang='id'><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  html += "<title>Login JWS</title><style>";
  html += "body { margin: 0; padding: 0; font-family: 'Segoe UI', Tahoma, sans-serif; background: linear-gradient(135deg, #0f2027, #203a43, #2c5364); height: 100vh; display: flex; justify-content: center; align-items: center; color: #fff; }";
  html += ".card { background: rgba(255, 255, 255, 0.05); backdrop-filter: blur(10px); -webkit-backdrop-filter: blur(10px); padding: 40px 30px; border-radius: 20px; box-shadow: 0 15px 35px rgba(0,0,0,0.5); border: 1px solid rgba(255,255,255,0.1); width: 85%; max-width: 320px; text-align: center; }";
  html += "h3 { font-size: 13px; letter-spacing: 3px; color: #00e5ff; margin: 0 0 10px 0; font-weight: 600; text-transform: uppercase; }";
  html += "h1 { font-size: 22px; margin: 0 0 30px 0; font-weight: 700; line-height: 1.4; }";
  html += "input[type='password'] { width: 100%; padding: 15px; margin-bottom: 20px; border: none; border-radius: 30px; background: rgba(255,255,255,0.9); text-align: center; font-size: 16px; outline: none; box-sizing: border-box; box-shadow: inset 0 2px 5px rgba(0,0,0,0.2); transition: 0.3s; }";
  html += "input[type='password']:focus { background: #fff; box-shadow: 0 0 15px rgba(0, 229, 255, 0.5); }";
  html += ".btn { width: 100%; padding: 15px; border: none; border-radius: 30px; background: linear-gradient(90deg, #ff416c, #ff4b2b); color: white; font-size: 16px; font-weight: bold; letter-spacing: 1px; cursor: pointer; transition: 0.3s; box-shadow: 0 5px 15px rgba(255, 75, 43, 0.4); }";
  html += ".btn:hover { transform: translateY(-2px); box-shadow: 0 8px 20px rgba(255, 75, 43, 0.6); }";
  html += ".footer { margin-top: 35px; font-size: 11px; color: #8892b0; letter-spacing: 1px; text-transform: uppercase; line-height: 1.5; }</style></head><body>";
  html += "<div class='card'><h3>Selamat Datang</h3><h1>di SETTING JWS<br>BAKPAO PACMAN</h1>" + msg;
  html += "<form action='/auth' method='POST'><input type='password' name='password' placeholder='Masukkan Password Web' autocomplete='off'><button type='submit' class='btn'>LOGIN</button></form>";
  html += "<div class='footer'>created by<br><strong style='color:#fff; font-size:12px;'>SKANISA - ELEKTRONIKA</strong></div></div></body></html>";
  server.send(200, "text/html", html);
}

void handleAuth() {
  if (server.arg("password") == webPassword) {
    server.sendHeader("Set-Cookie", "JWS_SESSION=LOGGED_IN; Path=/; HttpOnly");
    server.sendHeader("Location", "/setting");
    server.send(303);
  } else {
    server.sendHeader("Location", "/login?error=1");
    server.send(303);
  }
}

void handleLogout() {
  server.sendHeader("Set-Cookie", "JWS_SESSION=0; Path=/; Expires=Thu, 01 Jan 1970 00:00:00 GMT");
  String html = "<!DOCTYPE html><html lang='id'><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1.0'><title>Logout</title>";
  html += "<style>body{background:linear-gradient(135deg, #0f2027, #203a43);height:100vh;display:flex;justify-content:center;align-items:center;color:#fff;text-align:center;font-family:sans-serif;} .card{background:rgba(255,255,255,0.05);padding:40px;border-radius:20px;} .btn{padding:12px;width:100%;border-radius:30px;background:#e74c3c;color:white;border:none;cursor:pointer;font-weight:bold;}</style></head><body>";
  html += "<div class='card'><h2>&#10004; LOGOUT BERHASIL</h2><p>Akses ke Setting JWS ditutup dengan aman.</p><button class='btn' onclick='window.open(\"\",\"_self\").close(); window.location.href=\"about:blank\";'>KELUAR & TUTUP</button></div></body></html>";
  server.send(200, "text/html", html);
}

void handleCaptivePortal() {
  String redirectUrl = "http://" + WiFi.softAPIP().toString() + "/login";
  server.sendHeader("Location", redirectUrl, true);
  server.send(302, "text/plain", "");
}
// ----------------------------------------------

void tampilkanDashboard(const char* header) {
  int pMap[] = { 0, 2, 3, 5, 6 };
  const char* nama[] = { "Subuh  ", "Dzuhur ", "Ashar  ", "Magrib ", "Isya'  " };

  Serial.println("\n-------------------------");
  Serial.println(header);
  Serial.println("-------------------------");
  Serial.printf("SSID WiFi    = %s\n", conf.apSSID);
  Serial.printf("Password     = %s\n", conf.apPass);
  Serial.printf("Wilayah      = %s\n", dapatkanNamaWilayah(conf.lat, conf.lon).c_str());
  Serial.printf("Latitude     = %.6f\n", conf.lat);
  Serial.printf("Longitude    = %.6f\n", conf.lon);
  Serial.printf("Jam Sekarang = %02d:%02d:%02d\n", Hour, Minute, Second);
  Serial.println("-------------------------");

  int hS, mS;
  get_float_time_parts(prayerTimes[0], hS, mS);

  int totalMenitSubuh = (hS * 60) + mS + conf.ikh[0];
  int totalMenitImsak = totalMenitSubuh - 10;
  int hI = (totalMenitImsak / 60) % 24;
  int mI = totalMenitImsak % 60;
  Serial.printf("Imsak   = %02d:%02d\n", hI, mI);

  for (int i = 0; i < 5; i++) {
    int h, m;
    get_float_time_parts(prayerTimes[pMap[i]], h, m);
    m += conf.ikh[i];
    if (m >= 60) {
      m -= 60;
      h++;
    }
    if (h >= 24) { h -= 24; }
    Serial.printf("%s = %02d:%02d\n", nama[i], h, m);
  }
  Serial.println("-------------------------");
}

void updatePrayers() {
  set_calc_method(Custom);
  set_asr_method(Shafii);
  set_fajr_angle(20.0);
  set_isha_angle(18.0);
  get_prayer_times(Year, Month, Day, conf.lat, conf.lon, 7, prayerTimes);

  // --- HITUNG DAN SIMPAN WAKTU AKTUAL SEKALI SAJA ---
  int pMap[] = { 0, 2, 3, 5, 6 };  // Subuh, Dzuhur, Ashar, Maghrib, Isya

  for (int i = 0; i < 5; i++) {
    int h, m;
    get_float_time_parts(prayerTimes[pMap[i]], h, m);

    // Tambah Ikhtiyati dan perbaikan Maghrib
    if (i == 3) {
      m += (conf.ikh[i] + 6);  // Khusus Maghrib
    } else {
      m += conf.ikh[i];  // Waktu lainnya
    }

    // Normalisasi format 60 Menit & 24 Jam
    while (m >= 60) {
      m -= 60;
      h++;
    }
    while (m < 0) {
      m += 60;
      h--;
    }
    if (h >= 24) h -= 24;
    if (h < 0) h += 24;

    jamSholatAktual[i] = h;
    menitSholatAktual[i] = m;
  }

  // Hitung Imsak Aktual (10 Menit sebelum Subuh Aktual)
  int mImsak = menitSholatAktual[0] - 10;
  int hImsak = jamSholatAktual[0];
  while (mImsak < 0) {
    mImsak += 60;
    hImsak--;
  }
  if (hImsak < 0) hImsak += 24;

  jamImsakAktual = hImsak;
  menitImsakAktual = mImsak;
}

void drawDigit(int index, int value, byte baseHue) {
  int offset = (index < 2) ? (index * 7 * N_PER_SEG) : (index * 7 * N_PER_SEG + N_DOTS);

  byte animasiHue = baseHue + (millis() / 15);

  for (int i = 0; i < 7; i++) {
    bool on = (digits[value] >> (6 - i)) & 1;

    // Gradasi per segment / garis
    byte segmentHue = animasiHue + ((index * 7 + i) * 15);

    for (int j = 0; j < N_PER_SEG; j++) {
      int ledIndex = offset + (i * N_PER_SEG) + j;
      if (on) leds[ledIndex] = CHSV(segmentHue, 255, 255);
      else leds[ledIndex] = CRGB::Black;
    }
  }
}

void drawDigitStatic(int index, int value, CRGB warna) {
  int offset = (index < 2) ? (index * 7 * N_PER_SEG) : (index * 7 * N_PER_SEG + N_DOTS);
  for (int i = 0; i < 7; i++) {
    bool on = (digits[value] >> (6 - i)) & 1;
    for (int j = 0; j < N_PER_SEG; j++) {
      int ledIndex = offset + (i * N_PER_SEG) + j;
      leds[ledIndex] = on ? warna : CRGB::Black;
    }
  }
}

void handleRoot() {
  if (!isAuthorized()) {
    server.sendHeader("Location", "/login");
    server.send(303);
    return;
  }
  // --- 1. AMBIL DATA JADWAL SHOLAT AKTUAL & JAM ---
  String jamSholat[5];
  for (int i = 0; i < 5; i++) {
    char buf[10];
    sprintf(buf, "%02d:%02d", jamSholatAktual[i], menitSholatAktual[i]);
    jamSholat[i] = String(buf);
  }

  char bufImsak[10];
  sprintf(bufImsak, "%02d:%02d", jamImsakAktual, menitImsakAktual);
  String jamImsak = String(bufImsak);

  // TAMPILAN JAM SEKARANG (WAKTU AWAL)
  char bufJam[10];
  sprintf(bufJam, "%02d:%02d:%02d", Hour, Minute, Second);
  String jamSkrg = String(bufJam);

  // --- 2. MULAI MEMBANGUN HALAMAN WEB (HTML & CSS) ---
  String s = F("<html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1.0'>\n");

  s.reserve(12000);  // Naikkan sedikit reserve memori untuk jaga-jaga

  s += F("<style>\n");
  s += F("body{font-family:'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; background:#f4f7f6; color:#333; padding:15px; margin:0;} \n");
  s += F(".card{background:#fff; padding:20px; border-radius:12px; box-shadow:0 4px 10px rgba(0,0,0,0.1); max-width:400px; margin:0 auto 20px auto;} \n");
  s += F(".title-header{text-align:center; color:#2c3e50; margin-top:0; border-bottom:2px solid #eee; padding-bottom:10px; font-size:20px;}\n");
  s += F("input, button, select{width:100%; margin:8px 0; padding:12px; border-radius:8px; border:1px solid #ddd; box-sizing:border-box; font-size:15px;} \n");
  s += F(".btn{background:#27ae60; color:#ffffff; font-weight:bold; border:none; padding:14px; border-radius:8px; cursor:pointer; font-size:16px; margin-top:15px;} \n");
  s += F(".btn:active{background:#2ecc71; transform:scale(0.98);} \n");
  s += F("label{font-size:13px; font-weight:bold; color:#666; margin-left:5px; display:block; margin-top:12px;}\n");
  s += F("input[type=range]{-webkit-appearance:none; height:8px; background:#ddd; border-radius:5px; outline:none; margin:15px 0;} \n");
  s += F("input[type=range]::-webkit-slider-thumb{-webkit-appearance:none; width:20px; height:20px; background:#4a90e2; border-radius:50%; cursor:pointer;} \n");

  // CSS Tampilan Waktu Sholat
  s += F(".jadwal-wrap{background:#f8f9fa; border-radius:8px; padding:10px; border:1px solid #eee;}\n");
  s += F(".waktu-sholat{display:flex; justify-content:space-between; padding:8px 5px; border-bottom:1px solid #e1e1e1; font-size:16px;} \n");
  s += F(".waktu-sholat:last-child{border-bottom:none;} \n");
  s += F(".nama-waktu{font-weight:600; color:#555;} \n");
  s += F(".jam-waktu{font-weight:bold; color:#2980b9;} \n");

  // CSS Mata Password & Tombol Help
  s += F(".pw-wrap { position: relative; }\n");
  s += F(".btn-eye { position: absolute; right: 12px; top: 18px; cursor: pointer; font-size: 18px; color: #7f8c8d; user-select: none; }\n");
  s += F(".section-title{font-size:14px; font-weight:bold; color:#2980b9; margin-top:20px; margin-bottom:5px; border-bottom:1px solid #ddd; padding-bottom:5px;}\n");
  s += F(".btn-help{background:#f39c12; color:#fff; font-weight:bold; border:none; padding:10px; border-radius:8px; cursor:pointer; font-size:14px; margin-bottom:15px;}\n");

  // CSS Popup Modal (Petunjuk Penggunaan)
  s += F(".modal{display:none; position:fixed; z-index:100; left:0; top:0; width:100%; height:100%; overflow:auto; background-color:rgba(0,0,0,0.6); backdrop-filter:blur(3px);}\n");
  s += F(".modal-content{background-color:#fff; margin:10% auto; padding:20px; border-radius:12px; width:85%; max-width:400px; box-shadow:0 5px 15px rgba(0,0,0,0.3); animation: fadeIn 0.3s;}\n");
  s += F("@keyframes fadeIn {from {opacity:0; transform:translateY(-20px);} to {opacity:1; transform:translateY(0);}}\n");
  s += F(".close{color:#aaa; float:right; font-size:28px; font-weight:bold; cursor:pointer; line-height:20px;} .close:hover{color:#333;}\n");

  // --- TAMBAHAN: CSS KHUSUS TOMBOL LOGOUT ---
  s += F(".btn-logout { display: block; width: 100%; max-width: 400px; margin: 0 auto 30px auto; padding: 15px 20px; border-radius: 12px; background: linear-gradient(90deg, #ff416c, #ff4b2b); color: white; font-size: 15px; font-weight: bold; text-decoration: none; text-align: center; letter-spacing: 1px; transition: 0.3s; box-shadow: 0 5px 15px rgba(255, 75, 43, 0.4); box-sizing: border-box; }\n");
  s += F(".btn-logout:hover { transform: translateY(-2px); box-shadow: 0 8px 20px rgba(255, 75, 43, 0.6); }\n");
  // ------------------------------------------

  s += F("</style></head>\n");

  s += F("<body>\n");

  // ================= KOTAK 1: TAMPILAN JADWAL SHOLAT =================
  s += F("<div class='card'>\n");
  s += F("<h3 class='title-header'>🕋 Dashboard JWS-NEO</h3>\n");

  // TOMBOL PETUNJUK PENGGUNAAN
  s += F("<button type='button' class='btn-help' onclick='showHelp()'>ℹ️ Baca Petunjuk Penggunaan</button>\n");
  s += F("<div class='jadwal-wrap'>\n");

  // Waktu Live Jam Alat
  s += "<div class='waktu-sholat' style='background:#d1ecf1; border-radius:5px; padding:10px; margin-bottom:10px;'><span class='nama-waktu' style='color:#0c5460;'>JAM ALAT</span><span id='liveJam' class='jam-waktu' style='color:#0c5460; font-size:18px;'>" + jamSkrg + "</span></div>\n";

  s += "<div class='waktu-sholat'><span class='nama-waktu'>Imsak</span><span class='jam-waktu'>" + jamImsak + "</span></div>\n";
  s += "<div class='waktu-sholat'><span class='nama-waktu'>Subuh</span><span class='jam-waktu'>" + jamSholat[0] + "</span></div>\n";
  s += "<div class='waktu-sholat'><span class='nama-waktu'>Dzuhur</span><span class='jam-waktu'>" + jamSholat[1] + "</span></div>\n";
  s += "<div class='waktu-sholat'><span class='nama-waktu'>Ashar</span><span class='jam-waktu'>" + jamSholat[2] + "</span></div>\n";
  s += "<div class='waktu-sholat'><span class='nama-waktu'>Maghrib</span><span class='jam-waktu'>" + jamSholat[3] + "</span></div>\n";
  s += "<div class='waktu-sholat'><span class='nama-waktu'>Isya'</span><span class='jam-waktu'>" + jamSholat[4] + "</span></div>\n";
  s += F("</div></div>\n");

  // ================= KOTAK 2: FORM PENGATURAN =================
  s += F("<div class='card'>\n");
  s += F("<h3 class='title-header'>⚙️ Pengaturan Alat</h3>\n");
  s += F("<form action='/save' method='POST' id='f'>\n");
  s += F("<input type='hidden' name='tm' id='tm'>\n");

  // --- IKHTIYATI ---
  s += F("<div class='section-title' style='margin-top:0;'>KOREKSI WAKTU (IKHTIYATI)</div>\n");
  s += F("<label style='margin-top:5px;'>Penambahan/Pengurangan Menit</label>\n");
  s += F("<div style='display:flex; gap:8px; text-align:center; margin-top:5px; margin-bottom:15px;'>\n");
  const char* lbl[] = { "Sub", "Dzu", "Ash", "Mag", "Isy" };
  for (int i = 0; i < 5; i++) {
    s += F("<div style='flex:1; background:#f0f3f4; padding:5px; border-radius:5px;'>\n");
    s += "<span style='font-size:11px; font-weight:bold; color:#34495e; display:block;'>" + String(lbl[i]) + "</span>\n";
    s += "<input type='number' name='i" + String(i) + "' value='" + String(conf.ikh[i]) + "' style='padding:8px 2px; text-align:center; margin:5px 0 0 0;'>\n";
    s += F("</div>\n");
  }
  s += F("</div>\n");

  // --- PENGATURAN WIFI ---
  s += F("<div class='section-title'>PENGATURAN AKSES WIFI</div>\n");
  s += F("<label>Nama WiFi (SSID)</label>\n");
  s += F("<div style='font-size:11px; color:#7f8c8d; margin-bottom:5px; margin-left:5px;'>Maksimal 30 karakter.</div>\n");
  s += "<input name='ssid' maxlength='30' placeholder='Nama WiFi JWS' value='" + String(conf.apSSID) + "'>\n";

  s += F("<label>Password WiFi</label>\n");
  s += F("<div style='font-size:11px; color:#e74c3c; margin-bottom:5px; margin-left:5px;'><b>WAJIB:</b> Minimal 8 karakter!</div>\n");

  s += F("<div class='pw-wrap'>\n");
  s += "<input name='pass' id='pw' type='password' minlength='8' maxlength='30' placeholder='Password WiFi' value='" + String(conf.apPass) + "'>\n";
  s += F("<span class='btn-eye' onclick='togglePass()'>&#128065;</span>\n");
  s += F("</div>\n");

  // --- LOGIKA MENGUNCI DROPDOWN PRESET ---
  String strLat = String(conf.lat, 4);
  String strLon = String(conf.lon, 4);
  String curPos = strLat + "," + strLon;

  // Cek apakah kordinat saat ini sama dengan salah satu preset
  bool isPreset = (curPos == "-7.5666,110.8167" || curPos == "-7.7956,110.3695" || curPos == "-7.8487,110.9209" || curPos == "-7.5008,110.2048" || curPos == "-7.6482,110.3518");

  s += F("<label>Pilih Preset Wilayah</label>\n");
  s += F("<select id='preset' onchange='isiKoordinat()'>\n");
  s += F("<option value=''>-- Pilih Kota (Opsional/Manual) --</option>\n");

  // Daftar Kota (hanya 1 kali dicetak)
  s += "<option value='-7.5666,110.8167'" + String(curPos == "-7.5666,110.8167" ? " selected" : "") + ">Surakarta (Solo)</option>\n";
  s += "<option value='-7.7956,110.3695'" + String(curPos == "-7.7956,110.3695" ? " selected" : "") + ">Yogyakarta</option>\n";
  s += "<option value='-7.8487,110.9209'" + String(curPos == "-7.8487,110.9209" ? " selected" : "") + ">Wonogiri / Sukoharjo</option>\n";
  s += "<option value='-7.5008,110.2048'" + String(curPos == "-7.5008,110.2048" ? " selected" : "") + ">SMKN 1 Magelang</option>\n";
  s += "<option value='-7.6482,110.3518'" + String(curPos == "-7.6482,110.3518" ? " selected" : "") + ">Turi, Sleman</option>\n";

  // Jika posisi tersimpan BUKAN preset (hasil GPS/Manual sblmnya), tambahkan opsi khusus
  if (!isPreset && conf.lat != 0.0) {
    s += "<option value='" + curPos + "' selected>📍 Lokasi Kustom / GPS Tersimpan</option>\n";
  }

  s += F("</select>\n");

  s += "<label>Latitude</label><input name='lat' id='lat' value='" + String(conf.lat, 6) + "'>\n";
  s += "<label>Longitude</label><input name='lon' id='lon' value='" + String(conf.lon, 6) + "'>\n";
  s += F("<div style='font-size:11px; color:#e74c3c; margin-bottom:2px; margin-top:10px; margin-left:5px;'><i>*Pastikan GPS/Lokasi HP aktif sebelum klik tombol.</i></div>\n");

  // Tombol dengan ID btnGps
  s += F("<button type='button' id='btnGps' onclick='autoGet()' style='background:#8e44ad; color:white; border:none; padding:10px; border-radius:8px; cursor:pointer; font-size:14px; margin-bottom:15px; margin-top:2px;'>📍 Gunakan GPS HP Saat Ini</button>\n");

  s += F("<label>Durasi Jeda Iqomah (Menit)</label>\n");
  s += "<input type='number' name='iqm' min='1' max='20' value='" + String(conf.iqomah) + "'>\n";

  // --- TOMBOL TEST AUDIO MANUAL ---
  s += F("<div class='section-title'>TEST AUDIO MANUAL</div>\n");
  s += F("<div style='display:flex; gap:5px; margin-top:10px;'>\n");
  s += F("<button type='button' onclick=\"fetch('/test-tarhim')\" style='flex:1; padding:10px; background:#3498db; color:white; border:none; border-radius:5px; cursor:pointer;'>Play Tarhim</button>\n");
  s += F("<button type='button' onclick=\"fetch('/test-adzan')\" style='flex:1; padding:10px; background:#2ecc71; color:white; border:none; border-radius:5px; cursor:pointer;'>Play Adzan</button>\n");
  s += F("<button type='button' onclick=\"fetch('/stop-audio')\" style='flex:1; padding:10px; background:#e74c3c; color:white; border:none; border-radius:5px; cursor:pointer;'>Stop Suara</button>\n");
  s += F("</div>\n");

  // --- SLIDER VOLUME AUDIO ---
  s += F("<label>Volume Audio DFPlayer: <span id='valVol' style='color:#e67e22;'>");
  s += String(conf.volume);
  s += F("</span></label>\n");
  s += "<input type='range' name='vol' min='0' max='30' value='" + String(conf.volume) + "' oninput='updateVol(this.value)'>\n";

  s += F("<label>Kecerahan LED NeoPixel: <span id='valBr' style='color:#2980b9;'>");
  s += String(conf.bright);
  s += F("</span></label>\n");
  s += "<input type='range' name='br' min='5' max='255' value='" + String(conf.bright) + "' oninput='updateVal(this.value)'>\n";

  s += F("<p style='font-size:12px; color:#e74c3c; text-align:center; font-weight:bold; margin-top:20px;'>Wifi akan terputus sesaat jika SSID/Pass diubah.</p>\n");

  s += F("<button type='button' class='btn' onclick='sub()'>💾 SIMPAN & UPDATE</button>\n");
  s += F("</form></div>\n");

  // --- TAMBAHAN: TOMBOL LOGOUT ---
  s += F("<a href='/logout' class='btn-logout'>&#10006; LOGOUT KELUAR</a>\n");
  // -------------------------------

  // ================= KOTAK POPUP: PETUNJUK PENGGUNAAN =================
  s += F("<div id='helpModal' class='modal'>\n");
  s += F("<div class='modal-content' style='max-height:85vh; overflow-y:auto;'>\n");
  s += F("<span class='close' onclick='closeHelp()'>&times;</span>\n");
  s += F("<h3 style='margin-top:0; color:#2980b9; border-bottom:2px solid #eee; padding-bottom:10px;'>📖 Petunjuk Setting JWS</h3>\n");

  // DAFTAR LANGKAH-LANGKAH (Ordered List)
  s += F("<ol style='padding-left:20px; font-size:13.5px; color:#444; line-height:1.6; margin-top:15px;'>\n");
  s += F("<li style='margin-bottom:6px;'>Jika ingin menggunakan tombol <b>Gunakan GPS HP</b>, pastikan fitur <b>Lokasi/GPS di HP Anda sudah diaktifkan</b> terlebih dahulu.</li>\n");
  s += F("<li style='margin-bottom:6px;'>Wajib mengganti <b>SSID</b> dan <b>PASSWORD</b> demi keamanan.</li>\n");
  s += F("<li style='margin-bottom:6px;'>SSID maksimal 30 karakter, PASSWORD minimal 8 karakter.</li>\n");
  s += F("<li style='margin-bottom:6px;'>Pilih lokasi pada list. Jika tidak ada, isi <b>Latitude</b> dan <b>Longitude</b> pada tempat yang tersedia.</li>\n");
  s += F("<li style='margin-bottom:6px;'>Sesuaikan <b>Nama Masjid</b> dengan lokasi.</li>\n");
  s += F("<li style='margin-bottom:6px;'>Atur durasi hitung mundur <b>Iqomah</b>.</li>\n");
  s += F("<li style='margin-bottom:6px;'>Atur kecerahan pada <b>NeoPixel</b> (disarankan minimal 80).</li>\n");
  s += F("<li style='margin-bottom:6px;'>Klik <b>Simpan</b> perubahan yang sudah dilakukan.</li>\n");
  s += F("<li style='margin-bottom:6px;'>Silakan mengisi pesan agar tampil pada <b>Running Text</b> selama 1x24 jam.</li>\n");
  s += F("</ol>\n");

  // KOTAK PERINGATAN (KUNING)
  s += F("<div style='background:#fcf3cf; padding:10px; border-left:4px solid #f1c40f; border-radius:4px; font-size:13px; color:#555; margin-bottom:12px;'>\n");
  s += F("<b style='color:#b7950b;'>⚠️ INGAT !!</b><br>Setelah melakukan perubahan, WIFI akan restart. Silahkan koneksikan kembali HP Anda dengan WIFI setting baru.\n");
  s += F("</div>\n");

  // TEKS IKHTIYATI
  s += F("<p style='font-size:13.5px; color:#444; line-height:1.5; margin-bottom:10px;'>\n");
  s += F("Silahkan sesuaikan <b>Ikhtiyati</b> mengikuti kebijakan takmir masjid, lalu simpan lagi perubahan yang dilakukan.<br>\n");
  s += F("<i style='color:#e74c3c;'>*Perubahan yang kedua tidak akan merubah setting sebelumnya.</i>\n");
  s += F("</p>\n");

  // KOTAK KONTAK (HIJAU)
  s += F("<div style='background:#d4efdf; padding:10px; border-left:4px solid #27ae60; border-radius:4px; font-size:13px; color:#555; margin-bottom:15px;'>\n");
  s += F("<b style='color:#1e8449;'>📞 Butuh Bantuan?</b><br>Jika mengalami kendala setting atau menghendaki perubahan tampilan, silahkan hubungi CP: <b>08123456789</b>\n");
  s += F("</div>\n");

  // TOMBOL TUTUP
  s += F("<button type='button' class='btn' onclick='closeHelp()' style='background:#e74c3c; width:100%; margin-top:5px;'>Tutup Panduan</button>\n");
  s += F("</div></div>\n");

  // --- SCRIPT JS (DIJAMIN AMAN & JALAN) ---
  s += F("<script>\n");

  // Kirim string jamSkrg lalu pecah di JS (Menghindari syntax error pada C++)
  s += "var timeArr = '" + jamSkrg + "'.split(':');\n";
  s += F("var jam = parseInt(timeArr[0], 10);\n");
  s += F("var mnt = parseInt(timeArr[1], 10);\n");
  s += F("var dtk = parseInt(timeArr[2], 10);\n");

  s += F("setInterval(function(){\n");
  s += F("  dtk++; if(dtk>=60){dtk=0; mnt++;} if(mnt>=60){mnt=0; jam++;} if(jam>=24){jam=0;}\n");
  s += F("  var sJam = (jam<10?'0':'')+jam, sMnt = (mnt<10?'0':'')+mnt, sDtk = (dtk<10?'0':'')+dtk;\n");
  s += F("  document.getElementById('liveJam').innerHTML = sJam + ':' + sMnt + ':' + sDtk;\n");
  s += F("}, 1000);\n");

  s += F("function togglePass(){ var x=document.getElementById('pw'); if(x.type==='password'){x.type='text';}else{x.type='password';} }\n");
  s += F("function updateVal(v){ document.getElementById('valBr').innerHTML = v; }\n");
  s += F("function isiKoordinat(){ var p=document.getElementById('preset').value; if(p!=''){ var k=p.split(','); document.getElementById('lat').value=k[0]; document.getElementById('lon').value=k[1]; }}\n");

  // Pembungkus C++ yang benar untuk Javascript autoGet()
  s += F("function autoGet() { \n");
  s += F("  document.getElementById('btnGps').innerHTML = '⏳ Meminta akses dari HP...'; \n");

  // 1. KITA LANGSUNG PAKSA GANTI JUDUL HALAMAN WEB
  s += F("  document.title = 'REQ_GPS'; \n");

  // 2. KITA TETAP COBA FITUR APP INVENTOR JAGA-JAGA JIKA SUPPORT
  s += F("  try { window.AppInventor.setWebViewString('REQ_GPS'); } catch(e) {} \n");

  // 3. TAMBAHKAN TIMER JIKA DALAM 15 DETIK TIDAK ADA RESPON DARI APP INVENTOR
  s += F("  setTimeout(function() { \n");
  s += F("     if (document.getElementById('btnGps').innerHTML.includes('Meminta')) { \n");
  s += F("         alert('Gagal mengambil kordinat. Pastikan GPS aktif dan Anda menggunakan Aplikasi Android JWS.'); \n");
  s += F("         document.getElementById('btnGps').innerHTML = '📍 Gunakan GPS HP Saat Ini'; \n");
  s += F("     } \n");
  s += F("  }, 15000); \n");
  s += F("}\n");

  s += F("function setGPSFromApp(lat, lon) { \n");
  s += F("    document.getElementById('lat').value = lat; \n");
  s += F("    document.getElementById('lon').value = lon; \n");
  s += F("    var sel = document.getElementById('preset'); \n");
  s += F("    var opt = document.getElementById('optTempGps'); \n");
  s += F("    if(!opt) { \n");
  s += F("      opt = document.createElement('option'); \n");
  s += F("      opt.id = 'optTempGps'; \n");
  s += F("      sel.add(opt); \n");
  s += F("    } \n");
  s += F("    opt.value = lat + ',' + lon; \n");
  s += F("    opt.innerHTML = '📡 Titik GPS HP Saat Ini'; \n");
  s += F("    opt.selected = true; \n");
  s += F("    document.getElementById('btnGps').innerHTML = '📍 Gunakan GPS HP Saat Ini'; \n");
  s += F("    alert('Sukses! Titik kordinat berhasil ditarik dari HP.'); \n");
  s += F("}\n");

  s += F("function showHelp(){ document.getElementById('helpModal').style.display='block'; }\n");
  s += F("function closeHelp(){ document.getElementById('helpModal').style.display='none'; }\n");
  s += F("window.onclick = function(event){ if(event.target == document.getElementById('helpModal')) closeHelp(); }\n");

  s += F("function sub(){ var d=new Date(); document.getElementById('tm').value=d.getHours()+':'+d.getMinutes()+':'+d.getSeconds(); document.getElementById('f').submit(); }\n");
  s += F("function updateVol(v){ document.getElementById('valVol').innerHTML = v; fetch('/set-volume?v=' + v); }\n");
  s += F("</script></body></html>\n");

  server.send(200, "text/html", s);
}

void handleSave() {
  bool restartWiFi = false;

  if (server.hasArg("ssid") && server.arg("ssid") != "") {
    String nSSID = server.arg("ssid");
    nSSID.replace("+", " ");
    if (String(conf.apSSID) != nSSID) restartWiFi = true;
    memset(conf.apSSID, 0, sizeof(conf.apSSID));
    strncpy(conf.apSSID, nSSID.c_str(), 31);
  }

  if (server.hasArg("pass") && server.arg("pass").length() >= 8) {
    String nPass = server.arg("pass");
    nPass.replace("+", " ");
    if (String(conf.apPass) != nPass) restartWiFi = true;
    memset(conf.apPass, 0, sizeof(conf.apPass));
    strncpy(conf.apPass, nPass.c_str(), 31);
  }

  if (server.hasArg("tm")) {
    String t = server.arg("tm");
    int c1 = t.indexOf(':'), c2 = t.lastIndexOf(':');
    if (c1 != -1 && c2 != -1) {
      Hour = t.substring(0, c1).toInt();
      Minute = t.substring(c1 + 1, c2).toInt();
      Second = t.substring(c2 + 1).toInt();
      rtc.adjust(DateTime(Year, Month, Day, Hour, Minute, Second));
    }
  }

  if (server.hasArg("lat") && server.arg("lat") != "") conf.lat = server.arg("lat").toDouble();
  if (server.hasArg("lon") && server.arg("lon") != "") conf.lon = server.arg("lon").toDouble();
  if (server.hasArg("i0")) conf.ikh[0] = server.arg("i0").toInt();
  if (server.hasArg("i1")) conf.ikh[1] = server.arg("i1").toInt();
  if (server.hasArg("i2")) conf.ikh[2] = server.arg("i2").toInt();
  if (server.hasArg("i3")) conf.ikh[3] = server.arg("i3").toInt();
  if (server.hasArg("i4")) conf.ikh[4] = server.arg("i4").toInt();

  if (server.hasArg("br")) conf.bright = server.arg("br").toInt();
  if (server.hasArg("iqm")) {
    int val = server.arg("iqm").toInt();
    conf.iqomah = (val > 0 && val < 60) ? val : 3;
  }

  EEPROM.put(0, conf);
  if (EEPROM.commit()) {
    Serial.println(F("EEPROM Berhasil disimpan!"));
  }

  FastLED.setBrightness(conf.bright);
  readRTC();
  updatePrayers();
  delay(100);

  String pesanWeb = "UPDATE BERHASIL!";
  String pesanSub = "Mohon tunggu sebentar, halaman akan memuat ulang...";

  if (restartWiFi) {
    pesanWeb = "WIFI DIPERBARUI!";
    pesanSub = "Silakan hubungkan ulang HP Anda ke WiFi: <b>" + String(conf.apSSID) + "</b>";
  }
  tampilkanDashboard("SETELAH UPDATE");

  if (server.hasArg("app")) {
    server.send(200, "text/plain", "SUKSES");
  } else {
    server.send(200, "text/html",
                "<html><body style='font-family:sans-serif; text-align:center; padding-top:50px;'>"
                "<h2 style='font-size:36px; color:#2ecc71;'>"
                  + pesanWeb + "</h2>"
                               "<p style='font-size:18px; color:#666;'>"
                  + pesanSub + "</p>"
                               "<script>setTimeout(()=>window.location='/',4000);</script>"
                               "</body></html>");
  }

  if (restartWiFi) {
    delay(1000);
    WiFi.softAPdisconnect(true);
    WiFi.softAP(conf.apSSID, conf.apPass);
    Serial.println("WiFi Direstart dengan SSID Baru!");
  }
}

void handleTestIqomah() {
  // Test dijalankan di NeoPixel secara lokal
  isAdzan = true;
  hitungMundurAdzan = 5;  // 5 Detik Test Adzan, dilanjut test Iqomah
  server.send(200, "text/html", "<html><body><script>alert('Sinyal Test Adzan & Iqomah Aktif di NeoPixel!'); window.location='/';</script></body></html>");
}

void handleGetData() {
  String dataSync = String(conf.apSSID) + "*" + String(conf.apPass) + "*" + String(conf.namaMasjid) + "*" + String(conf.lat, 6) + "*" + String(conf.lon, 6) + "*" + String(conf.ikh[0]) + "*" + String(conf.ikh[1]) + "*" + String(conf.ikh[2]) + "*" + String(conf.ikh[3]) + "*" + String(conf.ikh[4]) + "*" + String(conf.bright) + "*" + String(conf.iqomah) + "*" + String(conf.msg);
  server.send(200, "text/plain", dataSync);
}

void handleGetJadwal() {
  String dataJadwal = "";
  for (int i = 0; i < 5; i++) {
    char waktu[10];
    sprintf(waktu, "%02d:%02d", jamSholatAktual[i], menitSholatAktual[i]);
    dataJadwal += String(waktu);
    if (i < 4) dataJadwal += "*";
  }
  server.send(200, "text/plain", dataJadwal);
}

void setup() {
  Serial.begin(115200);
  Wire.begin();

  if (!rtc.begin()) {
    Serial.println("RTC Tidak Ditemukan!");
  }

  EEPROM.begin(512);
  EEPROM.get(0, conf);

  if (conf.magic != 888889) {
    conf.magic = 888889;
    conf.lat = -7.648227;
    conf.lon = 110.351839;

    for (int i = 0; i < 5; i++) { conf.ikh[i] = 0; }

    conf.bright = 120;
    strcpy(conf.msg, "JWS NEO V17 OK");
    strcpy(conf.namaMasjid, "SMKN 1 Magelang");
    conf.iqomah = 3;

    EEPROM.put(0, conf);
    EEPROM.commit();
    Serial.println(F("EEPROM RESET SUCCESS DENGAN WIFI DEFAUT!"));
  }

  WiFi.persistent(false);
  WiFi.disconnect();
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(IPAddress(10, 10, 10, 9), IPAddress(10, 10, 10, 9), IPAddress(255, 255, 255, 0));
  WiFi.softAP(conf.apSSID, conf.apPass);
  dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
  // 2. ATUR ROUTING WEB BROWSER
  server.collectHeaders("Cookie");
  server.on("/", handleLogin);
  server.on("/login", handleLogin);
  server.on("/auth", HTTP_POST, handleAuth);
  server.on("/setting", handleRoot);
  server.on("/logout", handleLogout);

  // PANCINGAN CAPTIVE PORTAL
  server.on("/generate_204", handleCaptivePortal);
  server.on("/hotspot-detect.html", handleCaptivePortal);
  server.onNotFound(handleCaptivePortal);

  server.on("/save", handleSave);
  server.on("/test", handleTestIqomah);
  server.on("/getdata", handleGetData);
  server.on("/getjadwal", handleGetJadwal);
  server.begin();

  ElegantOTA.begin(&server);
  FastLED.addLeds<WS2812B, PIXEL_PIN, GRB>(leds, TOTAL_LED).setCorrection(TypicalLEDStrip);
  FastLED.setBrightness(conf.bright);
  FastLED.clear();
  FastLED.show();
  readRTC();
  updatePrayers();
  tampilkanDashboard("JWS - NEOPIXEL ONLY");
  WiFi.softAP("DnR_Neo");
}

void loop() {
  dnsServer.processNextRequest();
  server.handleClient();
  ElegantOTA.loop();
  ESP.wdtFeed();

  if (millis() - lastUpdateRTC >= 1000) {
    lastUpdateRTC = millis();
    readRTC();

    if (Hour == 0 && Minute == 0 && Second == 0) updatePrayers();

    if (isAdzan) {
      hitungMundurAdzan--;
      if (hitungMundurAdzan <= 0) {
        isAdzan = false;
        isIqomah = true;
        hitungMundurIqomah = conf.iqomah * 60;
      }
    } else if (isIqomah) {
      hitungMundurIqomah--;
      if (hitungMundurIqomah <= 0) {
        isIqomah = false;
      }
    }
  }

  static unsigned long lastAnim = 0;
  if (millis() - lastAnim > 20) {
    lastAnim = millis();
    wheelPos += 1;
  }

  // DI DALAM VOID LOOP()
  int sholatDekat = -1;

  for (int i = 0; i < 5; i++) {
    // Tinggal ambil dari variabel aktual, tidak perlu hitung ulang!
    int totalMenitSholat = (jamSholatAktual[i] * 60) + menitSholatAktual[i];
    int totalMenitSekarang = (Hour * 60) + Minute;

    if (totalMenitSekarang == (totalMenitSholat - 1)) {
      sholatDekat = i;
    }

    if (Hour == jamSholatAktual[i] && Minute == menitSholatAktual[i] && Second <= 2 && !isAdzan && !isIqomah) {
      isAdzan = true;
      hitungMundurAdzan = durasiAdzan * 60;
    }
  }

  static unsigned long lastShow = 0;
  if (millis() - lastShow > 30) {
    lastShow = millis();
    FastLED.clear();

    if (isIqomah) {
      int mIq = hitungMundurIqomah / 60;
      int sIq = hitungMundurIqomah % 60;

      drawDigitStatic(0, mIq / 10, CRGB::Cyan);
      drawDigitStatic(1, mIq % 10, CRGB::Cyan);
      drawDigitStatic(2, sIq / 10, CRGB::Cyan);
      drawDigitStatic(3, sIq % 10, CRGB::Cyan);

      if (Second % 2 == 0) {
        leds[N_PER_SEG * 7 * 2] = leds[N_PER_SEG * 7 * 2 + 1] = CRGB::Cyan;
      }

    } else if (isAdzan) {
      uint8_t napas = beatsin8(30, 50, 255);
      CRGB warnaNapas = CHSV(96, 255, napas);  // 96 = Hue Hijau

      // PERBAIKAN: Gunakan warnaNapas di sini agar angkanya ikut bernapas
      drawDigitStatic(0, Hour / 10, warnaNapas);
      drawDigitStatic(1, Hour % 10, warnaNapas);
      drawDigitStatic(2, Minute / 10, warnaNapas);
      drawDigitStatic(3, Minute % 10, warnaNapas);
      leds[N_PER_SEG * 7 * 2] = leds[N_PER_SEG * 7 * 2 + 1] = warnaNapas;

    } else if (sholatDekat != -1) {  // <--- Kurung kurawal yang bocor sudah dirapikan di sini
      if ((millis() / 500) % 2 == 0) {
        drawDigitStatic(0, Hour / 10, CRGB::Red);
        drawDigitStatic(1, Hour % 10, CRGB::Red);
        drawDigitStatic(2, Minute / 10, CRGB::Red);
        drawDigitStatic(3, Minute % 10, CRGB::Red);
        leds[N_PER_SEG * 7 * 2] = leds[N_PER_SEG * 7 * 2 + 1] = CRGB::Red;
      }
    } else {
      uint8_t baseHue = wheelPos;  // Default pelangi muter

      drawDigit(0, Hour / 10, baseHue);
      drawDigit(1, Hour % 10, baseHue);
      drawDigit(2, Minute / 10, baseHue);
      drawDigit(3, Minute % 10, baseHue);

      // Titik berkedip halus
      uint8_t kedipTitik = beatsin8(60, 0, 255);  // Kedip 1 detik sekali
      leds[N_PER_SEG * 7 * 2] = CHSV(baseHue, 255, kedipTitik);
      leds[N_PER_SEG * 7 * 2 + 1] = CHSV(baseHue, 255, kedipTitik);
    }

    FastLED.show();  // FastLED.show() harus berada di dalam blok `if (millis() - lastShow > 30)`
  }
  yield();
}
