#include "nextion.h"
#include "firebase/firebase_user_codes.h"

const char *GLOBAL_NOTE = "globalNote";

extern void mqtt_set_user_code(const String &code);

// Gunakan Serial2 untuk koneksi ke TJC/Nextion
// ESP32: TX2 = GPIO17, RX2 = GPIO16 (sesuai wiring kamu)
HardwareSerial &tjc = Serial2;

// ===================== STATE TERAKHIR (untuk IC) =====================
static void applyState();

static bool s_hasState = false;
static float s_kcal = 0.0f;
static float s_vo2 = 0.0f;
static float s_vco2 = 0.0f;
static float s_ve = 0.0f;

// ===================== STATE USER (HASIL LOOKUP FIREBASE) =====================
static bool s_userValid = false;
static String s_userName;
static String s_userNote;
static String s_userInputID = ""; // ID yang dimasukkin user (415126)
static String s_userFirebaseUid;  // UID Firebase Auth dari Firestore

// ===================== STATE WORKOUT SESSION =====================
static bool s_sessionActive = false;
static bool s_sessionRunning = false;
static uint32_t s_sessionStartMs = 0;
static uint32_t s_sessionAccumMs = 0;     // total ms (termasuk stop-start)
static String s_sessionActivity = "rest"; // default

static void workout_start();
static void workout_stop();
static void workout_finish();

// ===================== HELPER KIRIM PERINTAH =====================

static void sendRaw(const String &cmd)
{
    tjc.print(cmd);
    tjc.write(0xFF);
    tjc.write(0xFF);
    tjc.write(0xFF);

    // Debug ke Serial USB
    // DebugNextion
    // Serial.print("[TJC TX] ");
    // Serial.println(cmd);
}

void setText(const String &obj, const String &text)
{
    String cmd = obj + ".txt=\"" + text + "\"";
    sendRaw(cmd);
}

// ===================== APPLY STATE KE LAYAR (IC DISPLAY) =====================

static void applyState()
{
    char buf[32];

    float kcal = s_hasState ? s_kcal : 0.0f;
    float vo2 = s_hasState ? s_vo2 : 0.0f;
    float vco2 = s_hasState ? s_vco2 : 0.0f;
    float ve = s_hasState ? s_ve : 0.0f;

    // currentProgres & calorieText: kcal per min + " kkal" (4 decimal)
    snprintf(buf, sizeof(buf), "%.4f kkal", kcal);
    setText("currentProgres", buf);
    setText("calorieText", buf);

    // lastProgress1: VO2 (4 decimal)
    snprintf(buf, sizeof(buf), "%.4f O2", vo2);
    setText("lastProgress1", buf);

    // lastProgress2: VCO2 (4 decimal)
    snprintf(buf, sizeof(buf), "%.4f CO2", vco2);
    setText("lastProgress2", buf);

    // weightText: VE (3 decimal)
    snprintf(buf, sizeof(buf), "%.3f L/min VE", ve);
    setText("weightText", buf);

    if (s_userValid)
    {
        setText("nameText", s_userName);

        String note = s_userNote.length() > 0 ? s_userNote : String("User found");
        setText("noteText", note);
        setText("idText", note);
    }
}

// ===================== LOGIC INPUT ID (MIRIP BRIDGE) =====================

// valid hanya 1–6
static bool isValidInput(char c)
{
    return (c == '1' || c == '2' || c == '3' ||
            c == '4' || c == '5' || c == '6');
}

// ===================== PUBLIC API =====================

void tjc_setup()
{
    // Serial debug sudah diinisialisasi di main (Serial.begin)
    tjc.begin(9600, SERIAL_8N1, 16, 17); // RX=16, TX=17

    Serial.println(F("[TJC] Init Serial2 untuk TJC/Nextion"));

    // Awal boot: belum ada hasil IC → state 0
    s_hasState = false;
    s_kcal = s_vo2 = s_vco2 = s_ve = 0.0f;

    // Tampilkan default (0 kkal, 0 L/min, dst)
    applyState();
}

