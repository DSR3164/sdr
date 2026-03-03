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
}