#include <time.h>
#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <PubSubClient.h>
#include <Preferences.h>
#include <math.h>

#include "airflow/airflow_manager.h"
#include "co2/scd30_sensor.h"
#include "o2/dfrobot_sensor.h"
#include "indirect-calorimetry/indirect_calorimetry.h"
#include "screen/nextion.h"

// ===================== Global Objects =====================
IndirectCalorimetry ic;
static bool icInitialized = false;

volatile float last_CO2_ppm = NAN;
volatile float last_O2_pct = NAN;
AirflowSample lastAirflow;

WiFiClient espClient;
PubSubClient mqttClient(espClient);
Preferences prefs;

String deviceUID;
String mqttTopic;
String currentUserCode;

WiFiManager wm;

// ===================== MQTT CONFIG =====================
const char *MQTT_SERVER = "broker.mqtt.cool"; // ubah sesuai broker kamu
const int MQTT_PORT = 1883;

// ===================== UID GENERATOR =====================
String generateUID()
{
    String uid = "";
    uid.reserve(5); // avoids memory reallocation

    for (int i = 0; i < 5; i++)
    {
        int r = random(1, 7); // gives 1–6
        uid += char('0' + r); // convert to ASCII digit
    }

    return uid;
}

void connectWiFi()
{
    setText(GLOBAL_NOTE, "[WIFI] Connecting...");
    Serial.println("[WiFi] Connecting...");

    bool res = wm.autoConnect("ICalorimetry_AP");

    if (res)
    {
        Serial.print("[WiFi] Connected. IP: ");
        Serial.println(WiFi.localIP());

        setText(GLOBAL_NOTE, "[WIFI] Successfully Connected");
    }
    else
    {
        Serial.println("[WiFi] Failed to connect");

        setText(GLOBAL_NOTE, "[WIFI] Configure WiFi Manually Through Phone (Connect to: ICalorimetry_AP)");
    }
}

void mqtt_set_user_code(const String &code)
{
    currentUserCode = code;

    // Topic sekarang pakai userInputID (code) bukan deviceUID
    mqttTopic = "indirect_calorimetry/" + code + "/calorie";

    Serial.printf("[MQTT] Topic updated to: %s\n", mqttTopic.c_str());
    setText(GLOBAL_NOTE, "[MQTT] Data Streamed to " + code);
}

void initTime()
{
    const long gmtOffset_sec = 0; // kita simpan ke Firestore dalam UTC (Z)
    const int daylightOffset_sec = 0;

    configTime(gmtOffset_sec, daylightOffset_sec,
               "pool.ntp.org", "time.nist.gov");

    Serial.println("[TIME] Waiting for NTP...");
    setText(GLOBAL_NOTE, "[Time] Waiting for NTP");
    time_t now;
    struct tm timeinfo;
    int retry = 0;
    const int retry_max = 10;

    do
    {
        delay(1000);
        time(&now);
        gmtime_r(&now, &timeinfo);
        retry++;
    } while (timeinfo.tm_year < (2020 - 1900) && retry < retry_max);

    if (timeinfo.tm_year >= (2020 - 1900))
    {
        char buf[40];
        strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &timeinfo);
        Serial.print("[TIME] NTP synced: ");
        Serial.println(buf);
        setText(GLOBAL_NOTE, "[TIME] Successfully Initialized");
    }
    else
    {
        Serial.println("[TIME] NTP sync failed, still using epoch");
        setText(GLOBAL_NOTE, "[TIME] Failed Initializing, Using epoch");
    }
}

void initUID()
{
    prefs.begin("ic-config", false);
    deviceUID = prefs.getString("uid", "");
    if (deviceUID.isEmpty())
    {
        deviceUID = generateUID();
        prefs.putString("uid", deviceUID);
    }
    prefs.end();

    mqttTopic = "indirect_calorimetry/" + deviceUID + "/calorie";
    Serial.printf("[UID] This device UID: %s\n", deviceUID.c_str());
    Serial.printf("[MQTT Topic] %s\n", mqttTopic.c_str());
}

// ===================== MQTT HANDLER =====================
void reconnectMQTT()
{
    while (!mqttClient.connected())
    {
        Serial.print("[MQTT] Connecting...");
        setText(GLOBAL_NOTE, "[MQTT] Connecting...");
        if (mqttClient.connect(deviceUID.c_str()))
        {
            Serial.println("connected!");
            setText(GLOBAL_NOTE, "[MQTT] Successfully Connected");
        }
        else
        {
            Serial.print("failed, rc=");
            Serial.print(mqttClient.state());
            Serial.println(" retrying in 2s");
            setText(GLOBAL_NOTE, "[MQTT] Unable to Connect. Retrying");
            delay(2000);
        }
    }
}

// ===================== FreeRTOS Tasks =====================
void vCO2SensorTask(void *pvParameters)
{
    for (;;)
    {
        scd30_sensor_loop();
        float co2_ppm, t, rh;
        if (scd30_get_last(co2_ppm, t, rh))
        {
            last_CO2_ppm = co2_ppm;
            if (icInitialized)
                ic.setCO2ppm(co2_ppm);
        }
        vTaskDelay(pdMS_TO_TICKS(50)); // 20 Hz
    }
}

void vO2SensorTask(void *pvParameters)
{
    for (;;)
    {
        dfrobot_sensor_loop();
        float o2_pct;
        if (dfrobot_get_last(o2_pct))
        {
            last_O2_pct = o2_pct;
            if (icInitialized)
                ic.setO2Percent(o2_pct);
        }
        vTaskDelay(pdMS_TO_TICKS(50)); // 20 Hz
    }
}

