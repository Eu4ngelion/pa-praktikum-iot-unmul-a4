// ================================================================
//  PROYEK AKHIR PRAKTIKUM IoT - KELOMPOK 4
//  TEMA  : SMART VENT SYSTEM
//  JUDUL : Sistem Ventilasi Cerdas Berbasis IoT
// ================================================================
#include <AntaresESPMQTT.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <DHT.h>
#include <Preferences.h>

// ================================================================
//  KONFIGURASI PLATFORM
// ================================================================

// --- WiFi ---
#define WIFI_SSID "Flugel"
#define WIFI_PASSWORD "HP2002AU"

// --- Antares ---
#define ACCESSKEY "YOUR_ACCESS_KEY"
#define PROJECT_NAME "SmartVent"
#define DEVICE_NAME_SENSOR "Sensor"
#define DEVICE_NAME_KONTROL "Kontrol"

// --- Telegram Bot ---
#define TELE_BOT_TOKEN "YOUR_TELE_BOT_TOKEN"

// ================================================================
// PIN HARDWARE
// ================================================================
#define DHTPIN 18
#define DHTTYPE DHT11
#define PIN_MQ135 34
#define PIN_RELAY 23
#define PIN_LED_CONNECTION 19  // Nyala = WiFi terhubung
#define PIN_LED_MODE 21        // Nyala = mode Otomatis aktif

// ================================================================
//  INISIALISASI OBJEK
// ================================================================
AntaresESPMQTT antares(ACCESSKEY);
WiFiClientSecure securedClient;
UniversalTelegramBot bot(TELE_BOT_TOKEN, securedClient);
DHT dht(DHTPIN, DHTTYPE);
Preferences prefs;

// ================================================================
//  VARIABEL GLOBAL
// ================================================================

// --- Nilai Sensor  ---
float TempValue = 0;
float HumidityValue = 0;
float GasValue = 0;

// --- Batas Ambang (Threshold) ---
float TempThreshold = 32;
float HumidityThreshold = 75;
float GasThreshold = 900;

// --- State Sistem ---
int VentMode = 0;  // 0 = Auto, 1 = Manual ON, 2 = Manual OFF
bool isWifiConnected = false;
bool isVentOn = false;
bool isStartUp = true;

// --- Interval Timer ---
const unsigned long intervalWifi = 20000;
const unsigned long intervalSensor = 5000;
const unsigned long intervalTelegram = 2000;

unsigned long previousMillisWifi = (0 - intervalWifi);
unsigned long previousMillisSensor = 0;
unsigned long previousMillisTelegram = 0;

// --- Sistem Notifikasi ---
#define MAX_NOTIF_SUBSCRIBERS 10
String notifSubscribers[MAX_NOTIF_SUBSCRIBERS];
int subscriberCount = 0;
bool lastNotifCondition = false;

// ================================================================
//  DEKLARASI FUNGSI
// ================================================================
// Sensor
void readSensors();
void controlVentilation();
// Preferences
void loadPreferences();
void saveControls();
void saveSubscribers();
// Antares / Cloud
void publishSensorData();
void publishControlData();
void callback(char topic[], byte payload[], unsigned int length);
// Telegram Bot
void handleTelegramMessages();
void sendStatus(String chat_id);
String buildStatusMessage();
void sendToAllSubscribers(String message);
bool addSubscriber(String chat_id);
bool removeSubscriber(String chat_id);
int findSubscriber(String chat_id);
void checkAndSendNotification();


// ================================================================
//  SETUP
// ================================================================
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n=============================");
  Serial.println("      SMART VENT SYSTEM");
  Serial.println("=============================\n");

  // Konfigurasi pin
  pinMode(PIN_RELAY, OUTPUT);
  pinMode(PIN_LED_CONNECTION, OUTPUT);
  pinMode(PIN_LED_MODE, OUTPUT);
  pinMode(PIN_MQ135, INPUT);
  digitalWrite(PIN_RELAY, LOW);
  digitalWrite(PIN_LED_CONNECTION, LOW);
  digitalWrite(PIN_LED_MODE, LOW);
  analogReadResolution(10); 

  // Inisialisasi sensor
  dht.begin();

  // Ambil settingan kontrol terakhir yang tersimpan di memory flash
  loadPreferences();

  // Set insecure untuk telegram bot
  securedClient.setInsecure();

  Serial.println("\nSetup selesai.\n");
}


