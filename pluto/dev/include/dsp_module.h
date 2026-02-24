#pragma once

#include <pluto_lib.h>
#include <vector>
#include <complex>
#include <cmath>

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
std::vector<std::complex<float>> gardner(const std::vector<std::complex<float>> input, float BnTs, int SPS);
std::vector<std::complex<float>> costas_loop(const std::vector<std::complex<float>> &samples, float Ki);
std::vector<std::complex<float>> convolve_ones(const std::vector<std::complex<float>> &x, int SPS);