void tjc_poll()
{
    if (!tjc.available())
        return;

    String data = tjc.readStringUntil('\n');

    String cleanData = "";
    for (int i = 0; i < data.length(); i++)
    {
        if (isalnum((unsigned char)data[i]) || data[i] == '=')
            cleanData += data[i];
    }
    cleanData.trim();

    Serial.print("[TJC RAW LINE] ");
    Serial.println(cleanData);

    if (cleanData.length() == 0)
        return;

    // ✅ SEMUA parsing pakai cleanData
    if (cleanData.indexOf("start") != -1)
    {
        Serial.println("[TJC] start detected");
        workout_start();
        return;
    }
    if (cleanData.indexOf("stop") != -1)
    {
        Serial.println("[TJC] stop detected");
        workout_stop();
        return;
    }
    // if (cleanData.indexOf("fi") != -1)
    // {
    //     Serial.println("[TJC] finish detected");
    //     workout_finish();
    //     return;
    // }

    // int actIdx = cleanData.indexOf("activity=");
    // if (actIdx != -1)
    // {
    //     String act = cleanData.substring(actIdx + 9);
    //     act.trim();
    //     s_sessionActivity = act;
    //     Serial.print("[TJC] activity = ");
    //     Serial.println(s_sessionActivity);
    //     // lanjut, bisa saja ada digit juga
    // }

    // parse activity dulu (biar fi5activity=jog kebaca)
    int actIdx = cleanData.indexOf("activity=");
    if (actIdx != -1)
    {
        String act = cleanData.substring(actIdx + 9);
        act.trim();
        s_sessionActivity = act;
        Serial.print("[TJC] activity = ");
        Serial.println(s_sessionActivity);
    }

    // baru cek finish
    if (cleanData.indexOf("fi") != -1)
    {
        Serial.println("[TJC] finish detected");
        workout_finish();
        return;
    }

    // ✅ ini sekarang pasti kebaca
    if (cleanData == "do")
    {
        // OPTIONAL: debounce biar "do" tidak kepanggil berkali-kali
        static uint32_t lastDoMs = 0;
        uint32_t now = millis();
        if (now - lastDoMs < 800)
            return;
        lastDoMs = now;

        if (s_userInputID.length() == 0)
        {
            setText("noteText", "Enter code first");
            return;
        }

        String name, uid;
        if (firebase_lookupUserName(s_userInputID, name, uid))
        {
            s_userValid = true;
            s_userName = name;
            s_userNote = "Connected";
            s_userFirebaseUid = uid;
            mqtt_set_user_code(s_userInputID);
            applyState();
        }
        else
        {
            s_userValid = false;
            setText("nameText", "Code not found");
            setText("noteText", "User not found");
            s_userInputID = "";
        }
        return;
    }

    // digit input (1–6) juga parse dari cleanData
    for (int i = 0; i < cleanData.length(); i++)
    {
        char c = cleanData[i];
        if (isValidInput(c))
            s_userInputID += c;
    }

    Serial.print("User Input ID (String): ");
    Serial.println(s_userInputID);
}

static void workout_start()
{
    if (!s_sessionActive)
    {
        s_sessionActive = true;
        s_sessionAccumMs = 0;
    }
    s_sessionRunning = true;
    s_sessionStartMs = millis();
    Serial.println("[WO] start");
}

static void workout_stop()
{
    if (s_sessionActive && s_sessionRunning)
    {
        uint32_t now = millis();
        s_sessionAccumMs += (now - s_sessionStartMs);
        s_sessionRunning = false;
        Serial.printf("[WO] stop, totalMs=%lu\n", (unsigned long)s_sessionAccumMs);
    }
}

