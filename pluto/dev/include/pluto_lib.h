#pragma once
#include <SoapySDR/Device.h>
#include <SoapySDR/Formats.h>
#include <algorithm>
#include <iostream>
#include <fstream>
#include <complex>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <random>
#include <vector>
#include <tuple>
#include <ctime>
#include <cstring>

using namespace std;
using cp = complex<double>;

typedef struct sdr_config_s
{
  char *name; // USB:V... or IP
  int buffer_size;
  double tx_sample_rate;
  double rx_sample_rate;

  double tx_carrier_freq;
  double rx_carrier_freq;

  float tx_gain;
  float rx_gain;

  size_t channels[1] = {0};
  SoapySDRDevice *sdr;
  SoapySDRStream *rxStream;
  SoapySDRStream *txStream;

  sdr_config_s(char *name, int buf, double tx_sr, double rx_sr,
               double tx_f, double rx_f, float tx_g, float rx_g)
      : name(name),
        buffer_size(buf),
        tx_sample_rate(tx_sr),
        rx_sample_rate(rx_sr),
        tx_carrier_freq(tx_f),
        rx_carrier_freq(rx_f),
        tx_gain(tx_g),
        rx_gain(rx_g),
        channels{0},
        sdr(nullptr),
        rxStream(nullptr),
        txStream(nullptr)
  {
  }
} sdr_config_t;

int init(sdr_config_t *config);
int deinit(sdr_config_t *config);
void rrc(double beta, int sps, int N, vector<double> &h);
void file_to_bits(const string &path, vector<int> &bits);
void bpsk_mapper(const vector<int> &bits, vector<cp> &symbols);
void qpsk_mapper(const vector<int> &bits, vector<cp> &symbols);
void bpsk_mapper_3gpp(const vector<int> &bits, vector<cp> &symbols);
void qpsk_mapper_3gpp(const vector<int> &bits, vector<cp> &symbols);
void qam16_mapper_3gpp(const vector<int> &bits, vector<cp> &symbols);
void upsample(const vector<cp> &symbols, vector<cp> &upsampled, int up = 10);
void filter_i(const vector<cp> &a, const vector<double> &b, vector<double> &y);
void filter_q(const vector<cp> &a, const vector<double> &b, vector<double> &y);
void filter_rrc(const vector<cp> &a, const vector<double> &b, vector<cp> &y);
void bpsk(const vector<int> &bits, vector<int16_t> &buffer, bool timestamp = false, int sps = 10);
void qpsk(const vector<int> &bits, vector<int16_t> &buffer, bool timestamp = false, int sps = 10);
void bpsk_3gpp(const vector<int> &bits, vector<int16_t> &buffer, bool timestamp, int sps = 10);
void qpsk_3gpp(const vector<int> &bits, vector<int16_t> &buffer, bool timestamp, int sps = 10);
void qam16_3gpp(const vector<int> &bits, vector<int16_t> &buffer, bool timestamp, int sps = 10);
void qam16_3gpp_rrc(const vector<int> &bits, vector<int16_t> &buffer, bool timestamp, int sps = 10);
void implement_barker(vector<int16_t> &symbols, int sps = 10);
void gen_bits(int N, vector<int> &bits);
int16_t *read_pcm(const char *filename, size_t *sample_count);