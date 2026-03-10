#include "firebase_user_codes.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "screen/nextion.h"

// TODO: ganti sesuai project kamu
static const char *PROJECT_ID = "skripsi-9e617";
static const char *API_KEY = "AIzaSyCMgjxacltLzDUv8pI9QqbBCLg4GQn7cWE"; // Web API key Firebase

// ⛔ TIDAK pakai titik koma di sini
bool firebase_lookupUserName(const String &code, String &outName, String &outUid)
{
    if (WiFi.status() != WL_CONNECTED)
    {
        Serial.println("[FB] WiFi not connected");
        setText(GLOBAL_NOTE, "[FIREBASE] WiFi is not");
        return false;
    }

    // URL Firestore REST:
    // GET https://firestore.googleapis.com/v1/projects/<PROJECT_ID>/databases/(default)/documents/userCodes/<code>?key=<API_KEY>
    String url = String("https://firestore.googleapis.com/v1/projects/") +
                 PROJECT_ID +
                 "/databases/(default)/documents/userCodes/" +
                 code +
                 "?key=" + API_KEY;

    Serial.print("[FB] GET ");
    Serial.println(url);

    WiFiClientSecure client;
    client.setInsecure(); // untuk dev; kalau mau aman, set root cert

    HTTPClient http;
    if (!http.begin(client, url))
    {
        Serial.println("[FB] http.begin failed");
        setText(GLOBAL_NOTE, "[FIREBASE] HTTP Failed");
        return false;
    }

    int httpCode = http.GET();
    if (httpCode == 200)
    {
        String payload = http.getString();
        Serial.println("[FB] 200 OK");
        setText(GLOBAL_NOTE, "[FIREBASE] HTTP Successful");
        Serial.println(payload);

        // Parse JSON Firestore
        StaticJsonDocument<1024> doc;
        DeserializationError err = deserializeJson(doc, payload);

        if (err)
        {
            Serial.print("[FB] JSON error: ");
            Serial.println(err.c_str());
            http.end();
            return false;
        }

        // Struktur Firestore:
        // {
        //   fields: {
        //     name: { stringValue: "NamaUser" },
        //     uid:  { stringValue: "UID-Firebase-Auth" }
        //   }
        // }
        JsonVariant nameField = doc["fields"]["name"]["stringValue"];
        JsonVariant uidField = doc["fields"]["uid"]["stringValue"];

        if (!nameField.is<String>())
        {
            Serial.println("[FB] 'name' field missing");
            http.end();
            return false;
        }
        if (!uidField.is<String>())
        {
            Serial.println("[FB] 'uid' field missing");
            http.end();
            return false;
        }

        outName = nameField.as<String>();
        outUid = uidField.as<String>();

        http.end();
        return true;
    }
    else if (httpCode == 404)
    {
        Serial.println("[FB] 404 Not Found (code tidak ada)");
        setText(GLOBAL_NOTE, "[FIREBASE] User Not Found");
    }
    else
    {
        Serial.print("[FB] HTTP error: ");
        Serial.println(httpCode);
        Serial.println(http.getString());
    }

    http.end();
    return false;
}

// ===================== WORKOUT LOGGING =====================

struct WorkoutPayload
{
    String firebaseUid;
    String userInputId;
    uint32_t durationMs;
    String activity;
    float kcalPerMin;
};

static String makeDateId()
{
    // Sederhana: DDMMYYYY. Butuh NTP agar waktunya benar.
    time_t now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);

    char buf[16];
    snprintf(buf, sizeof(buf), "%02d%02d%04d",
             t.tm_mday, t.tm_mon + 1, t.tm_year + 1900);
    return String(buf);
}

static int getDailyCounter()
{
    // TODO: kalau mau bener2 persisten, pakai Preferences (NVS).
    // Sekarang versi sederhana: hanya counter in-RAM.
    static String lastDate = "";
    static int counter = 0;

    String today = makeDateId();
    if (today != lastDate)
    {
        lastDate = today;
        counter = 0;
    }
    return counter++;
}

