#pragma once

#include "dsp.h"

#include <vector>

inline constexpr int NFFT = 2048;
inline constexpr int NFFTW = NFFT / 2;
inline constexpr int WF_H = 400;

int context_edit_window(sdr_config_t &context, SharedData_t &data);
void compute_fftw(const std::vector<std::complex<float>> &iq, std::vector<float> &out_db);

void run_gui(sdr_config_t &context, SharedData_t &data);
