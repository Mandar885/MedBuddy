#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <time.h>

// ================= WIFI =================
#define WIFI_SSID "GalaxyM02f06f"
#define WIFI_PASSWORD "pskrn2002"

// ================= FIREBASE =================
#define API_KEY "AIzaSyBlGBKv5CNeksCADMZbbyumtBIlv-Lq-m4"
#define DATABASE_URL "https://pillmate-9955d-default-rtdb.firebaseio.com/"

#define USER_EMAIL "palshital11@gmail.com"
#define USER_PASSWORD "Shitalpal"
#define USER_UID "ExZ0LPfeDLOsKgVatHyANr1A8mn2"

// ================= FIREBASE OBJECTS =================
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

// ================= BLE =================
BLEScan* pBLEScan;
int scanTime = 3;

String nearestBeacon = "";
int strongestRSSI = -999;

bool boundaryCurrentlyActive = false;

// ====================================================
// WIFI CONNECT
// ====================================================
void connectWiFi() {
  Serial.println("\n📶 Connecting WiFi...");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\n✅ WiFi Connected");
}

// ====================================================
// FIREBASE INIT
// ====================================================
void initFirebase() {

  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;

  auth.user.email = USER_EMAIL;
  auth.user.password = USER_PASSWORD;

  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  Serial.print("🔐 Authenticating");
  while (auth.token.uid == "") {
    Serial.print(".");
    delay(300);
  }
  Serial.println("\n✅ Firebase Ready");
}

// ====================================================
// TIME SYNC
// ====================================================
void initTime() {
  configTime(19800, 0, "pool.ntp.org");
  while (time(nullptr) < 100000) {
    delay(200);
  }
}

// ====================================================
// BLE SCAN
// ====================================================
void scanBeacons() {

  nearestBeacon = "";
  strongestRSSI = -999;

  Serial.println("\n🔎 Scanning Beacons...");

  BLEScanResults results = pBLEScan->start(scanTime, false);

  for (int i = 0; i < results.getCount(); i++) {

    BLEAdvertisedDevice device = results.getDevice(i);
    String name = device.getName().c_str();
    int rssi = device.getRSSI();

    if (name == "B1" || name == "B2" ||
        name == "B3" || name == "B4") {

      Serial.print("Detected ");
      Serial.print(name);
      Serial.print(" RSSI: ");
      Serial.println(rssi);

      if (rssi > strongestRSSI) {
        strongestRSSI = rssi;
        nearestBeacon = name;
      }
    }
  }

  pBLEScan->clearResults();

  if (nearestBeacon != "") {
    Serial.print("🏆 Strongest: ");
    Serial.print(nearestBeacon);
    Serial.print(" RSSI: ");
    Serial.println(strongestRSSI);
  } else {
    Serial.println("❌ No Beacon Found");
  }
}

// ====================================================
// UPDATE LIVE TRACKING
// ====================================================
void updateLiveTracking(unsigned long long timestamp) {

  String path = "users/" + String(USER_UID) + "/liveTracking";

  FirebaseJson json;
  json.set("currentBeacon", nearestBeacon);
  json.set("lastUpdated", timestamp);

  if (Firebase.RTDB.setJSON(&fbdo, path, &json)) {
    Serial.println("📍 liveTracking updated");
  } else {
    Serial.println("❌ liveTracking failed: " + fbdo.errorReason());
  }
}

// ====================================================
// UPDATE BOUNDARY SAFETY
// ====================================================
void updateBoundary(unsigned long long timestamp) {

  String basePath = "users/" + String(USER_UID) + "/safety/boundary";

  // EXIT ZONE = B4
  if (nearestBeacon == "B4" && !boundaryCurrentlyActive) {

    Serial.println("🚨 EXIT ZONE DETECTED");

    FirebaseJson json;
    json.set("active", true);
    json.set("beaconId", "B4");
    json.set("detectedAt", timestamp);
    json.set("resolvedAt", 0);
    json.set("source", "WEARABLE");

    Firebase.RTDB.setJSON(&fbdo, basePath, &json);
    boundaryCurrentlyActive = true;
  }

  // If user leaves B4
  else if (nearestBeacon != "B4" && boundaryCurrentlyActive) {

    Serial.println("✅ EXIT RESOLVED");

    FirebaseJson json;
    json.set("active", false);
    json.set("resolvedAt", timestamp);

    Firebase.RTDB.updateNode(&fbdo, basePath, &json);
    boundaryCurrentlyActive = false;
  }
}

// ====================================================
// SETUP
// ====================================================
void setup() {

  Serial.begin(115200);

  connectWiFi();
  initTime();
  initFirebase();

  BLEDevice::init("");
  pBLEScan = BLEDevice::getScan();
  pBLEScan->setActiveScan(true);

  Serial.println("\n🚀 Wearable Ready\n");
}

// ====================================================
// LOOP
// ====================================================
void loop() {

  if (!Firebase.ready()) return;

  unsigned long long timestamp =
      (unsigned long long)time(nullptr) * 1000ULL;

  scanBeacons();

  if (nearestBeacon != "") {

    updateLiveTracking(timestamp);
    updateBoundary(timestamp);
  }

  delay(5000);
}