#include <Arduino.h>
#include <Wire.h>
#include "DFRobot_OxygenSensor.h"
#include "dfrobot_sensor.h"
#include "screen/nextion.h"

// ====== Hardware & I2C ======
const uint8_t O2_ADDR = 0x73; // alamat I2C sensor O2 kamu (0x73 dari scanner)
// const int SDA_PIN = 21;
// const int SCL_PIN = 22;

// ====== Kalibrasi dasar (dari hasil analisis) ======
// fase turun (eksalasi / O2 turun) -> hasil 2 titik
const float slope_down_base = 0.964f;
const float offset_down_base = 1.225f;

// fase naik (recovery / balik ke ambient) -> hasil global
const float slope_up_base = 1.0546f;
const float offset_up_base = -1.0998f;

// ====== Parameter pembacaan ======
const uint16_t AVG_N = 20;              // averaging internal library
const uint32_t SAMPLE_MS = 2000;        // interval baca 2 detik
const float DEAD_BAND = 0.002f;         // deadband arah (%Vol)
const uint32_t FRC_DURATION = 120000UL; // 2 menit fase FRC (ms)

// ====== State & objek ======
DFRobot_OxygenSensor oxygen;

enum TrendMode : uint8_t
{
    MODE_UNKNOWN = 0,
    MODE_DOWN,
    MODE_UP
};
enum Phase : uint8_t
{
    PHASE_FRC = 0,
    PHASE_DYNAMIC
};

TrendMode currentMode = MODE_UNKNOWN;
Phase currentPhase = PHASE_FRC;

float lastRaw = NAN;

// baseline (hasil rata-rata fase FRC)
float baseRaw = NAN;  // rata-rata raw
float baseTrue = NAN; // rata-rata corrected (FRC)

// offset efektif (setelah di-anchoring ke baseline)
float slope_down = slope_down_base;
float offset_down = offset_down_base;
float slope_up = slope_up_base;
float offset_up = offset_up_base;

// akumulasi untuk rata-rata FRC
double sumRawFRC = 0.0;
double sumTrueFRC = 0.0;
uint32_t countFRC = 0;

uint32_t startMillis = 0;

// cache nilai terakhir O2 (persen)
static float g_last_o2_percent = NAN;

// ====== Helper ======
static inline float applyCalDown(float raw)
{
    return slope_down * raw + offset_down;
}

static inline float applyCalUp(float raw)
{
    return slope_up * raw + offset_up;
}

// pilih mode naik/turun berdasarkan perubahan raw + deadband
static inline TrendMode decideMode(float rawNow, float rawPrev, TrendMode prevMode)
{
    if (isnan(rawPrev))
        return MODE_DOWN; // default awal
    if (rawNow < rawPrev - DEAD_BAND)
        return MODE_DOWN;
    if (rawNow > rawPrev + DEAD_BAND)
        return MODE_UP;
    return prevMode; // dalam deadband -> jangan gonta-ganti mode
}

void dfrobot_sensor_setup()
{
    // Serial.begin(115200);
    delay(200);

    // Wire.begin(SDA_PIN, SCL_PIN);

    if (!oxygen.begin(O2_ADDR))
    {
        Serial.println("Init O2 gagal! Cek wiring / alamat I2C.");
        setText(GLOBAL_NOTE, "[O2 Sensor] Check Wiring");
        while (1)
            delay(1000);
    }

    startMillis = millis();
    currentPhase = PHASE_FRC;

    // Pesan sederhana saat masuk fase kalibrasi FRC
    Serial.println("Kalibrasi Sensor O2 dan Sensor CO2 (FRC ~2 menit)...");
    setText(GLOBAL_NOTE, "[O2 Sensor] Calibrating Sensor. (~2 minutes)");
    Serial.println();
}

void dfrobot_sensor_loop()
{
    // 1) Baca raw dari sensor
    float rawNow = oxygen.getOxygenData(AVG_N);

    uint32_t now = millis();

    if (currentPhase == PHASE_FRC)
    {
        // ========= PHASE FRC (2 MENIT PERTAMA) =========
        // pakai garis DOWN base sebagai koreksi
        float corrDown = slope_down_base * rawNow + offset_down_base;

        // akumulasi untuk rata-rata baseline
        sumRawFRC += rawNow;
        sumTrueFRC += corrDown;
        countFRC++;

        // simpan nilai koreksi ke cache O2
        g_last_o2_percent = corrDown;

        // Tidak print spam setiap sample lagi di FRC,
        // cukup pesan di setup (di atas).

        // kalau sudah lewat 2 menit dan punya cukup data -> kunci baseline
        if (now - startMillis >= FRC_DURATION && countFRC > 10)
        {
            baseRaw = (float)(sumRawFRC / (double)countFRC);
            baseTrue = (float)(sumTrueFRC / (double)countFRC);

            // re-anchoring: paksa kedua garis (DOWN & UP) lewat titik (baseRaw, baseTrue)
            slope_down = slope_down_base;
            slope_up = slope_up_base;
            offset_down = baseTrue - slope_down * baseRaw;
            offset_up = baseTrue - slope_up * baseRaw;

            Serial.println("Kalibrasi O2 selesai, masuk mode dinamis.");
            setText(GLOBAL_NOTE, "[O2 Sensor] Calibration Successful");
            Serial.println();

            currentPhase = PHASE_DYNAMIC;
            currentMode = MODE_DOWN; // mulai dari DOWN saja
            lastRaw = rawNow;        // inisialisasi untuk deteksi arah
        }
    }
    else
    {
        // ========= PHASE DYNAMIC (setelah FRC) =========

        // 1) tentukan mode naik/turun
        currentMode = decideMode(rawNow, lastRaw, currentMode);

        // 2) apply kalibrasi sesuai mode, tapi SUDAH di-anchor ke baseline
        float corrected;
        if (currentMode == MODE_UP)
        {
            corrected = applyCalUp(rawNow);
        }
        else
        { // MODE_DOWN & MODE_UNKNOWN -> pakai DOWN
            corrected = applyCalDown(rawNow);
        }

        // simpan ke cache nilai O2
        g_last_o2_percent = corrected;

        // Tidak print per-sample di sini supaya Serial lebih bersih.
        lastRaw = rawNow;
    }

    delay(SAMPLE_MS);
}

// ===== Getter nilai terakhir O2 =====
bool dfrobot_get_last(float &o2_percent)
{
    if (isnan(g_last_o2_percent))
        return false;
    o2_percent = g_last_o2_percent;
    return true;
}

bool dfrobot_is_frc_done()
{
    return (currentPhase == PHASE_DYNAMIC);
}
