#pragma once
#include <Arduino.h>

// Struktur hasil per menit
struct CalorimetryResult
{
    uint32_t t_start_ms; // waktu mulai perhitungan menit
    float VE_L_min;      // ventilasi total (L/min)
    float VO2_L_min;     // konsumsi O2 (L/min)
    float VCO2_L_min;    // produksi CO2 (L/min)
    float RQ;            // respiratory quotient (VCO2/VO2)
    float kcal_per_min;  // energi (kcal/min)
};

class IndirectCalorimetry
{
public:
    // Inisialisasi; FiO2 & FiCO2 adalah fraksi gas inspirasi (default udara ambien)
    void begin(float FiO2 = 0.2093f, float FiCO2 = 0.0004f);

    // ================== VOLUME (BARU) ==================
    // Set minute volume dari main (total volume L selama ~60 detik).
    // duration_s = durasi menit dalam detik (biasanya 60, tapi bisa 59–61).
    void setMinuteVolume(float minute_volume_L, float duration_s = 60.0f);

    // ================== FLOW (DEPRECATED) ==================
    // Di versi baru, IC tidak lagi menghitung VE dari flow.
    // Fungsi ini dibiarkan ada agar kode yang lama masih compile,
    // tapi isinya NO-OP (tidak melakukan apa-apa).
    void feedAirflow(float flow_mL_s);

    // Input nilai sensor gas (dipanggil hanya jika ada data baru)
    void setO2Percent(float o2_percent); // persen (%)
    void setCO2ppm(float co2_ppm);       // ppm

    // Update logika perhitungan window gas (dipanggil tiap loop)
    void update(uint32_t now_ms);

    // Jika sudah terkumpul 1 menit data + minute volume dikirim, hasil siap dibaca
    bool minuteReady() const { return minute_ready_; }

    // Ambil hasil terakhir (hanya sekali per menit)
    CalorimetryResult popResult();

private:
    // Parameter waktu
    static constexpr uint32_t WINDOW_MS = 20000; // 20 detik
    static constexpr uint32_t MINUTE_MS = 60000; // 60 detik (3 window)

    // Fraksi inspirasi (tetap)
    float FiO2_{0.2093f};
    float FiCO2_{0.0004f};

    // Fraksi ekspirasi (terakhir dibaca dari sensor)
    float FeO2_{NAN};
    float FeCO2_{NAN};

    // Akumulator window 20 detik (untuk gas)
    uint32_t win_start_{0};
    // double flow_sum_mL_s_{0.0}; // DEPRECATED
    // uint32_t flow_count_{0};    // DEPRECATED
    float snap_O2_frac_{NAN};
    float snap_CO2_frac_{NAN};

    // Akumulator 1 menit (3×20 detik) untuk gas
    uint32_t minute_start_{0};
    int seg_count_{0};
    double sum_FeO2_{0.0};
    double sum_FeCO2_{0.0};

    // Minute volume (dari main)
    float minute_VE_L_min_{NAN};
    bool have_minute_volume_{false};

    // Hasil terakhir
    bool minute_ready_{false};
    CalorimetryResult last_result_{};

    // Fungsi internal
    void closeWindow20s(uint32_t now_ms);
    void finalizeMinuteIfReady();
};
