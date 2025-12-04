#include <SoapySDR/Device.h>
#include <SoapySDR/Formats.h>
#include <iostream>
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
using ci = complex<int16_t>;

typedef struct sdr_config_s
{
  char *name; // USB:V... or IP
  int buffer_size;
  int tx_sample_rate;
  int rx_sample_rate;

  int tx_carrier_freq;
  int rx_carrier_freq;

  float tx_gain;
  float rx_gain;

  size_t channels[1] = {0};
  SoapySDRDevice *sdr;
  SoapySDRStream *rxStream;
  SoapySDRStream *txStream;

  sdr_config_s(char *name, int buf, int tx_sr, int rx_sr,
               int tx_f, int rx_f, float tx_g, float rx_g)
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
void mapper_q(const vector<int> &bits, vector<cp> &symbols);
void upsample(const vector<cp> &symbols, vector<cp> &upsampled, int up = 10);
void filter_i(const vector<cp> &a, const vector<double> &b, vector<int> &y);
void filter_q(const vector<cp> &a, const vector<double> &b, vector<int> &y);
void qpsk(vector<int> &bits, vector<int16_t> &buffer, bool timestamp = false);
int16_t *read_pcm(const char *filename, size_t *sample_count);