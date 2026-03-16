#include <DMDESP.h>
#include <fonts/SystemFont5x7Ramping.h>
#include <fonts/Arial_Black_16.h>
#include <fonts/SystemFont5x7Ramping.h>
#include <fonts/BigNumber.h>
#include <EEPROM.h>    // Tambahkan ini
#define ADDR_PESAN 10  // Alamat mulai simpan pesan di EEPROM

int pnlX = 1;
int pnlY = 1;
DMDESP Disp(pnlX, pnlY);

int jam, menit, detik, tgl, bln, thn;
String jadwal[6] = { "00:00", "00:00", "00:00", "00:00", "00:00", "00:00" };
String namaSholat[6] = { "IMSAK", "SUBUH", "DZUHUR", "ASHAR", "MAGRIB", "ISYA" };

// --- MODIFIKASI VARIABEL PESAN ---
String pesanHP = "JWS SMK ELECTRONICS";  // Simpan input HP
String pesanAuto = "";                   // Simpan Motivasi/Kalender
String* activeMsg = &pesanHP;            // Pointer untuk memilih pesan yang tampil

char frame[160];
byte idx = 0;
bool mulai = false;
unsigned long lastScroll, lastMode, lastRX = 0;
int scrollX;
bool sudahAnimasi = false;  // Deklarasi variabel animasi
int displayWidth = 32;      // Definisi lebar panel

byte mode = 0;

byte checksum(String s) {
  byte cs = 0;
  for (int i = 0; i < s.length(); i++) cs ^= s[i];
  return cs;
}

void animasiTypewriter(int x, int y, String pesan, int delayTime) {
  String tempPesan = "";
  for (int i = 0; i < pesan.length(); i++) {
    tempPesan += pesan[i];  // Tambah satu huruf
    Disp.clear();           // Bersihkan layar
    // Gambar baris lain jika ada (misal jam di baris bawah)
    Disp.drawText(x, y, tempPesan);
    delay(delayTime);  // Kecepatan mengetik
  }
}

void scrollDown(String pesan, int targetY) {
  // Gunakan Disp.width() agar lebih aman
  int xPos = (Disp.width() - Disp.textWidth(pesan)) / 2;
  for (int y = -8; y <= targetY; y++) {
    Disp.clear();
    Disp.drawText(xPos, y, pesan);
    delay(50);
  }
}

void scrollUp(String pesan, int targetY) {
  int xPos = (Disp.width() - Disp.textWidth(pesan)) / 2;
  for (int y = 16; y >= targetY; y--) {
    Disp.clear();
    Disp.drawText(xPos, y, pesan);
    delay(50);
  }
}

