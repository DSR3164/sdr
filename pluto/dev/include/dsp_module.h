#pragma once

#include <pluto_lib.h>
#include <vector>
#include <complex>
#include <cmath>
#include <algorithm>
#include <cstddef>

struct GardnerResult
{
    std::vector<std::complex<float>> fixed;
    std::vector<int> offsets;
};

struct CostasQpskResult
{
    std::vector<std::complex<float>> out;
    std::vector<float> freq;
    std::vector<float> phase;
    std::vector<float> error;
};

GardnerResult gardner_timing_recovery(const std::vector<int16_t>& iq_interleaved, float BnTs, int SPS, float scale = 1.0f);
CostasQpskResult costas_loop_qpsk(const std::vector<std::complex<float>> &samples, float loop_bw);
std::vector<std::complex<float>> costas_loop(const std::vector<std::complex<float>> &samples, float Ki);
std::vector<std::complex<float>> convolve_ones(const std::vector<std::complex<float>> &x, int SPS);
std::vector<std::complex<float>> costas_loop_qam16(const std::vector<std::complex<float>> &samples, float Ki);
std::vector<std::complex<double>> rrc_mf(const std::vector<std::complex<double>> &samples, double beta, int sps, int span);
static inline std::complex<float> nearest_qam16(const std::complex<float> &r);
static inline float phase_detector_qpsk(const std::complex<float> &r);
