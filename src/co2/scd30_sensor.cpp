#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_SCD30.h>
#include "scd30_sensor.h"
#include "screen/nextion.h"

///////////////////////
// PIN & CONSTANTS
///////////////////////
// #define I2C_SDA_PIN 21
// #define I2C_SCL_PIN 22
#define SCD30_I2C_ADDR 0x61
#define MEAS_INTERVAL_SEC 2 // interval baca sensor 2 detik

// Global object
Adafruit_SCD30 scd30;

// hasil kalibrasi dari regresi least squares:
const float CO2_GAIN = 0.2626736f;  // a
const float CO2_OFFSET = 391.3472f; // b

// ====== cache nilai terakhir (untuk IC & monitoring) ======
static float g_last_co2_corr = NAN;
static float g_last_temp_c = NAN;
static float g_last_rh_pct = NAN;

///////////////////////
// I2C / SENSOR CHECK
///////////////////////
static bool checkI2C_SCD30()
{
    Wire.beginTransmission(SCD30_I2C_ADDR);
    uint8_t error = Wire.endTransmission();

    if (error == 0)
    {
        Serial.println(F("[I2C] SCD30 terdeteksi di alamat 0x61."));
        setText(GLOBAL_NOTE, "[CO2 Sensor] Fully Functional");
        return true;
    }
    else
    {
        Serial.print(F("[I2C] Gagal komunikasi dengan SCD30. Error code: "));
        setText(GLOBAL_NOTE, "[CO2 Sensor] Check Wiring");
        Serial.println(error);
        return false;
    }
}

///////////////////////
// UTILITIES
///////////////////////
static void printHelp()
{
    Serial.println(F("Perintah Serial yang tersedia:"));
    Serial.println(F("  HELP        -> tampilkan bantuan ini"));
    Serial.println(F("  STATUS      -> tampilkan status/config SCD30"));
    Serial.println(F("  FRC 400     -> Forced Re-Calibration ke 400 ppm (fresh air)"));
    Serial.println(F("  FRC <ppm>   -> FRC ke nilai lain, misal: FRC 800"));
    Serial.println(F("  ASC ON      -> enable Automatic Self-Calibration"));
    Serial.println(F("  ASC OFF     -> disable Automatic Self-Calibration"));
    Serial.println();
}

static void printStatus()
{
    Serial.println();
    Serial.println(F("===== STATUS SCD30 ====="));

    // Interval pengukuran
    uint16_t interval = scd30.getMeasurementInterval();
    Serial.print(F("Measurement interval : "));
    Serial.print(interval);
    Serial.println(F(" detik"));

    // Forced Recalibration reference
    uint16_t frcRef = scd30.getForcedCalibrationReference();
    Serial.print(F("FRC reference        : "));
    Serial.print(frcRef);
    Serial.println(F(" ppm"));

    // ASC status
    bool asc = scd30.selfCalibrationEnabled();
    Serial.print(F("ASC (auto calib)     : "));
    Serial.println(asc ? F("ENABLED") : F("DISABLED"));

    // Altitude offset
    uint16_t alt = scd30.getAltitudeOffset();
    Serial.print(F("Altitude offset      : "));
    Serial.print(alt);
    Serial.println(F(" meter"));

    // Temperature offset
    float toffset = scd30.getTemperatureOffset() / 100.0f;
    Serial.print(F("Temp offset          : "));
    Serial.print(toffset, 2);
    Serial.println(F(" C"));

    Serial.println(F("========================"));
    Serial.println();
}

static void handleSerialCommands()
{
    if (!Serial.available())
        return;

    String line = Serial.readStringUntil('\n');
    line.trim();
    if (line.length() == 0)
        return;

    // Buat versi uppercase untuk cek keyword
    String upper = line;
    upper.toUpperCase();

    if (upper == "HELP")
    {
        printHelp();
    }
    else if (upper == "STATUS")
    {
        printStatus();
    }
    else if (upper.startsWith("FRC"))
    {
        // Format: "FRC 400" atau "FRC 800"
        int sp = line.indexOf(' ');
        if (sp > 0 && sp + 1 < (int)line.length())
        {
            String valStr = line.substring(sp + 1);
            valStr.trim();
            int refPPM = valStr.toInt();

            if (refPPM >= 400 && refPPM <= 2000)
            {
                Serial.print(F("[CMD] FRC "));
                Serial.print(refPPM);
                Serial.println(F(" ppm"));

                if (scd30.forceRecalibrationWithReference((uint16_t)refPPM))
                {
                    Serial.println(F("[OK] Forced Re-Calibration berhasil."));
                    Serial.print(F("     FRC reference sekarang: "));
                    Serial.print(scd30.getForcedCalibrationReference());
                    Serial.println(F(" ppm"));
                }
                else
                {
                    Serial.println(F("[ERR] Gagal melakukan FRC (forceRecalibrationWithReference)."));
                }
            }
            else
            {
                Serial.println(F("[ERR] Nilai FRC harus 400 - 2000 ppm. Contoh: FRC 400"));
            }
        }
        else
        {
            Serial.println(F("[ERR] Format FRC salah. Contoh: FRC 400"));
        }
    }
    else if (upper == "ASC ON")
    {
        if (scd30.selfCalibrationEnabled(true))
        {
            Serial.println(F("[OK] ASC di-ENABLE."));
        }
        else
        {
            Serial.println(F("[ERR] Gagal mengubah status ASC."));
        }
    }
    else if (upper == "ASC OFF")
    {
        if (scd30.selfCalibrationEnabled(false))
        {
            Serial.println(F("[OK] ASC di-DISABLE."));
        }
        else
        {
            Serial.println(F("[ERR] Gagal mengubah status ASC."));
        }
    }
    else
    {
        Serial.print(F("[?] Perintah tidak dikenal: "));
        Serial.println(line);
        printHelp();
    }
}