void parseData(char* data) {
  // 1. LOGIKA LAYOUT
  if (strncmp(data, "CONFIG,LAYOUT", 13) == 0) {
    char* pData = data;            // Gunakan nama pData agar tidak tertukar
    strtok_r(pData, ",", &pData);  // Skip CONFIG
    strtok_r(pData, ",", &pData);  // Skip LAYOUT

    char* valX = strtok_r(pData, ",", &pData);
    char* valY = strtok_r(pData, ",", &pData);

    if (valX != NULL && valY != NULL) {
      int newX = atoi(valX);
      int newY = atoi(valY);

      if (newX > 0 && (newX != pnlX || newY != pnlY)) {
        pnlX = newX;
        pnlY = newY;
        Disp.start();         // Inisialisasi ulang ukuran buffer
        scrollX = pnlX * 32;  // Reset koordinat scroll ke ujung kanan
      }
    }
    Disp.setBrightness(40);
    Serial.println("ACK");
    return;
  }

  // 2. LOGIKA TYPE (IQOMAH & MOTIVASI)
  if (strncmp(data, "CONFIG,TYPE", 11) == 0) {
    char* pType = data;
    strtok_r(pType, ",", &pType);  // Skip CONFIG
    strtok_r(pType, ",", &pType);  // Skip TYPE

    // Sisa string pType adalah isi pesannya
    pesanAuto = String(pType);
    pesanAuto.toUpperCase();
    activeMsg = &pesanAuto;
    mode = 1;

    // LOGIKA KRUSIAL: Jangan reset scroll jika pesan IQOMAH
    if (pesanAuto.indexOf("IQOMAH") == -1) {
      scrollX = pnlX * 32;
    }

    lastMode = millis();
    Serial.println("ACK");
    return;
  }

  // 3. LOGIKA PESAN DARI HP (MSG)
  if (strncmp(data, "CONFIG,MSG", 10) == 0) {
    char* pMsg = data;
    strtok_r(pMsg, ",", &pMsg);
    strtok_r(pMsg, ",", &pMsg);

    pesanHP = String(pMsg);
    pesanHP.toUpperCase();
    // --- SIMPAN KE EEPROM ---
    for (int i = 0; i < pesanHP.length(); i++) {
      EEPROM.write(ADDR_PESAN + i, pesanHP[i]);
    }
    EEPROM.write(ADDR_PESAN + pesanHP.length(), '\0');  // Penutup string
    EEPROM.commit();                                    // Wajib untuk ESP8266 agar data benar-benar tertulis
    // ------------------------
    activeMsg = &pesanHP;
    mode = 1;
    scrollX = pnlX * 32;
    lastMode = millis();
    Serial.println("ACK");
    return;
  }

  // 4. DATA JAM & JADWAL (DENGAN CHECKSUM)
  char* star = strchr(data, '*');
  if (!star) return;
  *star = '\0';
  byte csDiterima = atoi(star + 1);
  if (checksum(String(data)) != csDiterima) return;

  lastRX = millis();
  char* pt = data;
  char* str;
  byte i = 0;
  while ((str = strtok_r(pt, ",", &pt)) != NULL) {
    if (i == 0) jam = atoi(str);
    else if (i == 1) menit = atoi(str);
    else if (i == 2) detik = atoi(str);
    else if (i == 3) tgl = atoi(str);
    else if (i == 4) bln = atoi(str);
    else if (i == 5) thn = atoi(str);
    else if (i >= 6 && i <= 11) {
      jadwal[i - 6] = String(str);
      jadwal[i - 6].trim();
    }
    i++;
  }
  // 1.5 LOGIKA KECERAHAN (BRIGHTNESS)
  if (strncmp(data, "CONFIG,BRIGHT", 13) == 0) {
    char* pBright = data;
    strtok_r(pBright, ",", &pBright);  // Skip CONFIG
    strtok_r(pBright, ",", &pBright);  // Skip BRIGHT

    int valBright = atoi(pBright);
    if (valBright >= 0 && valBright <= 255) {
      Disp.setBrightness(valBright);
    }
    Serial.println("ACK");
    return;
  }
}

void bacaSerial() {
  while (Serial.available() > 0) {
    char c = Serial.read();
    if (c == '<') {
      mulai = true;
      idx = 0;
    } else if (c == '>') {
      frame[idx] = '\0';
      parseData(frame);
      mulai = false;
    } else if (mulai && idx < 155) {  // Proteksi agar memori tidak jebol
      frame[idx++] = c;
    }
    yield();
  }
}

void setup() {
  Serial.begin(9600);
  EEPROM.begin(512);  // Inisialisasi memori EEPROM 512 byte

  // Baca pesan dari EEPROM saat start
  String savedMsg = "";
  for (int i = 0; i < 150; ++i) {
    char c = char(EEPROM.read(ADDR_PESAN + i));
    if (c == '\0' || c == 255) break;
    savedMsg += c;
  }

  if (savedMsg.length() > 0) {
    pesanHP = savedMsg;  // Gunakan pesan yang tersimpan
  } else {
    pesanHP = "JWS SMK ELECTRONICS";
  }

  Disp.start();
  Disp.setBrightness(40);
  scrollX = pnlX * 32;
}

