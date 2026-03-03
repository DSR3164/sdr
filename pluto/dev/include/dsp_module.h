#pragma once

#include <pluto_lib.h>
#include <vector>
#include <complex>
#include <cmath>
#include <atomic>

std::vector<std::complex<float>> gardner(const std::vector<std::complex<float>> input, float BnTs, int SPS);
std::vector<std::complex<float>> costas_loop(const std::vector<std::complex<float>> &samples, float Ki);
std::vector<std::complex<float>> convolve_ones(const std::vector<std::complex<float>> &x, int SPS);
typedef struct SharedData
{
    std::vector<int16_t> tx[2];
    std::vector<int16_t> rx[2];
    std::atomic<int> write_index{ 0 };   // куда пишет SDR
    std::atomic<int> read_index{ 1 };    // откуда читает DSP
    std::atomic<bool> ready{ false };
    std::vector<std::complex<float>> receive_history;
    std::vector<float> signal_spectrum;

    struct Modulation
    {
        int ModulationType;
        int n; // N Subcarriers
        int ncp; // Len of CP
        int ps; // Spacing between pilots

        std::vector<std::complex<float>> raw;
        std::vector<std::complex<float>> conv;
        std::vector<std::complex<float>> sync;
        std::vector<std::complex<float>> demodul;

        std::vector<std::complex<float>> ofdm;

        bool cfo;

    } mod;

    SharedData(int samples_in_buffer, int n, int ncp, int ps, int mod_type)
    {
        rx[0].resize(samples_in_buffer * 2, 0);
        rx[1].resize(samples_in_buffer * 2, 0);
        tx[0].resize(samples_in_buffer * 2, 0);
        tx[1].resize(samples_in_buffer * 2, 0);
        receive_history.reserve(samples_in_buffer * 11);
        mod.raw.resize(samples_in_buffer);
        mod.conv.resize(samples_in_buffer);
        mod.sync.resize(samples_in_buffer);
        mod.demodul.resize(samples_in_buffer);
        mod.ofdm.resize(samples_in_buffer);
        mod.ModulationType = mod_type;
        mod.n = n;
        mod.ncp = ncp;
        mod.ps = ps;
        mod.cfo = 0;
    }

} SharedData_t;