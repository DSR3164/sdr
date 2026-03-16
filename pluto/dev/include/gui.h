#pragma once

#include <cstdint>
#include <vector>
#include "pluto_lib.h"
#include "dsp_module.h"

namespace gui
{
    inline constexpr int NFFT = 2048;
    inline constexpr int NFFTW = NFFT / 2;
    inline constexpr int WF_H = 400;

    void compute_fftw(const std::vector<std::complex<float>> &iq, std::vector<float> &out_db);
    void compute_hz(sdr_config_t &context, std::vector<float> &x_hz);
    void change_modulation(sdr_config_t &sdr_config, std::vector<int16_t> &tx_buffer, std::vector<int> &bits, SharedData_t &data, int ofdm_mod);
    void compute_wf_row_u8(const int16_t *iq_interleaved, uint8_t *outRow);
    static inline void db_to_u8(float db, uint8_t &r, uint8_t &g, uint8_t &b, float db_min = -120.0f, float db_max = -20.0f)
    {
        float t = (db - db_min) / (db_max - db_min);
        if (t < 0.0f)
            t = 0.0f;
        if (t > 1.0f)
            t = 1.0f;

        t = std::pow(t, 0.6f);
        if (t == 0.0f)
        {
            r = 0;
            g = 0;
            b = 0;
        }
        else if (t < 0.7f)
        {
            float u = t * 2.0f;
            r = (uint8_t)std::lround(255.0f * u);
            g = 0;
            b = (uint8_t)std::lround(255.0f * (1.0f - u));
        }
        else
        {
            float u = (t - 0.5f) * 2.0f;
            r = 255;
            g = (uint8_t)std::lround(255.0f * u);
            b = (uint8_t)std::lround(255.0f * u);
        }
    };
}