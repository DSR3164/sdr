#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include "pluto_lib.h"


namespace gui
{

    inline constexpr int NFFT = 2048;
    inline constexpr int NFFTW = NFFT / 2;
    inline constexpr int WF_H = 400;

    void compute_fftw(const int16_t *iq_interleaved, float *out_db);
    void compute_hz(sdr_config_t *cfg, std::vector<float> &x_hz);
    void change_modulation(sdr_config_t *cfg, std::vector<int16_t> &tx_buffer);
    void compute_wf_row_u8(const int16_t *iq_interleaved, uint8_t *outRow);

}