// ================================================================
//  LOOP
// ================================================================
void loop() {

  // Mengatur Koneksi WiFi + MQTT
  if (!isWifiConnected && (millis() - previousMillisWifi >= intervalWifi)) {
    previousMillisWifi = millis();

    // Coba hubungkan wifi
    if (antares.wifiConnection(WIFI_SSID, WIFI_PASSWORD)) {
      // Nyalakan led indikator wifi
      digitalWrite(PIN_LED_CONNECTION, HIGH);
      isWifiConnected = true;
      Serial.println("[WiFi] Terhubung!");

      // Setup koneksi MQTT ke Antares
      antares.setMqttServer();
      antares.setCallback(callback);
      antares.checkMqttConnection();

      // Ambil data kontrol terakhir dari Antares, Mentrigger fungsi callback
      antares.retrieveLastData(PROJECT_NAME, DEVICE_NAME_KONTROL);

    } else {
      Serial.println("[WiFi] Gagal menghubungkan.");
    }

  } 
  if (isWifiConnected) {
    antares.checkMqttConnection();
  }


  // Baca Sensor & Kontrol Kipas tiap interval
  if (millis() - previousMillisSensor >= intervalSensor) {
    previousMillisSensor = millis();

    readSensors();
    Serial.print("[SENSOR] Suhu: ");
    Serial.print(TempValue);
    Serial.print("\t| Kelembapan: ");
    Serial.print(HumidityValue);
    Serial.print("\t| Gas: ");
    Serial.println(GasValue);
    controlVentilation();

    // Publish MQTT & Kirim Notifikasi Tele
    if (isWifiConnected) {
      publishSensorData();
      checkAndSendNotification();
    }
  }

  // Baca Chat telegram tiap interval
  if (isWifiConnected && (millis() - previousMillisTelegram >= intervalTelegram)) {
    previousMillisTelegram = millis();
    handleTelegramMessages();
  }
}

// ================================================================
//  FUNGSI SENSOR & HARDWARE
// ================================================================

// Fungsi untuk membaca nilai sensor
void readSensors() {
  TempValue = dht.readTemperature();
  HumidityValue = dht.readHumidity();
  GasValue = analogRead(PIN_MQ135);
}

// Fungsi untuk mengendalikan kipas sesuai mode dan nilai sensor
void controlVentilation() {
  // Mode Otomatis
  if (VentMode == 0) {
    digitalWrite(PIN_LED_MODE, HIGH);
    bool isAboveThreshold = (TempValue >= TempThreshold) || (HumidityValue >= HumidityThreshold) || (GasValue >= GasThreshold);
    digitalWrite(PIN_RELAY, isAboveThreshold ? LOW : HIGH);
    isVentOn = isAboveThreshold;

  }
  // Manual ON
  else if (VentMode == 1) {  // Manual ON
    digitalWrite(PIN_LED_MODE, LOW);
    digitalWrite(PIN_RELAY, LOW);
    isVentOn = true;

  }
  // Manual OFF
  else {
    digitalWrite(PIN_LED_MODE, LOW);
    digitalWrite(PIN_RELAY, HIGH);
    isVentOn = false;
  }
}


// ================================================================
//  FUNGSI PREFERENCES
//  - Preferences menyimpan data secara permanen di namespace, tidak hilang saat boot/reset.
// ================================================================

