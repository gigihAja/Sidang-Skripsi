#ifndef CALCULATE_H
#define CALCULATE_H

class Calculate
{
private:
    // SI units
    float inletDiameter_m;
    float throatDiameter_m;
    float rho_kg_m3;
    float pressure1_Pa;
    float pressure2_Pa;

    float findDelta(float p1, float p2);

public:
    Calculate(float in_m, float out_m, float rho);

    float convertToPressure(long raw, long offset, float scale_Pa_per_count);

    // Forward (ΔP -> Q)
    float airflow_mLs(float p_inlet_Pa, float p_throat_Pa);

    // K (mL/s)/sqrt(Pa) for this geometry + Cd
    float k_mLs() const;

    // Inverse (Q -> ΔP), with same geometry + Cd
    float pressure_from_Q_mLs(float Q_mL_s) const;

    void showPressures();
};

#endif
