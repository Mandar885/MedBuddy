#include <WiFi.h>
#include <Firebase_ESP_Client.h>
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

// ================= PINS =================
#define LED_PIN 15
#define BUZZER_PIN 4
#define VALIDATE_BUTTON 5

FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

// ================= GLOBALS =================
unsigned long long nextDoseTime = 0;
String nextCylinder = "";
String nextMedicineName = "";
String nextCourseName = "";

unsigned long long lastPrintedDose = 0;

unsigned long lastFirebaseCheck = 0;
const unsigned long firebaseInterval = 10000;

bool alertActive = false;
bool doseProcessed = false;
unsigned long alertStartMillis = 0;
unsigned long long lastHandledDoseTime = 0;
const unsigned long validationWindow = 120000;

// ====================================================
// WIFI
// ====================================================
void connectWiFi() {
  Serial.println("\nConnecting to WiFi...");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  WiFi.setSleep(false);
  Serial.println("\nWiFi Connected!");
  Serial.println(WiFi.localIP());
}

// ====================================================
// TIME
// ====================================================
void initTime() {
  Serial.println("Syncing time...");
  configTime(19800, 0, "pool.ntp.org");
  while (time(nullptr) < 100000) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nTime Synced!");
}

// ====================================================
// FIREBASE
// ====================================================
void initFirebase() {
  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;
  auth.user.email = USER_EMAIL;
  auth.user.password = USER_PASSWORD;

  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  Serial.print("Authenticating");
  while (auth.token.uid == "") {
    Serial.print(".");
    delay(300);
  }
  Serial.println("\nFirebase Authenticated!");
}

// ====================================================
// UPDATE STATUS + HISTORY + DECREMENT
// ====================================================
// ====================================================
// UPDATE STATUS + HISTORY + SMART COMPLETION
// ====================================================
void updateDoseStatus(String status) {

  unsigned long long validatedAt =
      (unsigned long long)time(nullptr) * 1000ULL;

  String basePath = "users/" + String(USER_UID) +
                    "/cylinders/" + nextCylinder;

  String schedulePath = basePath +
                        "/schedule/" + String(nextDoseTime);

  // ---------------- Update Schedule ----------------
  FirebaseJson updateJson;
  updateJson.set("status", status);
  updateJson.set("validatedAt", validatedAt);
  updateJson.set("source", "HARDWARE");

  Firebase.RTDB.updateNode(&fbdo, schedulePath, &updateJson);
  Serial.println("Schedule Updated → " + status);

  // ---------------- Write History ----------------
  String historyPath = "users/" + String(USER_UID) + "/doseHistory";

  FirebaseJson historyJson;
  historyJson.set("cylinder", nextCylinder);
  historyJson.set("courseName", nextCourseName);
  historyJson.set("medicineName", nextMedicineName);
  historyJson.set("doseTime", nextDoseTime);
  historyJson.set("status", status);
  historyJson.set("validatedAt", validatedAt);
  historyJson.set("source", "HARDWARE");

  Firebase.RTDB.pushJSON(&fbdo, historyPath, &historyJson);

  // ---------------- Decrement ONLY IF TAKEN ----------------
  if (status == "TAKEN") {

    int doseAmount = 0;
    int remaining = 0;

    Firebase.RTDB.getInt(&fbdo, basePath + "/doseAmount");
    doseAmount = fbdo.intData();

    Firebase.RTDB.getInt(&fbdo, basePath + "/remainingPillCount");
    remaining = fbdo.intData();

    remaining -= doseAmount;
    if (remaining < 0) remaining = 0;

    Firebase.RTDB.setInt(&fbdo,
        basePath + "/remainingPillCount",
        remaining);

    Serial.print("Remaining Pills → ");
    Serial.println(remaining);
  }

  // ---------------- SMART COMPLETION CHECK ----------------
  bool hasPending = false;

  if (Firebase.RTDB.getJSON(&fbdo, basePath + "/schedule")) {

    FirebaseJson &json = fbdo.jsonObject();
    size_t count = json.iteratorBegin();

    for (size_t i = 0; i < count; i++) {

      String key, value;
      int type;
      json.iteratorGet(i, type, key, value);

      FirebaseJsonData statusData;
      json.get(statusData, key + "/status");

      if (statusData.success &&
          statusData.to<String>() == "PENDING") {

        hasPending = true;
        break;
      }
    }

    json.iteratorEnd();
  }

  // ---------------- DELETE CYLINDER IF NO PENDING ----------------
  if (!hasPending) {

    Serial.println("All doses completed (No PENDING left)");

    Firebase.RTDB.setBool(&fbdo,
        basePath + "/completed",
        true);

    if (Firebase.RTDB.deleteNode(&fbdo, basePath)) {
      Serial.println("Cylinder Deleted (Freed Automatically)");
    } else {
      Serial.println("Cylinder Delete Failed: " + fbdo.errorReason());
    }
  }
}  // ✅ Properly closed function

