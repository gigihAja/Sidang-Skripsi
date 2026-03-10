#pragma once
#include <Arduino.h>
#include "indirect-calorimetry/indirect_calorimetry.h"

extern const char *GLOBAL_NOTE;

// Inisialisasi Serial2 untuk TJC/Nextion dan set teks awal default.
void tjc_setup();

// Baca RX dari TJC (event tombol, page change, dsb).
// Dipanggil berkala dari task Nextion.
void tjc_poll();

// Update tampilan TJC berdasarkan hasil indirect calorimetry.
// - Kalau hasResult == false  → tampilkan nilai default (0 kkal, 0 L/min, dll).
// - Kalau hasResult == true   → simpan dan gunakan nilai dari 'result'.
void tjc_update_from_ic(const CalorimetryResult *result, bool hasResult);

// Paksa kirim ulang state terakhir ke layar (misalnya dipanggil periodik dari task).
void tjc_apply_last_state();

void setText(const String &obj, const String &text);