// Fungsi ini dipanggil sekali di setup(). Membaca data dari Memort Flash.
void loadPreferences() {

  // Membaca namespace "settings" - Konfigurasi kontrol sistem
  prefs.begin("settings", true);  // true: read-only
    // Mengambil data dari namespace
  TempThreshold = prefs.getFloat("batasSuhu", 32.0);       // default 32 C
  HumidityThreshold = prefs.getFloat("batasHumid", 75.0);  // default 75%
  GasThreshold = prefs.getFloat("batasGas", 900.0);        // default 900ppm
  VentMode = prefs.getInt("ventMode", 0);
  prefs.end();

  Serial.println("[NVS] Threshold dimuat:");
  Serial.print("  Batas Suhu: ");
  Serial.println(TempThreshold);
  Serial.print("  Batas Kelembapan: ");
  Serial.println(HumidityThreshold);
  Serial.print("  Batas Gas: ");
  Serial.println(GasThreshold);
  Serial.print("  VentMode: ");
  Serial.println(VentMode);


  // Namespace "notif" - Daftar subscriber notifikasi telegram
  // Menyimpan setiap chat_id ke variabel array "notifSubscribers[]"
  prefs.begin("notif", true);                  // true: read only
  subscriberCount = prefs.getInt("count", 0);  // Jumlah Subscriber tersimpan
  for (int i = 0; i < subscriberCount; i++) {
    String key = "sub_" + String(i);  // "sub_0", "sub_1", "sub_2", "sub_..."
    notifSubscribers[i] = prefs.getString(key.c_str(), "");
  }
  prefs.end();


  Serial.print("[NVS] Subscriber dimuat: ");
  Serial.println(subscriberCount);
}


// Fungsi: Menyimpan nilai kontrol ke Memory Flash
void saveControls() {
  prefs.begin("settings", false);  // read-write mode
  prefs.putFloat("batasSuhu", TempThreshold);
  prefs.putFloat("batasHumid", HumidityThreshold);
  prefs.putFloat("batasGas", GasThreshold);
  prefs.putInt("ventMode", VentMode);
  prefs.end();
  Serial.println("[NVS] Setting Kontrol disimpan ke flash.");
}


// Menyimpan daftar subsriber ke memory flash
void saveSubscribers() {
  prefs.begin("notif", false);  // read-write mode

  prefs.putInt("count", subscriberCount);
  for (int i = 0; i < subscriberCount; i++) {
    String key = "sub_" + String(i);
    prefs.putString(key.c_str(), notifSubscribers[i]);
  }
  prefs.end();
  Serial.print("[NVS] Subscriber disimpan: ");
  Serial.println(subscriberCount);
}


// ================================================================
// FUNGSI ANTARES (MQTT)
// ================================================================

// Mengirim nilai sensor + status kipas ke device "Sensor" di Antares.
void publishSensorData() {
  antares.add("suhu", TempValue);
  antares.add("kelembapan", HumidityValue);
  antares.add("gas", GasValue);
  antares.add("statusKipas", isVentOn ? 1 : 0);
  antares.publish(PROJECT_NAME, DEVICE_NAME_SENSOR);
}

// Mengirim nilai threshold + mode kipas ke device "Kontrol" di Antares.
void publishControlData() {
  antares.add("batasSuhu", TempThreshold);
  antares.add("batasKelembapan", HumidityThreshold);
  antares.add("batasGas", GasThreshold);
  antares.add("modeKipas", VentMode);
  antares.publish(PROJECT_NAME, DEVICE_NAME_KONTROL);
}