static void firebase_logWorkoutTask(void *pvParameters)
{
    WorkoutPayload *p = (WorkoutPayload *)pvParameters;

    if (WiFi.status() != WL_CONNECTED)
    {
        Serial.println("[FB-WO] WiFi not connected");
        setText(GLOBAL_NOTE, "[FIREBASE] WiFi Not Connected");
        delete p;
        vTaskDelete(NULL);
        return;
    }

    String dateId = makeDateId();
    int counter = getDailyCounter();
    String docId = dateId + String(counter); // "<<DDMMYYYY>>" + count++

    // Path: users/{UID}/userInputID/{docId}

    // String url = String("https://firestore.googleapis.com/v1/projects/") +
    //              PROJECT_ID +
    //              "/databases/(default)/documents/users/" +
    //              p->firebaseUid +
    //              "/userInputID?documentId=" + docId +
    //              "&key=" + API_KEY;

    // sesudah: auto-id dari Firestore (tidak akan 409)
    String url = String("https://firestore.googleapis.com/v1/projects/") +
                 PROJECT_ID +
                 "/databases/(default)/documents/users/" +
                 p->firebaseUid +
                 "/userInputID?key=" + API_KEY;

    Serial.print("[FB-WO] POST ");
    Serial.println(url);
    setText(GLOBAL_NOTE, "[FIREBASE] Successfully Posted To Firebase");

    // Hitung durasi & kalori total
    float durationSec = p->durationMs / 1000.0f;
    float durationMin = durationSec / 60.0f;
    float kcalTotal = p->kcalPerMin * durationMin;

    // Timestamp ISO kasar (butuh NTP supaya akurat)
    time_t now = time(nullptr);
    char tsBuf[40];
    strftime(tsBuf, sizeof(tsBuf), "%Y-%m-%dT%H:%M:%SZ", gmtime(&now));
    String timestampIso = String(tsBuf);

    // Build JSON Firestore
    // Durasi pakai detik, kalori pakai double
    String json =
        "{"
        "\"fields\":{"
        "\"Durasi\":{\"integerValue\":\"" +
        String((int)durationSec) + "\"},"
                                   "\"Timestamp\":{\"timestampValue\":\"" +
        timestampIso + "\"},"
                       "\"JenisAktivitas\":{\"stringValue\":\"" +
        p->activity + "\"},"
                      "\"Kalori\":{\"doubleValue\":" +
        String(kcalTotal, 3) + "},"
                               "\"UserInputId\":{\"stringValue\":\"" +
        p->userInputId + "\"}"
                         "}"
                         "}";

    Serial.print("[FB-WO] Payload: ");
    Serial.println(json);

    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;
    if (!http.begin(client, url))
    {
        Serial.println("[FB-WO] http.begin failed");
        setText(GLOBAL_NOTE, "[FIREBASE] HTTP Failed");
        delete p;
        vTaskDelete(NULL);
        return;
    }

    http.addHeader("Content-Type", "application/json");
    int httpCode = http.POST(json);

    Serial.print("[FB-WO] HTTP code: ");
    Serial.println(httpCode);
    if (httpCode > 0)
    {
        String payload = http.getString();
        Serial.println(payload);
    }

    http.end();
    delete p;
    vTaskDelete(NULL);
}

void firebase_logWorkoutAsync(
    const String &firebaseUid,
    const String &userInputId,
    uint32_t durationMs,
    const String &activity,
    float kcalPerMin)
{
    WorkoutPayload *p = new WorkoutPayload();
    p->firebaseUid = firebaseUid;
    p->userInputId = userInputId;
    p->durationMs = durationMs;
    p->activity = activity;
    p->kcalPerMin = kcalPerMin;

    xTaskCreatePinnedToCore(
        firebase_logWorkoutTask,
        "FBWorkout",
        8192,
        p,
        1,
        nullptr,
        1);
}
