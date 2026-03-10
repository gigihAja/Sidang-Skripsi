#ifndef ADS_READER_H
#define ADS_READER_H

#include <Arduino.h>
#include <Adafruit_ADS1X15.h>

class ADSReader
{
public:
    // I2C pin default ESP32: SDA=21 SCL=22, addr default 0x48
    ADSReader(uint8_t i2c_addr = 0x48, adsGain_t gain = GAIN_SIXTEEN,
              int sda = 21, int scl = 22);

    bool begin();     // try current addr_
    bool beginAuto(); // scan 0x48..0x4B
    void i2cScan();   // debug scanner

    void setLPFAlpha(float a) { alpha_ = a; }
    void setClock(uint32_t hz) { i2c_clock_hz_ = hz; }

    // Differential read (A1-A3) with LPF, returns Volt
    float readDiffVoltFiltered();

private:
    float countsToVolt(int16_t counts) const;

    uint8_t addr_;
    adsGain_t gain_;
    int sda_, scl_;
    Adafruit_ADS1115 ads_;
    float alpha_ = 0.05f;
    float vdiff_lpf_ = 0.0f;
    uint32_t i2c_clock_hz_ = 100000;
};

#endif
