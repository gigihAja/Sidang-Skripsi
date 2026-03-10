#pragma once
#include <Arduino.h>

// State ringkas airflow yang bisa dipakai main untuk display
struct AirflowSample
{
    uint32_t nowMs;       // waktu sample (ms)
    float dP_Pa;          // delta P (Pa)
    float flow_mL_s;      // flow mentah (mL/s, setelah threshold & clamp)
    float flow_mL_s_view; // flow yang sudah di-smoothing untuk tampilan
    float totalVolume_L;  // volume terintegrasi sejak reset (L)
};

// Inisialisasi modul airflow (ADS1115, venturi, parameter, zero awal, dsb)
void airflow_setup();

// Dipanggil berkala dari task utama (misal setiap loop di vMainTask).
// - Meng-handle: baca ADS, zero/span command di Serial, tombol kalibrasi,
//   integrasi volume (trapezoid + anti-spike).
// - Mengembalikan sample airflow terbaru.
AirflowSample airflow_update();

// Minute-volume (60 detik) dari airflow.
// Dipakai untuk di-feed ke IndirectCalorimetry::setMinuteVolume().
bool airflow_minute_volume_ready();
void airflow_pop_minute_volume(float &volume_L, float &duration_s);
