#include "ads_reader.h"
#include <Wire.h>

ADSReader::ADSReader(uint8_t i2c_addr, adsGain_t gain, int sda, int scl)
    : addr_(i2c_addr), gain_(gain), sda_(sda), scl_(scl) {}

bool ADSReader::begin()
{
    Wire.begin(sda_, scl_);
    Wire.setClock(i2c_clock_hz_);
    delay(5);
    if (!ads_.begin(addr_))
        return false;
    ads_.setGain(gain_);
#ifdef RATE_ADS1115_128SPS
    ads_.setDataRate(RATE_ADS1115_128SPS);
#endif
    vdiff_lpf_ = countsToVolt(ads_.readADC_Differential_1_3());
    return true;
}

bool ADSReader::beginAuto()
{
    Wire.begin(sda_, scl_);
    Wire.setClock(i2c_clock_hz_);
    delay(5);
    const uint8_t cand[] = {0x48, 0x49, 0x4A, 0x4B};
    for (uint8_t a : cand)
    {
        if (ads_.begin(a))
        {
            addr_ = a;
            ads_.setGain(gain_);
#ifdef RATE_ADS1115_128SPS
            ads_.setDataRate(RATE_ADS1115_128SPS);
#endif
            vdiff_lpf_ = countsToVolt(ads_.readADC_Differential_1_3());
            Serial.print(F("[ADS] Found at 0x"));
            Serial.println(addr_, HEX);
            return true;
        }
    }
    return false;
}

void ADSReader::i2cScan()
{
    Wire.begin(sda_, scl_);
    Wire.setClock(i2c_clock_hz_);
    delay(5);
    Serial.println(F("I2C scan:"));
    int found = 0;
    for (uint8_t a = 8; a < 120; ++a)
    {
        Wire.beginTransmission(a);
        if (Wire.endTransmission(true) == 0)
        {
            Serial.print(F("  - 0x"));
            Serial.println(a, HEX);
            ++found;
        }
        delay(1);
    }
    if (!found)
        Serial.println(F("  (no devices)"));
}

float ADSReader::countsToVolt(int16_t c) const
{
    float FS = 0.256f; // GAIN_SIXTEEN
    switch (gain_)
    {
    case GAIN_TWOTHIRDS:
        FS = 6.144f;
        break;
    case GAIN_ONE:
        FS = 4.096f;
        break;
    case GAIN_TWO:
        FS = 2.048f;
        break;
    case GAIN_FOUR:
        FS = 1.024f;
        break;
    case GAIN_EIGHT:
        FS = 0.512f;
        break;
    case GAIN_SIXTEEN:
        FS = 0.256f;
        break;
    default:
        FS = 0.256f;
        break;
    }
    return c * (FS / 32768.0f);
}

float ADSReader::readDiffVoltFiltered()
{
    int16_t raw = ads_.readADC_Differential_1_3();
    float v = countsToVolt(raw);
    vdiff_lpf_ = vdiff_lpf_ + alpha_ * (v - vdiff_lpf_);
    return vdiff_lpf_;
}
