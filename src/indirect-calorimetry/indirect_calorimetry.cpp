#include "indirect_calorimetry.h"
#include <math.h>

// Persamaan Weir (non-protein)
// Energi (kcal/min) = 3.941 × VO₂ + 1.106 × VCO₂  [VO₂,VCO₂ dalam L/min]
static inline float weir_kcal_per_min(float VO2_L_min, float VCO2_L_min)
{
    return 3.941f * VO2_L_min + 1.106f * VCO2_L_min;
}

void IndirectCalorimetry::begin(float FiO2, float FiCO2)
{
    FiO2_ = FiO2;
    FiCO2_ = FiCO2;

    win_start_ = millis();
    minute_start_ = win_start_;

    // flow_sum_mL_s_ = 0.0;  // DEPRECATED
    // flow_count_     = 0;    // DEPRECATED

    seg_count_ = 0;
    sum_FeO2_ = 0.0;
    sum_FeCO2_ = 0.0;
    minute_VE_L_min_ = NAN;
    have_minute_volume_ = false;

    minute_ready_ = false;
    last_result_ = {};
}

//
// ======= MINUTE VOLUME (BARU) =======
//
void IndirectCalorimetry::setMinuteVolume(float minute_volume_L, float duration_s)
{
    if (duration_s <= 0.0f)
        duration_s = 60.0f; // fallback

    float duration_min = duration_s / 60.0f;
    if (duration_min <= 0.0f)
        duration_min = 1.0f; // safety

    // VE = total volume (L) / durasi (menit)
    minute_VE_L_min_ = minute_volume_L / duration_min;
    have_minute_volume_ = true;

    // Coba finalize kalau gas (3×20s) sudah siap
    finalizeMinuteIfReady();
}

//
// ======= FLOW (DEPRECATED) =======
//
void IndirectCalorimetry::feedAirflow(float /*flow_mL_s*/)
{
    // Versi baru: VE tidak lagi dihitung dari flow di sini.
    // Fungsi ini dibiarkan NO-OP agar kode lama tetap kompatibel.
    // flow_sum_mL_s_ += flow_mL_s;
    // flow_count_++;
}

void IndirectCalorimetry::setO2Percent(float o2_percent)
{
    if (!isnan(o2_percent))
        FeO2_ = o2_percent / 100.0f;
}

void IndirectCalorimetry::setCO2ppm(float co2_ppm)
{
    if (!isnan(co2_ppm))
        FeCO2_ = co2_ppm / 1e6f;
}

void IndirectCalorimetry::update(uint32_t now_ms)
{
    if (now_ms - win_start_ >= WINDOW_MS)
    {
        closeWindow20s(now_ms);
    }
}

//
// ======= WINDOW 20 DETIK: hanya urus GAS =======
//
void IndirectCalorimetry::closeWindow20s(uint32_t now_ms)
{
    // Snapshot gas ekspirasi (pakai nilai terakhir kalau belum ada)
    snap_O2_frac_ = isnan(FeO2_) ? FiO2_ : FeO2_;
    snap_CO2_frac_ = isnan(FeCO2_) ? FiCO2_ : FeCO2_;

    // Akumulasi untuk rata-rata 1 menit (3×20 detik)
    if (seg_count_ == 0)
        minute_start_ = win_start_; // waktu mulai menit baru

    sum_FeO2_ += snap_O2_frac_;
    sum_FeCO2_ += snap_CO2_frac_;
    seg_count_++;

    // Reset window waktu
    win_start_ = now_ms;

    // Coba finalize kalau sudah 3 segmen & minute volume sudah ada
    finalizeMinuteIfReady();
}

void IndirectCalorimetry::finalizeMinuteIfReady()
{
    // Butuh 3 window (±60 detik) dan minute volume dari main
    if (seg_count_ < 3)
        return;
    if (!have_minute_volume_)
        return;

    // Rata-rata fraksi gas untuk menit ini
    float FeO2_avg = (float)(sum_FeO2_ / (double)seg_count_);
    float FeCO2_avg = (float)(sum_FeCO2_ / (double)seg_count_);

    // Haldane correction ratio (pakai rata-rata FeCO2 menit)
    float haldane_ratio = (1.0f - FiCO2_) / (1.0f - FeCO2_avg);

    // Minute volume (VE) sudah dalam L/min
    float VE = minute_VE_L_min_;

    float VO2 = VE * (FiO2_ - FeO2_avg * haldane_ratio);
    float VCO2 = VE * (FeCO2_avg - FiCO2_);

    if (VO2 < 0.0f)
        VO2 = 0.0f;
    if (VCO2 < 0.0f)
        VCO2 = 0.0f;

    float RQ = (VO2 > 1e-6f) ? (VCO2 / VO2) : NAN;
    float kcal = weir_kcal_per_min(VO2, VCO2);

    last_result_.t_start_ms = minute_start_;
    last_result_.VE_L_min = VE;
    last_result_.VO2_L_min = VO2;
    last_result_.VCO2_L_min = VCO2;
    last_result_.RQ = RQ;
    last_result_.kcal_per_min = kcal;

    minute_ready_ = true;

    // Reset untuk menit berikutnya
    seg_count_ = 0;
    sum_FeO2_ = 0.0;
    sum_FeCO2_ = 0.0;
    minute_VE_L_min_ = NAN;
    have_minute_volume_ = false;
}

CalorimetryResult IndirectCalorimetry::popResult()
{
    minute_ready_ = false;
    return last_result_;
}