// Fungsi yang dipanggil otomatis setiap ada pesan MQTT masuk
// Sinkronisasi data kontrol antara esp32 - antares - kodular
void callback(char topic[], byte payload[], unsigned int length) {
  antares.get(topic, payload, length);

  // Filter: hanya proses jika pesan berisi data kontrol
  if (antares.getString("batasSuhu") != "null" ) {
    float newTempThresh = antares.getFloat("batasSuhu");
    int newHumThresh = antares.getInt("batasKelembapan");
    int newGasThresh = antares.getInt("batasGas");
    int newVentMode = antares.getInt("modeKipas");

    // Update hanya jika ada nilai yang berubah (atau saat pertama kali startup)
    bool isControlChanged = isStartUp || (TempThreshold != newTempThresh) || (HumidityThreshold != newHumThresh) || (GasThreshold != newGasThresh) || (VentMode != newVentMode);

    if (isControlChanged) {
      isStartUp = false;
      TempThreshold = newTempThresh;
      HumidityThreshold = newHumThresh;
      GasThreshold = newGasThresh;
      VentMode = newVentMode;

      // Kontrol kipas sesuai setting terbaru
      readSensors();
      controlVentilation();
      publishSensorData();
      Serial.println("[MQTT] Setting Kontrol diperbarui.");

      // Simpan settingan terbaru ke memory flash
      saveControls();
    }
  }
}



// ================================================================
//  TELEGRAM BOT
// ================================================================

