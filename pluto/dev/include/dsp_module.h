#pragma once

#include <pluto_lib.h>
#include <vector>
#include <complex>
#include <cmath>

std::vector<std::complex<float>> gardner(const std::vector<std::complex<float>> input, float BnTs, int SPS);
std::vector<std::complex<float>> costas_loop(const std::vector<std::complex<float>> &samples, float Ki);
std::vector<std::complex<float>> convolve_ones(const std::vector<std::complex<float>> &x, int SPS);