///////////////////////
// SETUP
///////////////////////
void scd30_sensor_setup()
{
    // Serial.begin(115200);
    delay(200);

    Serial.println();
    Serial.println(F("==================================="));
    Serial.println(F("  TES SEDERHANA SENSOR SCD30"));
    Serial.println(F("==================================="));

    // Inisialisasi I2C
    // Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
    // Serial.print(F("[INIT] I2C mulai di SDA="));
    // Serial.print(I2C_SDA_PIN);
    // Serial.print(F(", SCL="));
    // Serial.println(I2C_SCL_PIN);

    while (true)
    {
        // // Cek I2C dulu
        // if (!checkI2C_SCD30())
        // {
        //     Serial.println(F("[ERROR] SCD30 tidak terdeteksi. Cek wiring/power!"));
        //     setText(GLOBAL_NOTE, "[CO2 Sensor] Not Detected");
        // }
        // else
        {
            // Coba inisialisasi SCD30
            if (scd30.begin())
            {
                // Sukses → keluar dari loop retry
                break;
            }
            Serial.println(F("[ERROR] Inisialisasi SCD30 gagal (scd30.begin() return false)."));
            setText(GLOBAL_NOTE, "[CO2 Sensor] Retry Initializing");
        }

        Serial.println(F("[RETRY] Coba lagi inisialisasi SCD30 dalam 5 detik..."));
        setText(GLOBAL_NOTE, "[CO2 Sensor] Retrying in 5");
        delay(5000);
    }

    // Set interval pengukuran di dalam sensor (min 2 detik)
    if (scd30.setMeasurementInterval(MEAS_INTERVAL_SEC))
    {
        Serial.print(F("[INIT] Interval pengukuran diset ke "));
        Serial.print(MEAS_INTERVAL_SEC);
        Serial.println(F(" detik."));
    }
    else
    {
        Serial.println(F("[WARN] Gagal set interval pengukuran, pakai default sensor."));
    }

    // Matikan ASC dulu supaya FRC tidak langsung dioverride
    if (scd30.selfCalibrationEnabled(false))
    {
        Serial.println(F("[INIT] ASC dimatikan (self calibration OFF)."));
    }
    else
    {
        Serial.println(F("[WARN] Gagal mematikan ASC (self calibration)."));
    }

    Serial.println(F("[OK] SCD30 inisialisasi berhasil."));
    Serial.println(F("[INFO] Sensor akan mengukur CO2 setiap beberapa detik."));
    Serial.println();
    printHelp();
    printStatus();
}

///////////////////////
// LOOP
///////////////////////
void scd30_sensor_loop()
{
    // 1) Baca data kalau sudah ready
    if (scd30.dataReady())
    {
        if (scd30.read())
        {
            float co2_raw = scd30.CO2;                        // ppm (raw dari sensor)
            float co2_corr = CO2_GAIN * co2_raw + CO2_OFFSET; // ppm (sudah dikalibrasi)
            float temp = scd30.temperature;                   // °C
            float rh = scd30.relative_humidity;               // %

            // simpan ke cache untuk IC / main
            g_last_co2_corr = co2_corr;
            g_last_temp_c = temp;
            g_last_rh_pct = rh;

            // Serial.print(F("CO2 = "));
            // Serial.print(co2_corr, 2);
            // Serial.println(F(" ppm"));

            //     // Tampilkan temperatur & RH hanya kalau temperatur > 30°C
            //     if (temp > 30.0f)
            //     {
            //         Serial.print(F("       T = "));
            //         Serial.print(temp, 2);
            //         Serial.print(F(" C, RH = "));
            //         Serial.print(rh, 2);
            //         Serial.println(F(" %"));
            //     }

            //     Serial.println();
        }
        else
        {
            Serial.println(F("[WARN] Gagal baca data dari SCD30."));
            setText(GLOBAL_NOTE, "[CO2 Sensor] Retrieve Data Warning");
        }
    }

    // 2) Cek command dari Serial
    // handleSerialCommands();

    // kecilkan beban CPU & traffic I2C
    delay(100);
}

// ===== Getter nilai terakhir =====
bool scd30_get_last(float &co2_ppm, float &temp_c, float &rh_pct)
{
    if (isnan(g_last_co2_corr))
        return false; // belum ada data
    co2_ppm = g_last_co2_corr;
    temp_c = g_last_temp_c;
    rh_pct = g_last_rh_pct;
    return true;
}