// Fungsi untuk memproses setiap command telegram
void handleTelegramMessages() {
  int numNewMessages = bot.getUpdates(bot.last_message_received + 1);

  while (numNewMessages) {
    for (int i = 0; i < numNewMessages; i++) {
      String chat_id = String(bot.messages[i].chat_id);
      String text = bot.messages[i].text;
      String from = bot.messages[i].from_name;

      // /start
      if (text == "/start") {
        String help = "🤖 *SMART VENT SYSTEM*\n\n";
        help += "Halo, " + from + "! Daftar command:\n\n";
        help += "📡 *Monitoring*\n";
        help += "`/status` - Status lengkap sistem\n\n";
        help += "🔧 *Kontrol Kipas*\n";
        help += "`/fanAuto` `/fanOn` `/fanOff`\n\n";
        help += "⚙️ *Setting Batas*\n";
        help += "`/setBatasSuhu [angka]`\n";
        help += "`/setBatasKelembapan [angka]`\n";
        help += "`/setBatasGas [angka]`\n\n";
        help += "🔔 *Notifikasi*\n";
        help += "`/seeSubscriber`\n";
        help += "`/addSubs`\n";
        help += "`/deleteSubs`\n\n";
        help += "💡 Chat ID kamu: `" + chat_id + "`\n";
        bot.sendMessage(chat_id, help, "Markdown");

        // ----------------------------------------------------------
        // /status
        // ----------------------------------------------------------
      } else if (text == "/status") {
        // Menampilkan pesan status lengkap sistem.
        sendStatus(chat_id);

        // ----------------------------------------------------------
        // /fanAuto /fanOn /fanOff
        // ----------------------------------------------------------
      } else if (text == "/fanAuto") {
        // Mengubah Mode Kipas ke otomatis
        VentMode = 0;
        controlVentilation();
        publishControlData();
        publishSensorData();
        saveControls();
        bot.sendMessage(chat_id, "✅ Mode kipas: *Otomatis* 🔄\n", "Markdown");

      } else if (text == "/fanOn") {
        // Mengubah Mode Kipas ke Manual ON
        VentMode = 1;
        controlVentilation();
        publishControlData();
        publishSensorData();
        saveControls();
        bot.sendMessage(chat_id, "Kipas: *Manual ON* 🟢\n", "Markdown");

      } else if (text == "/fanOff") {
        // Mengubah Mode Kipas ke Manual OFF
        VentMode = 2;
        controlVentilation();
        publishControlData();
        publishSensorData();
        saveControls();
        bot.sendMessage(chat_id, "Kipas: *Manual OFF* 🔴", "Markdown");

        // ----------------------------------------------------------
        // /setBatasSuhu [nilai]
        // ----------------------------------------------------------
      } else if (text.startsWith("/setBatasSuhu")) {
        // Membaca pesan setelah command sebagai parameter [nilai]
        String val = text.substring(14);
        val.trim();

        // Jika [nilai] kosong, kirim pesan peringatan
        if (val.length() == 0 || val.toFloat() == 0) {
          bot.sendMessage(chat_id, "⚠️ Format: `/setBatasSuhu 32`", "Markdown");
        }

        // Jika [nilai] sesuai, ubah setting kontrol batasSuhu
        else {
          TempThreshold = val.toFloat();
          controlVentilation();
          publishControlData();
          saveControls();
          bot.sendMessage(chat_id, "✅ Batas suhu: *" + String(TempThreshold, 1) + " °C*", "Markdown");
        }

        // ----------------------------------------------------------
        // /setBatasKelembapan [nilai]
        // ----------------------------------------------------------
      } else if (text.startsWith("/setBatasKelembapan")) {
        // Membaca pesan setelah command sebagai parameter [nilai]
        String val = text.substring(20);
        val.trim();

        // Jika [nilai] kosong, kirim pesan peringatan
        if (val.length() == 0 || val.toFloat() == 0) {
          bot.sendMessage(chat_id, "⚠️ Format: `/setBatasKelembapan 75`", "Markdown");
        }

        // Jika [nilai] sesuai, ubah setting kontrol
        else {
          HumidityThreshold = val.toFloat();
          controlVentilation();
          publishControlData();
          saveControls();
          bot.sendMessage(chat_id, "✅ Batas kelembapan: *" + String(HumidityThreshold, 1) + " %*", "Markdown");
        }
        // ----------------------------------------------------------
        // /setBatasGas [nilai]
        // ----------------------------------------------------------
      } else if (text.startsWith("/setBatasGas")) {
        // Membaca pesan setelah command sebagai parameter [nilai]
        String val = text.substring(13);
        val.trim();
        // Jika [nilai] kosong, kirim pesan peringatan
        if (val.length() == 0 || val.toFloat() == 0) {
          bot.sendMessage(chat_id, "⚠️ Format: `/setBatasGas 800`", "Markdown");
        }
        // Jika [nilai] sesuai, ubah setting kontrol
        else {
          GasThreshold = val.toFloat();
          controlVentilation();
          publishControlData();
          saveControls();
          bot.sendMessage(chat_id, "✅ Batas gas: *" + String(GasThreshold, 1) + " ppm*.", "Markdown");
        }
        // ----------------------------------------------------------
        // /seeSubscriber
        // ----------------------------------------------------------
      } else if (text == "/seeSubscriber") {
        if (subscriberCount == 0) {
          bot.sendMessage(chat_id, "📭 Belum ada subscriber notifikasi.", "Markdown");
        }

        else {
          String list = "📢 *Subscriber Notifikasi (" + String(subscriberCount) + ")* :\n\n";
          for (int j = 0; j < subscriberCount; j++) {
            list += String(j + 1) + ". `" + notifSubscribers[j] + "`\n";
          }
          list += "\n_Chat ID Anda:_" + String(chat_id);
          bot.sendMessage(chat_id, list, "Markdown");
        }
        // ----------------------------------------------------------
        // /addSubscribtion
        // ----------------------------------------------------------
      } else if (text.startsWith("/addSubs")) {
        String target_id = chat_id;

        if (addSubscriber(target_id)) {
          bot.sendMessage(target_id, "✅ Subscribtion telah didaftarkan.", "Markdown");
        } else if (findSubscriber(target_id) != -1) {
          bot.sendMessage(target_id, "ℹ️  Anda sudah terdaftar.", "Markdown");
        } else {
          bot.sendMessage(target_id, "❌ Daftar penuh (maks " + String(MAX_NOTIF_SUBSCRIBERS) + ").", "Markdown");
        }

        // ----------------------------------------------------------
        // /deleteSubscribtion
        // ----------------------------------------------------------
      } else if (text.startsWith("/deleteSubs")) {
        String target_id = target_id;

        if (removeSubscriber(target_id)) {
          bot.sendMessage(target_id, "🗑️ Subscribtion telah dihapus.", "Markdown");
        } else {
          bot.sendMessage(target_id, "⚠️ Anda belum terdaftar.", "Markdown");
        }
      } 
      
      else {
        bot.sendMessage(chat_id, "❓ Command tidak dikenal. Ketik /start.", "Markdown");
      }
    }

    numNewMessages = bot.getUpdates(bot.last_message_received + 1);
  }
}