void vAirflowTask(void *pvParameters)
{
    for (;;)
    {
        AirflowSample af = airflow_update();
        lastAirflow = af;

        if (icInitialized && airflow_minute_volume_ready())
        {
            float minute_volume_L, duration_s;
            airflow_pop_minute_volume(minute_volume_L, duration_s);
            ic.setMinuteVolume(minute_volume_L, duration_s);
        }

        vTaskDelay(pdMS_TO_TICKS(20)); // 50 Hz
    }
}

void vICTask(void *pvParameters)
{
    for (;;)
    {
        if (icInitialized)
        {
            ic.update(lastAirflow.nowMs);
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void vNextionTask(void *pvParameters)
{
    (void)pvParameters;

    uint32_t lastRefresh = 0;

    for (;;)
    {
        // Selalu baca RX dari TJC (kalau ada tombol / event lain)
        tjc_poll();
        // Setiap 2000 ms, kirim ulang state terakhir ke layar
        uint32_t now = millis();
        if (now - lastRefresh >= 2000)
        {
            tjc_apply_last_state();
            lastRefresh = now;
        }

        // Jangan terlalu sering, cukup 100 ms
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void vPrintAndMQTTTask(void *pvParameters)
{
    uint32_t lastPrint = 0;
    uint32_t lastICPrintMs = 0;

    for (;;)
    {
        uint32_t nowMs = lastAirflow.nowMs;

        // Reconnect MQTT if needed
        if (!mqttClient.connected())
            reconnectMQTT();
        mqttClient.loop();

        // Print live data every 100 ms
        if (nowMs - lastPrint >= 100)
        {
            lastPrint = nowMs;
            if (dfrobot_is_frc_done())
            {
                Serial.printf(
                    "%8lu | %7.1f | %10.1f | %7.3f | %9.0f | %6.3f\n",
                    (unsigned long)nowMs,
                    lastAirflow.dP_Pa,
                    lastAirflow.flow_mL_s_view,
                    lastAirflow.totalVolume_L,
                    last_CO2_ppm,
                    last_O2_pct);
            }
        }

        // Publish IC result every 1 minute result ready
        if (icInitialized && ic.minuteReady())
        {
            CalorimetryResult r = ic.popResult();

            // Print to Serial
            Serial.println(F("\n[=== INDIRECT CALORIMETRY RESULT ===]"));
            Serial.printf("VE   : %.2f L/min\n", r.VE_L_min);
            Serial.printf("VO2  : %.3f L/min\n", r.VO2_L_min);
            Serial.printf("VCO2 : %.3f L/min\n", r.VCO2_L_min);
            Serial.printf("RQ   : %.2f\n", r.RQ);
            Serial.printf("kcal : %.3f kcal/min\n", r.kcal_per_min);

            // Publish to MQTT
            char payload[128];
            snprintf(payload, sizeof(payload),
                     "{\"uid\":\"%s\",\"VE\":%.2f,\"VO2\":%.3f,\"VCO2\":%.3f,\"RQ\":%.2f,\"kcal\":%.3f}",
                     deviceUID.c_str(),
                     r.VE_L_min,
                     r.VO2_L_min,
                     r.VCO2_L_min,
                     r.RQ,
                     r.kcal_per_min);

            mqttClient.publish(mqttTopic.c_str(), payload);
            Serial.println(F("[MQTT] Calorimetry result published."));
            setText(GLOBAL_NOTE, "[MQTT] Data Streamed to Application");
            tjc_update_from_ic(&r, true);
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// ===================== SETUP =====================
void setup()
{
    Serial.begin(115200);
    delay(500);

    Serial.println(F("\n=== Indirect Calorimetry System (WiFi + MQTT + RTOS) ==="));

    Wire.begin(21, 22);
    Wire.setClock(100000);
    delay(500);

    tjc_setup();
    scd30_sensor_setup();
    delay(500);
    dfrobot_sensor_setup();
    airflow_setup();

    float FiO2 = 0.2093f;
    float FiCO2 = 0.0004f;
    ic.begin(FiO2, FiCO2);
    icInitialized = true;

    initUID();

    // // --- WiFi Manager ---
    // WiFiManager wm;
    // wm.autoConnect("ICalorimetry_AP");
    // Serial.print("[WiFi] Connected. IP: ");
    // Serial.println(WiFi.localIP());
    // setText(GLOBAL_NOTE, "[WIFI] Successfully Connected");
    connectWiFi();

    initTime();

    // --- MQTT Setup ---
    mqttClient.setServer(MQTT_SERVER, MQTT_PORT);

    // --- Tasks ---
    xTaskCreatePinnedToCore(vCO2SensorTask, "CO2Task", 4096, nullptr, 2, nullptr, 1);
    xTaskCreatePinnedToCore(vO2SensorTask, "O2Task", 4096, nullptr, 2, nullptr, 1);
    xTaskCreatePinnedToCore(vAirflowTask, "AirflowTask", 4096, nullptr, 2, nullptr, 1);
    xTaskCreatePinnedToCore(vICTask, "ICTask", 4096, nullptr, 1, nullptr, 1);
    xTaskCreatePinnedToCore(vPrintAndMQTTTask, "PrintMQTTTask", 8192, nullptr, 1, nullptr, 1);
    xTaskCreatePinnedToCore(vNextionTask, "NextionTask", 8192, nullptr, 2, nullptr, 1);
}

void loop()
{
    vTaskDelay(portMAX_DELAY);
}
