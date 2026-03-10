#include "airflow_manager.h"
#include <math.h>
#include "airflow/ads_reader.h"
#include "airflow/calculate.h"
#include "screen/nextion.h"

// ===================== Geometry (SI) =====================
static constexpr float INLET_DIAMETER_M = 0.0370f;  // 3.7 cm
static constexpr float THROAT_DIAMETER_M = 0.0150f; // 1.5 cm
static constexpr float AIR_DENSITY = 1.225f;        // kg/m^3

// ===================== I2C / ADS =====================
static constexpr int SDA_PIN = 21;
static constexpr int SCL_PIN = 22;
static ADSReader ads(0x48, GAIN_TWO, SDA_PIN, SCL_PIN); // A1-A3 diff

// ===================== Button (ZERO + RESET) =====================
static constexpr int CAL_BUTTON_PIN = 18; // active-LOW
static constexpr uint32_t DEBOUNCE_MS = 50;

// ===================== MPX10DP Scale =====================
// Bisa di-set runtime dg "K <Pa>"
static float SCALE_PA_PER_VOLT = 4000.0f; // Pa/V (placeholder)

// ===================== Filters & thresholds =====================
static constexpr float LPF_ALPHA = 0.2f;
static constexpr int MA_WINDOW = 5;

// Fix zero-creep:
static constexpr float DP_DEADBAND_PA = 0.1f;    // set ΔP=0 jika |ΔP| < 0.1 Pa
static constexpr float FLOW_IGNORE_ML_S = 20.0f; // ignore <20 mL/s untuk integrasi

// ====== ANTI SPIKE ======
static constexpr float FLOW_MAX_ML_S = 4000.0f; // max 4 L/s (napas manusia tinggi)
static constexpr float DT_MAX_S = 0.5f;         // dt max 0.5 s untuk integrasi
static constexpr float DV_MAX_L = 1.0f;         // max tambahan volume 1 L per step

// ===================== Calc object =====================
static Calculate calc(INLET_DIAMETER_M, THROAT_DIAMETER_M, AIR_DENSITY);

// ===================== State airflow =====================
static float zeroVolt = 0.0f;
static float totalVolume_L = 0.0f; // integrasi volume TOTAL (L)
static float last_Q_L_s = 0.0f;
static uint32_t lastMs = 0;

// Moving average utk tampilan smooth
static float maBuf[MA_WINDOW];
static int maIdx = 0;
static bool maFull = false;

// ===================== State minute-volume =====================
static float minuteStartVolume_L = 0.0f;
static uint32_t minuteStartMs = 0;
static bool minuteReady = false;
static float minuteVolume_L_last = 0.0f;
static float minuteDuration_s_last = 60.0f;

// ===================== Utils (internal) =====================
static float applyMA(float x)
{
    maBuf[maIdx] = x;
    maIdx = (maIdx + 1) % MA_WINDOW;
    if (maIdx == 0)
        maFull = true;
    float s = 0;
    int n = maFull ? MA_WINDOW : maIdx;
    if (n <= 0)
        return x;
    for (int i = 0; i < n; ++i)
        s += maBuf[i];
    return s / n;
}

static void clearSession()
{
    totalVolume_L = 0.0f;
    last_Q_L_s = 0.0f;
    Serial.println(F("[RESET] total volume & buffers cleared"));
    setText(GLOBAL_NOTE, "[Airflow Sensor] Total Volume is Set to Zero");
}

// Zero offset (avg) + RESET totals
static void doZero()
{
    Serial.println(F("[ZERO] Mulai... pastikan dua port setara tekanan"));
    setText(GLOBAL_NOTE, "[Airflow Sensor] Calibrating Sensor (~30 Seconds)");
    float sum = 0.0f;
    const int N = 2000; // banyak sample utk zero
    for (int i = 0; i < N; ++i)
    {
        sum += ads.readDiffVoltFiltered();
        delay(5);
    }
    zeroVolt = sum / N;
    Serial.print(F("[ZERO] zeroVolt="));
    Serial.print(zeroVolt, 6);
    Serial.println(F(" V"));
    clearSession();

    // restart minute tracking
    minuteStartVolume_L = totalVolume_L;
    minuteStartMs = millis();
}

