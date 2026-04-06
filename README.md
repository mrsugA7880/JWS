# JWS
Smart JWS - SMK Electronics Edition - NEOPIXEL STANDALONE

# 🕋 Smart JWS - SMK Electronics Edition
**Sistem Jam Waktu Sholat Berbasis ESP8266 (StandAlone)**

---

## 🚀 Fitur Unggulan
* **Auto-Location:** Sinkronisasi lokasi (Lat/Lon) otomatis via internet.
* **Web Control Interface:** Pengaturan WiFi, Pesan, Volume, dan Jadwal melalui HP

---

## 🔌 Skema Koneksi Pin

### Master Unit (Wemos D1 Mini)
| Komponen | Pin Wemos | Keterangan |
| :--- | :--- | :--- |
| **RTC DS3231** | D2 (SDA), D1 (SCL) | Waktu Presisi |
| **DFPlayer** | D7 (TX), D5 (RX) | Modul MP3 |
| **Data Out** | **TX** | Kirim ke RX Slave |
| **Power** | 5V & GND | Sumber Daya |

---

## 📝 Catatan Teknis
* **Baudrate:** Master dan Slave disetel pada kecepatan `9600 bps`.
* **Power:** Gunakan Power Supply minimal **5V 5A** jika menggunakan lebih dari 1 panel P10.
* **Upload:** Login melalui koneksi wifi master, update menggunakan OTA.
---

**Developed by SMK Electronics - 2026**
