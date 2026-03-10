#pragma once

// Fungsi pembungkus untuk kode SCD30 (CO2)
// Isi logika 100% sama seperti kode aslinya (setup/loop dipindah ke sini)
void scd30_sensor_setup();
void scd30_sensor_loop();
bool scd30_get_last(float &co2_ppm, float &temp_c, float &rh_pct);