void loop() {
  bacaSerial();
  Disp.loop();
  int displayWidth = pnlX * 32;

  // 1. CEK KONEKSI MASTER
  if (millis() - lastRX > 10000) {
    static unsigned long lastM = 0;
    if (millis() - lastM > 500) {
      Disp.clear();
      Disp.setFont(SystemFont5x7Ramping);
      Disp.drawText((displayWidth - Disp.textWidth("MENCARI")) / 2, 4, "MENCARI");
      lastM = millis();
    }
    return;
  }

  // 2. LOGIKA STATE MACHINE (URUTAN TAMPILAN)
  static byte urutan = 0;
  static unsigned long lastUpdate = 0;
  static int displayDuration = 5000;

  if (millis() - lastUpdate > displayDuration) {
    urutan++;
    if (urutan > 3) {
      urutan = 0;
      sudahAnimasi = false;  // Reset animasi saat siklus kembali ke awal
    }
    lastUpdate = millis();
    Disp.clear();
  }

  switch (urutan) {
    case 0:
      {  // --- JAM BESAR (FONT BIGNUMBER) ---
        Disp.setFont(BigNumber);
        char h[3], m[3];
        sprintf(h, "%02d", jam);
        sprintf(m, "%02d", menit);

        Disp.drawText(1, 0, h);
        Disp.drawText(18, 0, m);

        if (detik % 2 == 0) {
          Disp.setPixel(15, 5, 1);
          Disp.setPixel(16, 5, 1);
          Disp.setPixel(15, 6, 1);
          Disp.setPixel(16, 6, 1);
          Disp.setPixel(15, 10, 1);
          Disp.setPixel(16, 10, 1);
          Disp.setPixel(15, 11, 1);
          Disp.setPixel(16, 11, 1);
        } else {
          Disp.setPixel(15, 5, 0);
          Disp.setPixel(16, 5, 0);
          Disp.setPixel(15, 6, 0);
          Disp.setPixel(16, 6, 0);
          Disp.setPixel(15, 10, 0);
          Disp.setPixel(16, 10, 0);
          Disp.setPixel(15, 11, 0);
          Disp.setPixel(16, 11, 0);
        }
        displayDuration = 5000;
      }
      break;

    case 1:
      {  // --- HARI & TANGGAL: ANIMASI SCROLL ---
        Disp.setFont(SystemFont5x7Ramping);

        // 1. Logika Nama Hari
        static const char* namaHariK[] = { "MINGGU", "SENIN", "SELASA", "RABU", "KAMIS", "JUMAT", "SABTU" };
        int yK = thn;
        static int tK[] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
        yK -= bln < 3;
        int dIdxK = (yK + yK / 4 - yK / 100 + yK / 400 + tK[bln - 1] + tgl) % 7;

        char tglStr[6];
        sprintf(tglStr, "%02d-%02d", tgl, bln);

        if (!sudahAnimasi) {
          // --- ANIMASI 1: HARI TURUN ---
          for (int y = -8; y <= 0; y++) {
            Disp.clear();
            int xHari = (displayWidth - Disp.textWidth(namaHariK[dIdxK])) / 2;
            Disp.drawText(xHari, y, namaHariK[dIdxK]);
            Disp.loop();  // Paksa refresh panel saat animasi
            delay(40);    // Kecepatan meluncur
          }

          // --- ANIMASI 2: TANGGAL NAIK ---
          for (int y = 16; y >= 9; y--) {
            Disp.clear();
            int xHari = (displayWidth - Disp.textWidth(namaHariK[dIdxK])) / 2;
            int xTgl = (displayWidth - Disp.textWidth(tglStr)) / 2;

            Disp.drawText(xHari, 0, namaHariK[dIdxK]);  // Hari sudah diam di 0
            Disp.drawText(xTgl, y, tglStr);             // Tanggal bergerak naik
            Disp.loop();
            delay(40);
          }

          sudahAnimasi = true;
          lastUpdate = millis();  // Mulai hitung durasi diam SETELAH animasi selesai
        }

        // --- TAMPILAN DIAM (Setelah Animasi) ---
        Disp.drawText((displayWidth - Disp.textWidth(namaHariK[dIdxK])) / 2, 0, namaHariK[dIdxK]);
        Disp.drawText((displayWidth - Disp.textWidth(tglStr)) / 2, 9, tglStr);

        displayDuration = 7000;  // Diam selama 7 detik
      }
      break;

    case 2:
      {  // --- JADWAL SHOLAT OTOMATIS BERGANTI (IMSAK - ISYA) ---
        Disp.setFont(SystemFont5x7Ramping);

        static byte idxSholat = 0;  // Index untuk memilih sholat (0-5)
        static int charIndex = 0;
        static unsigned long lastType = 0;

        String sholat = namaSholat[idxSholat];
        String waktu = jadwal[idxSholat];
        int totalLength = sholat.length() + waktu.length();

        // 1. Logika Animasi Mengetik
        if (charIndex <= totalLength) {
          if (millis() - lastType > 120) {
            Disp.clear();
            if (charIndex <= sholat.length()) {
              // Tampilkan Nama Sholat (Progress Mengetik)
              String subNama = sholat.substring(0, charIndex);
              int xPusat = (displayWidth - Disp.textWidth(subNama)) / 2;
              Disp.drawText(xPusat, 0, subNama);
            } else {
              // Nama Sholat sudah penuh di atas
              int xPusatNama = (displayWidth - Disp.textWidth(sholat)) / 2;
              Disp.drawText(xPusatNama, 0, sholat);

              // Tampilkan Jam Sholat (Progress Mengetik)
              int subIdx = charIndex - sholat.length();
              String subWaktu = waktu.substring(0, subIdx);
              int xPusatWaktu = (displayWidth - Disp.textWidth(subWaktu)) / 2;
              Disp.drawText(xPusatWaktu, 9, subWaktu);
            }
            charIndex++;
            lastType = millis();
            lastUpdate = millis();
          }
        } else {
          // Teks diam (Sudah selesai diketik semua)
          Disp.clear();
          Disp.drawText((displayWidth - Disp.textWidth(sholat)) / 2, 0, sholat);
          Disp.drawText((displayWidth - Disp.textWidth(waktu)) / 2, 9, waktu);
        }

        displayDuration = 3000;

        // 2. Logika Pergantian Sholat
        if (millis() - lastUpdate > displayDuration) {
          charIndex = 0;
          idxSholat++;

          if (idxSholat > 5) {
            idxSholat = 0;
            urutan++;
            lastUpdate = millis();
            Disp.clear();
          } else {
            lastUpdate = millis();
          }
        }
      }
      break;

    case 3:
      {  // --- PESAN MOTIVASI / RUNNING TEXT ---
        if (activeMsg->indexOf("IQOMAH") >= 0) {
          Disp.setFont(SystemFont5x7Ramping);
          Disp.drawText((displayWidth - Disp.textWidth(*activeMsg)) / 2, 5, *activeMsg);
          displayDuration = 5000;
        } else {
          Disp.setFont(Arial_Black_16);
          if (millis() - lastScroll > 30) {
            Disp.clear();
            Disp.drawText(scrollX, 0, *activeMsg);
            scrollX--;
            lastScroll = millis();
          }
          if (scrollX < -(Disp.textWidth(*activeMsg))) {
            scrollX = displayWidth;
            urutan = 0;
            sudahAnimasi = false;  // Penting: Reset animasi di sini juga
            lastUpdate = millis();
          }
          displayDuration = 60000;
        }
      }
      break;
  }
}  // <--- KURUNG TUTUP VOID LOOP ADA DI SINI SEKARANG