// Span: set SCALE Pa/V using current Vdiff at known Pa
static void applySpanFromPa(float known_Pa)
{
    float vdiff = ads.readDiffVoltFiltered() - zeroVolt;
    if (fabsf(vdiff) < 1e-6f)
    {
        Serial.println(F("[SPAN] Vdiff ~0 V. Naikkan ΔP lalu ulangi."));
        return;
    }
    SCALE_PA_PER_VOLT = known_Pa / vdiff;
    Serial.print(F("[SPAN] Set SCALE_PA_PER_VOLT = "));
    Serial.print(SCALE_PA_PER_VOLT, 2);
    Serial.println(F(" Pa/V"));
}

static void printParams()
{
    Serial.println(F("==== PARAMETER AIRFLOW ===="));
    Serial.print(F("Inlet Dia  (m): "));
    Serial.println(INLET_DIAMETER_M, 6);
    Serial.print(F("Throat Dia (m): "));
    Serial.println(THROAT_DIAMETER_M, 6);
    Serial.print(F("Air density  : "));
    Serial.println(AIR_DENSITY, 3);
    Serial.print(F("Scale Pa/V   : "));
    Serial.println(SCALE_PA_PER_VOLT, 2);
    Serial.print(F("K (mL/s)/sqrt(Pa): "));
    Serial.println(calc.k_mLs(), 3);
    Serial.println(F("==========================="));
}

static void printHelp()
{
    Serial.println();
    Serial.println(F("Commands (airflow):"));
    Serial.println(F("  ?        : help airflow"));
    Serial.println(F("  Z        : ZERO + reset total volume"));
    Serial.println(F("  K <Pa>   : span set SCALE = Pa / Vdiff_now"));
    Serial.println(F("  R        : reset total volume only"));
    Serial.println(F("  P        : print airflow parameters"));
    Serial.println();
}

static void handleSerial()
{
    static String line;
    while (Serial.available())
    {
        char c = (char)Serial.read();
        // bisa dihilangkan kalau mau console lebih bersih
        // Serial.print("Received: ");
        // Serial.println(c);

        if (c == '\n' || c == '\r')
        {
            if (line.length())
            {
                line.trim();
                if (line.equalsIgnoreCase("?"))
                    printHelp();
                else if (line.equalsIgnoreCase("Z"))
                    doZero();
                else if (line.equalsIgnoreCase("R"))
                {
                    clearSession();
                }
                else if (line.equalsIgnoreCase("P"))
                    printParams();
                else if (line.startsWith("K "))
                {
                    float pa = line.substring(2).toFloat();
                    if (pa > 0)
                        applySpanFromPa(pa);
                    else
                        Serial.println(F("[ERR] Format: K <Pa>, contoh: K 500"));
                }
                else
                {
                    Serial.println(F("[?] Tidak dikenal. Ketik '?' untuk help airflow."));
                }
                line = "";
            }
        }
        else
            line += c;
    }
}

// ===================== Public API =====================

void airflow_setup()
{
    pinMode(CAL_BUTTON_PIN, INPUT_PULLUP);

    ads.setClock(100000);
    ads.setLPFAlpha(LPF_ALPHA);

    Serial.println(F("[AIRFLOW] Init ADS1115 & venturi"));

    if (!ads.beginAuto())
    {
        Serial.println(F("[ERR] ADS1115 not found. Cek wiring & ADDR (0x48..0x4B)."));
        setText(GLOBAL_NOTE, "[Airflow Sensor] Check Wiring");
        ads.i2cScan();
        while (1)
            delay(300);
    }

    printParams();
    printHelp();

    doZero(); // zero awal + reset volume

    lastMs = millis();
    last_Q_L_s = 0.0f;
    minuteStartVolume_L = totalVolume_L;
    minuteStartMs = lastMs;

    Serial.println(F("[AIRFLOW] Ready.\n"));
    setText(GLOBAL_NOTE, "[Airflow Sensor] Sensor is Ready");
}