// ====================================================
// CHECK CYLINDER (Silent)
// ====================================================
void checkCylinder(String cylinderName) {

  String basePath = "users/" + String(USER_UID) +
                    "/cylinders/" + cylinderName;

  if (!Firebase.RTDB.getJSON(&fbdo, basePath + "/schedule")) return;

  FirebaseJson &json = fbdo.jsonObject();
  size_t count = json.iteratorBegin();

  for (size_t i = 0; i < count; i++) {

    String key, value;
    int type;
    json.iteratorGet(i, type, key, value);

    unsigned long long doseTime =
        strtoull(key.c_str(), NULL, 10);

    FirebaseJsonData statusData;
    json.get(statusData, key + "/status");

    if (statusData.success &&
        statusData.to<String>() == "PENDING") {

      if (nextDoseTime == 0 || doseTime < nextDoseTime) {

        nextDoseTime = doseTime;
        nextCylinder = cylinderName;

        Firebase.RTDB.getString(&fbdo, basePath + "/medicineName");
        nextMedicineName = fbdo.stringData();

        Firebase.RTDB.getString(&fbdo, basePath + "/courseName");
        nextCourseName = fbdo.stringData();
      }
    }
  }

  json.iteratorEnd();
}

// ====================================================
// PRINT ONLY NEXT UPCOMING DOSE
// ====================================================
void checkNextDose() {

  nextDoseTime = 0;
  nextCylinder = "";
  nextMedicineName = "";
  nextCourseName = "";

  checkCylinder("C1");
  checkCylinder("C2");
  checkCylinder("C3");

  // ================= NO DOSE =================
  if (nextDoseTime == 0) {

    Serial.println("\n==============================");
    Serial.println("NO UPCOMING DOSE");
    Serial.println("==============================\n");

    return;
  }

  // ================= DOSE EXISTS =================

  time_t doseSeconds =
      (time_t)(nextDoseTime / 1000ULL);

  struct tm timeinfo;
  localtime_r(&doseSeconds, &timeinfo);

  char timeStr[10];
  sprintf(timeStr, "%02d:%02d",
          timeinfo.tm_hour,
          timeinfo.tm_min);

  Serial.println("\n==============================");
  Serial.println("NEXT UPCOMING DOSE");
  Serial.println("==============================");
  Serial.print("Course   : ");
  Serial.println(nextCourseName);
  Serial.print("Medicine : ");
  Serial.println(nextMedicineName);
  Serial.print("Cylinder : ");
  Serial.println(nextCylinder);
  Serial.print("Time     : ");
  Serial.println(timeStr);
  Serial.println("==============================\n");
}

// ====================================================
// ALERT CONTROL
// ====================================================
void handleAlert() {

  unsigned long long now =
      (unsigned long long)time(nullptr) * 1000ULL;

  if (!alertActive &&
      nextDoseTime != 0 &&
      nextDoseTime != lastHandledDoseTime &&
      now >= nextDoseTime) {

    alertActive = true;
    doseProcessed = false;
    alertStartMillis = millis();

    Serial.println("\n🚨 DOSE TIME MATCHED!");

    digitalWrite(LED_PIN, HIGH);
    digitalWrite(BUZZER_PIN, HIGH);
  }

  if (alertActive && !doseProcessed) {

    if (digitalRead(VALIDATE_BUTTON) == LOW) {

      Serial.println("✅ Dose Taken by User");

      digitalWrite(LED_PIN, LOW);
      digitalWrite(BUZZER_PIN, LOW);

      updateDoseStatus("TAKEN");

      lastHandledDoseTime = nextDoseTime;
      doseProcessed = true;
      alertActive = false;
    }

    if (millis() - alertStartMillis >= validationWindow) {

      Serial.println("❌ Dose Missed");

      digitalWrite(LED_PIN, LOW);
      digitalWrite(BUZZER_PIN, LOW);

      updateDoseStatus("MISSED");

      lastHandledDoseTime = nextDoseTime;
      doseProcessed = true;
      alertActive = false;
    }
  }
}

// ====================================================
// SETUP
// ====================================================
void setup() {

  Serial.begin(115200);

  pinMode(LED_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(VALIDATE_BUTTON, INPUT_PULLUP);

  digitalWrite(LED_PIN, LOW);
  digitalWrite(BUZZER_PIN, LOW);

  connectWiFi();
  initTime();
  initFirebase();
}

// ====================================================
// LOOP
// ====================================================
void loop() {

  if (!alertActive &&
      millis() - lastFirebaseCheck > firebaseInterval) {

    lastFirebaseCheck = millis();

    if (Firebase.ready()) {
      checkNextDose();
    }
  }

  handleAlert();
}