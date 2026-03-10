#include "calculate.h"
#include <Arduino.h>
#include <cmath>

namespace
{
    constexpr float PI_F = 3.1415926535f;
    constexpr float C_d = 0.98f; // discharge coefficient
}

Calculate::Calculate(float in_m, float out_m, float rho)
    : inletDiameter_m(in_m), throatDiameter_m(out_m), rho_kg_m3(rho),
      pressure1_Pa(0), pressure2_Pa(0) {}

float Calculate::convertToPressure(long raw, long offset, float scale_Pa_per_count)
{
    return (raw - offset) * scale_Pa_per_count;
}

float Calculate::findDelta(float p1, float p2)
{
    float d = p1 - p2;
    return (d > 0.0f) ? d : 0.0f;
}

void Calculate::showPressures()
{
    Serial.print(pressure1_Pa);
    Serial.println(F(" Pa (inlet)"));
    Serial.print(pressure2_Pa);
    Serial.println(F(" Pa (throat)"));
}

// ΔP -> Q (mL/s)
float Calculate::airflow_mLs(float p_inlet_Pa, float p_throat_Pa)
{
    pressure1_Pa = p_inlet_Pa;
    pressure2_Pa = p_throat_Pa;

    const float dP = findDelta(pressure1_Pa, pressure2_Pa);
    if (dP <= 0)
        return 0.0f;

    const float A2 = (PI_F * throatDiameter_m * throatDiameter_m) * 0.25f; // m^2
    const float beta = throatDiameter_m / inletDiameter_m;
    const float one_minus_beta4 = 1.0f - std::pow(beta, 4);
    if (A2 <= 0.0f || beta <= 0.0f || beta >= 1.0f || one_minus_beta4 <= 0.0f)
        return 0.0f;

    // Venturi (real): Q = Cd * A2 * sqrt((2*dP)/(rho*(1 - beta^4)))
    const float Q_m3s = C_d * A2 * std::sqrt((2.0f * dP) / (rho_kg_m3 * one_minus_beta4));
    return Q_m3s * 1e6f; // mL/s
}

float Calculate::k_mLs() const
{
    const float A2 = (PI_F * throatDiameter_m * throatDiameter_m) * 0.25f; // m^2
    const float beta = throatDiameter_m / inletDiameter_m;
    const float one_minus_beta4 = 1.0f - std::pow(beta, 4);
    if (A2 <= 0.0f || beta <= 0.0f || beta >= 1.0f || one_minus_beta4 <= 0.0f)
        return 0.0f;

    const float K_m3s = C_d * A2 * std::sqrt(2.0f / (rho_kg_m3 * one_minus_beta4));
    return K_m3s * 1e6f; // (mL/s)/sqrt(Pa)
}

float Calculate::pressure_from_Q_mLs(float Q_mL_s) const
{
    const float K = k_mLs(); // (mL/s)/sqrt(Pa)
    if (K <= 0.0f)
        return 0.0f;
    const float ratio = Q_mL_s / K;
    return ratio * ratio; // Pa
}