AirflowSample airflow_update()
{
    AirflowSample sample{};
    uint32_t nowMs = millis();
    sample.nowMs = nowMs;

    // ====== handle serial command untuk airflow ======
    handleSerial();

    // ====== Button ZERO/RESET (active LOW) + debounce ======
    static bool lastBtn = HIGH;
    static uint32_t lastDeb = 0;
    bool b = digitalRead(CAL_BUTTON_PIN);
    uint32_t now = nowMs;
    if (b != lastBtn)
        lastDeb = now;
    if (now - lastDeb > DEBOUNCE_MS)
    {
        static bool armed = true;
        if (b == LOW && armed)
        {
            doZero();
            armed = false;
        }
        if (b == HIGH)
            armed = true;
    }
    lastBtn = b;

    // 1) Vdiff (V) dikoreksi zeroVolt
    float vdiff = ads.readDiffVoltFiltered() - zeroVolt;

    // 2) Volt -> ΔP (Pa), apply deadband
    float dP_Pa = vdiff * SCALE_PA_PER_VOLT;
    if (fabsf(dP_Pa) < DP_DEADBAND_PA)
        dP_Pa = 0.0f;
    if (dP_Pa < 0)
        dP_Pa = 0;

    // 3) ΔP -> Q (mL/s) [forward]
    float Q_mL_s = calc.airflow_mLs(dP_Pa, 0.0f);
    if (Q_mL_s < FLOW_IGNORE_ML_S)
        Q_mL_s = 0.0f; // ignore flow kecil

    // ===== ANTI SPIKE FLOW =====
    if (Q_mL_s > FLOW_MAX_ML_S)
        Q_mL_s = FLOW_MAX_ML_S;

    float Q_mL_s_view = applyMA(Q_mL_s);

    // 4) Integrasi total volume (trapezoid) + ANTI SPIKE
    float dt_s = (nowMs - lastMs) / 1000.0f;

    // clamp dt_s supaya kalau loop ngelag tidak bikin loncat volume
    if (dt_s < 0.0f)
        dt_s = 0.0f;
    if (dt_s > DT_MAX_S)
        dt_s = DT_MAX_S;

    lastMs = nowMs;

    float Q_L_s = Q_mL_s / 1000.0f; // mL/s -> L/s

    float dV_L = 0.5f * (last_Q_L_s + Q_L_s) * dt_s;
    if (dV_L > DV_MAX_L)
        dV_L = DV_MAX_L; // batasi max penambahan volume per step

    totalVolume_L += dV_L;
    last_Q_L_s = Q_L_s;

    // ====== MINUTE VOLUME tracking (60 detik) ======
    float minuteElapsed_s = (nowMs - minuteStartMs) / 1000.0f;
    if (minuteElapsed_s >= 60.0f)
    {
        float minute_volume_L = totalVolume_L - minuteStartVolume_L;
        if (minute_volume_L < 0.0f)
            minute_volume_L = 0.0f;

        minuteVolume_L_last = minute_volume_L;
        minuteDuration_s_last = minuteElapsed_s;
        minuteReady = true;

        // reset untuk menit berikutnya
        minuteStartVolume_L = totalVolume_L;
        minuteStartMs = nowMs;
    }

    // isi sample untuk dikembalikan ke main
    sample.dP_Pa = dP_Pa;
    sample.flow_mL_s = Q_mL_s;
    sample.flow_mL_s_view = Q_mL_s_view;
    sample.totalVolume_L = totalVolume_L;

    return sample;
}

bool airflow_minute_volume_ready()
{
    return minuteReady;
}

void airflow_pop_minute_volume(float &volume_L, float &duration_s)
{
    volume_L = minuteVolume_L_last;
    duration_s = minuteDuration_s_last;
    minuteReady = false;
}