// Fungsi bantu telegram

String buildStatusMessage() {
  String modeStr;
  if (VentMode == 0) modeStr = "🔄 Otomatis";
  else if (VentMode == 1) modeStr = "🟢 Manual ON";
  else modeStr = "🔴 Manual OFF";

  String msg = "📊 *STATUS SMART VENT SYSTEM*\n";
  msg += "─────────────────────\n";
  msg += "🌡️ Suhu       : " + String(TempValue, 1) + " °C  _(batas: " + String(TempThreshold, 1) + " °C)_\n";
  msg += "💧 Kelembapan : " + String(HumidityValue, 1) + " %   _(batas: " + String(HumidityThreshold, 1) + " %)_\n";
  msg += "💨 Gas        : " + String(GasValue, 1) + " ppm _(batas: " + String(GasThreshold, 1) + " ppm)_\n";
  msg += "─────────────────────\n";
  msg += "🔧 Mode Kipas : " + modeStr + "\n";
  msg += "💨 Kipas      : " + String(isVentOn ? "🟢 ON" : "🔴 OFF") + "\n";
  msg += "🌐 WiFi       : " + String(isWifiConnected ? "✅ Terhubung" : "❌ Terputus") + "\n";
  msg += "─────────────────────\n";
  msg += "📢 Subscriber : " + String(subscriberCount) + " orang\n";
  return msg;
}

// Kirimkan pesan status
void sendStatus(String chat_id) {
  bot.sendMessage(chat_id, buildStatusMessage(), "Markdown");
}

// Kirimkan pesan ke seluruh subscriber
void sendToAllSubscribers(String message) {
  for (int i = 0; i < subscriberCount; i++) {
    bot.sendMessage(notifSubscribers[i], message, "Markdown");
  }
}

// cari subsribed berdasarkan chat_id
int findSubscriber(String chat_id) {
  for (int i = 0; i < subscriberCount; i++) {
    if (notifSubscribers[i] == chat_id) return i;
  }
  return -1;
}

// Tambah subscriber lalu simpan ke flash
bool addSubscriber(String chat_id) {
  if (findSubscriber(chat_id) != -1) return false;
  if (subscriberCount >= MAX_NOTIF_SUBSCRIBERS) return false;
  notifSubscribers[subscriberCount++] = chat_id;
  saveSubscribers();
  return true;
}

// Hapus subscriber lalu simpan ulang ke flash
bool removeSubscriber(String chat_id) {
  int idx = findSubscriber(chat_id);
  if (idx == -1) return false;
  for (int i = idx; i < subscriberCount - 1; i++) {
    notifSubscribers[i] = notifSubscribers[i + 1];
  }
  subscriberCount--;
  saveSubscribers();
  return true;
}

void checkAndSendNotification() {
  if (subscriberCount == 0) return;

  bool kondisiBahaya = (TempValue >= TempThreshold) || (HumidityValue >= HumidityThreshold) || (GasValue >= GasThreshold);

  if (kondisiBahaya && !lastNotifCondition) {
    String msg = "⚠️ *PERINGATAN SMART VENT!*\n";
    msg += "Kondisi lingkungan melebihi batas!\n\n";
    if (TempValue >= TempThreshold) msg += "🌡️ Suhu: " + String(TempValue, 1) + " °C\n";
    if (HumidityValue >= HumidityThreshold) msg += "💧 Kelembapan: " + String(HumidityValue, 1) + " %\n";
    if (GasValue >= GasThreshold) msg += "💨 Gas: " + String(GasValue, 1) + " ppm\n";
    msg += "\n_Silahkan nyalakan ventilasi._";
    sendToAllSubscribers(msg);
    lastNotifCondition = true;

  } else if (!kondisiBahaya && lastNotifCondition) {
    sendToAllSubscribers("✅ *Kondisi Kembali Normal");
    lastNotifCondition = false;
  }
}
