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

void mapper_q(const vector<int> &bits, vector<cp> &symbols);
void upsample(const vector<cp> &symbols, vector<cp> &upsampled, int up = 10);
void filter_i(const vector<cp> &a, const vector<double> &b, vector<int> &y);
void filter_q(const vector<cp> &a, const vector<double> &b, vector<int> &y);
void qpsk(vector<int> &bits, vector<int16_t> &buffer);
int16_t *read_pcm(const char *filename, size_t *sample_count);
tuple<SoapySDRDevice *, SoapySDRStream *, SoapySDRStream *, size_t, size_t> init(const char usb[], int sample_rate = 1e6, int carrier_freq = 807e6, bool usb_or_ip = 1);
void deinit(SoapySDRDevice *sdr, SoapySDRStream *rxStream, SoapySDRStream *txStream);