static void workout_finish()
{
    if (!s_sessionActive)
        return;

    // kalau masih running, tutup dulu
    if (s_sessionRunning)
    {
        uint32_t now = millis();
        s_sessionAccumMs += (now - s_sessionStartMs);
        s_sessionRunning = false;
    }

    uint32_t durationMs = s_sessionAccumMs;
    Serial.printf("[WO] finish, durationMs=%lu, activity=%s\n",
                  (unsigned long)durationMs,
                  s_sessionActivity.c_str());

    // Estimasi kalori total berdasarkan kcal/min terakhir
    float durationMin = durationMs / 60000.0f;
    float totalKcal = s_kcal * durationMin; // s_kcal dari IC (kcal/min)

    // Sementara pakai s_userInputID sebagai UID & userInputId (bisa dipisah nanti)
    if (s_userInputID.length() > 0)
    {
        firebase_logWorkoutAsync(
            s_userFirebaseUid,
            s_userInputID, // userInputId
            durationMs,
            s_sessionActivity,
            s_kcal);
    }
    else
    {
        Serial.println("[WO] WARNING: s_userInputID kosong, tidak kirim ke Firebase");
    }

    // reset session
    s_sessionActive = false;
    s_sessionRunning = false;
    s_sessionAccumMs = 0;
}

// void tjc_poll()
// {
//     // Mirip dengan bridge: kumpulin string per-baris (sampai '\n')
//     static String lineBuf;     // buffer satu baris dari Nextion
//     static String userInputID; // kumpulan digit 1–6 dari user

//     while (tjc.available())
//     {
//         uint8_t b = tjc.read();
//         Serial.printf("[TJC RAW] 0x%02X '%c'\n", b,
//                       (b >= 32 && b <= 126) ? (char)b : '.');

//         char c = (char)b;

//         // kalau pakai newline sebagai pemisah (seperti di bridge)
//         if (c == '\r' || c == '\n')
//         {
//             if (lineBuf.length() > 0)
//             {
//                 String data = lineBuf;
//                 lineBuf = "";

//                 data.trim();
//                 if (data.length() == 0)
//                     continue;

//                 Serial.print("[TJC LINE] ");
//                 Serial.println(data);

//                 // === kalau data == "do" → seperti di contoh bridge ===
//                 if (data == "do")
//                 {
//                     String name;
//                     int userInputIDInt = userInputID.toInt();
//                     Serial.print("userInputID (String): ");
//                     Serial.println(userInputID);
//                     Serial.print("userInputIDInt (Integer): ");
//                     Serial.println(userInputIDInt);

//                     // lookup ke Firebase berdasarkan userInputID (String)
//                     if (firebase_lookupUserName(userInputID, name))
//                     {
//                         Serial.print("[TJC] Firebase name = ");
//                         Serial.println(name);

//                         setText("nameText", name);         // tampilkan nama user
//                         setText("noteText", "User found"); // <-- tambahan baru
//                     }
//                     else
//                     {
//                         Serial.println("[TJC] Code not found / error");

//                         setText("nameText", "Code not found");
//                         setText("noteText", "User not found"); // <-- tambahan baru
//                     }

//                     // reset setelah 'do' (kalau mau mulai dari awal)
//                     userInputID = "";
//                 }
//                 else
//                 {
//                     // === selain "do", treat sebagai kumpulan digit 1–6 ===
//                     for (int i = 0; i < data.length(); i++)
//                     {
//                         char incomingChar = data.charAt(i);
//                         if (isValidInput(incomingChar))
//                         {
//                             userInputID += incomingChar;
//                         }
//                         else
//                         {
//                             Serial.print("Karakter tidak valid: ");
//                             Serial.println(incomingChar);
//                         }
//                     }

//                     Serial.print("User Input ID (String): ");
//                     Serial.println(userInputID);
//                 }
//             }
//         }
//         else
//         {
//             // kumpulin di buffer sampai ketemu newline
//             lineBuf += c;
//         }
//     }
// }

void tjc_update_from_ic(const CalorimetryResult *result, bool hasResult)
{
    if (hasResult && result != nullptr)
    {
        s_hasState = true;
        s_kcal = result->kcal_per_min;
        s_vo2 = result->VO2_L_min;
        s_vco2 = result->VCO2_L_min;
        s_ve = result->VE_L_min;
    }
    else
    {
        // Kalau mau clear kembali 0
        s_hasState = false;
        s_kcal = s_vo2 = s_vco2 = s_ve = 0.0f;
    }

    // Setiap kali ada update dari IC, langsung kirim ke layar
    applyState();
}

void tjc_apply_last_state()
{
    // Dipanggil dari task Nextion secara periodik
    applyState();
}