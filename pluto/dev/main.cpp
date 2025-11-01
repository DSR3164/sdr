#include <SoapySDR/Device.h>
#include <SoapySDR/Formats.h>
#include <stdio.h>            
#include <stdlib.h>           
#include <stdint.h>
#include <complex.h>
#include <cstring>
#include <string.h>
#include <random>
#include <iostream>

using namespace std;
using cp = complex<double>;
using ci = complex<int16_t>;

void mapper_b(const vector<int> &bits, vector<cp> &symbols)
{
    /*
    Map input bits to BPSK symbols and store them in 'symbols'.
    'bits' is the input vector of bits (0s and 1s).
    'symbols' is the output vector of complex symbols.
    0 -> +1 + 0j
    1 -> -1 + 0j
    */

    for (size_t i = 0; i < bits.size(); ++i)
        symbols[i] = cp(bits[i] * -2.0 + 1.0, 0.0);
}

void mapper_q(const vector<int> &bits, vector<cp> &symbols)
{
    /*
    Map input bits to QPSK symbols and store them in 'symbols'.
    'bits' is the input vector of bits (0s and 1s).
    'symbols' is the output vector of complex symbols.
    00 -> +1 + 1j
    01 -> +1 - 1j
    10 -> -1 + 1j
    11 -> -1 - 1j
    */

    for (size_t i = 0; i < symbols.size(); ++i)
        symbols[i] = cp(bits[2*i] * -2.0 + 1.0, bits[2*i + 1] * -2.0 + 1.0);
}

void upsample(const vector<cp> &symbols, vector<cp> &upsampled, int up = 10)
{
    /*
    Upsample the input symbols with zeros by a factor of 'up' and store the result in 'upsampled'.
    'symbols' is the input vector of complex symbols.
    'upsampled' is the output vector of complex samples after upsampling.
    'up' is the upsampling factor (default is 10).
    */

    if (upsampled.size() < symbols.size() * up)
    {
        printf("Ошибка: недостаточный размер вектора для апсемплинга!\n");
        return;
    }
    fill(upsampled.begin(), upsampled.end(), cp(0, 0));

    for (size_t i = 0; i < symbols.size(); ++i)
        upsampled[i * up] = symbols[i];
}

void filter(const vector<cp> &a, const vector<double> &b, vector<int> &y)
{
    /*
    Convolve input signal 'a' with filter coefficients 'b' and store the result in 'y'.
    'a' is a vector of complex samples.
    'b' is a vector of filter coefficients (real numbers), constant 1 in our case.
    'y' is the output vector of integers (filtered signal).
    */
    const int nb = b.size();
    const int na = a.size();

    y.assign(na, 0);

    for (int n = 0; n < na; ++n)
    {
        int acc = 0;
        for (int m = 0; m < nb; ++m)
        {
            if (n - m >= 0)
                acc += a[n - m].real() * b[m];
        }
        y[n] = acc;
    }
}

void filter_q(const vector<cp> &a, const vector<double> &b, vector<int> &y)
{
    /*
    Convolve input signal 'a' with filter coefficients 'b' and store the result in 'y'.
    'a' is a vector of complex samples.
    'b' is a vector of filter coefficients (real numbers), constant 1 in our case.
    'y' is the output vector of integers (filtered signal).
    */
    const int nb = b.size();
    const int na = a.size();

    y.assign(na, 0);

    for (int n = 0; n < na; ++n)
    {
        int acc = 0;
        for (int m = 0; m < nb; ++m)
        {
            if (n - m >= 0)
                acc += a[n - m].imag() * b[m];
        }
        y[n] = acc;
    }
}

tuple<const void **, void **> bpsk(vector<int> &bits)
{
    const int up = 10;
    vector<cp> symbols(bits.size());
    vector<cp> upsampled(bits.size() * up);
    vector<int> signal(bits.size() * up);
    vector<double> b(10, 1.0);

    mapper_b(bits, symbols);
    upsample(symbols, upsampled, up);
    filter(upsampled, b, signal);

    vector<int16_t> buffer(signal.size() * 2);
    vector<int16_t> rx_vec(1920 * 2);

    for (int i = 0; i < (int)buffer.size(); i += 2)
    {
        buffer[i] = signal[i / 2];
        buffer[i + 1] = 0;
    }

    static const void *tx_buffs[] = {buffer.data()}; // Buffer for transmitting samples
    static void *rx_buffs[] = {rx_vec.data()};       // Buffer for receiving samples

    return make_tuple(tx_buffs, rx_buffs);
}

tuple<const void **, void **> qpsk(vector<int> &bits)
{
    const int up = 10;
    vector<cp> symbols(bits.size()/2);
    vector<cp> upsampled(symbols.size() * up);
    vector<int> signal_i(symbols.size() * up);
    vector<int> signal_q(symbols.size() * up);
    vector<double> b(10, 1.0);

    mapper_q(bits, symbols);
    upsample(symbols, upsampled, up);
    filter(upsampled, b, signal_i);
    filter_q(upsampled, b, signal_q);

    vector<int16_t> buffer(signal_i.size() * 2);
    vector<int16_t> rx_vec(1920 * 2);

    for (int i = 0; i < (int)buffer.size(); i += 2)
    {
        buffer[i] = signal_i[i / 2];
        buffer[i + 1] = signal_q[i / 2];
    }

    static const void *tx_buffs[] = {buffer.data()}; // Buffer for transmitting samples
    static void *rx_buffs[] = {rx_vec.data()};       // Buffer for receiving samples

    return make_tuple(tx_buffs, rx_buffs);
}

int16_t *read_pcm(const char *filename, size_t *sample_count)
{
    FILE *file = fopen(filename, "rb");
    if (!file)
    {
        printf("Ошибка чтения файла\n");
        return NULL;
    }
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    printf("file_size = %ld\n", file_size);

    *sample_count = file_size / sizeof(int16_t);
    int16_t *samples = (int16_t *)malloc(file_size);
    size_t sf = fread(samples, sizeof(int16_t), *sample_count, file);

    if (sf == 0)
    {
        printf("file %s empty!", filename);
    }

    fclose(file);

    return samples;
}

tuple<SoapySDRDevice *, SoapySDRStream *, SoapySDRStream *, size_t, size_t> init(int sample_rate = 1e6, int carrier_freq = 800e6, bool usb_or_ip = 1)
{
    SoapySDRKwargs args = {};
    SoapySDRKwargs_set(&args, "driver", "plutosdr");
    if (usb_or_ip) {
        SoapySDRKwargs_set(&args, "uri", "usb:");
    } else {
        SoapySDRKwargs_set(&args, "uri", "ip:192.168.2.1");
    }
    SoapySDRKwargs_set(&args, "direct", "1");
    SoapySDRKwargs_set(&args, "timestamp_every", "1920");
    SoapySDRKwargs_set(&args, "loopback", "0");
    SoapySDRDevice *sdr = SoapySDRDevice_make(&args);
    SoapySDRKwargs_clear(&args);

    if (!sdr) {
        printf("No device found!\n");
        return make_tuple(nullptr, nullptr, nullptr, 0, 0);
    }

    // RX parameters
    SoapySDRDevice_setSampleRate(sdr, SOAPY_SDR_RX, 0, sample_rate);
    SoapySDRDevice_setFrequency(sdr, SOAPY_SDR_RX, 0, carrier_freq, NULL);

    // TX parameters
    SoapySDRDevice_setSampleRate(sdr, SOAPY_SDR_TX, 0, sample_rate);
    SoapySDRDevice_setFrequency(sdr, SOAPY_SDR_TX, 0, carrier_freq, NULL);

    // Initialize channel count for RX\TX (in AdalmPluto it is one, zero)
    size_t channel = 0;

    // Configure the gain settings for the receiver and transmitter
    SoapySDRDevice_setGain(sdr, SOAPY_SDR_RX, channel, 40.0); // RX sensitivity
    SoapySDRDevice_setGain(sdr, SOAPY_SDR_TX, channel, -7.0); // TX power

    size_t numchun = 0;
    size_t channels[] = {0};
    // Forming streams for transmitting and receiving samples
    SoapySDRStream *rxStream = SoapySDRDevice_setupStream(sdr, SOAPY_SDR_RX, SOAPY_SDR_CS16, channels, numchun, NULL);
    SoapySDRStream *txStream = SoapySDRDevice_setupStream(sdr, SOAPY_SDR_TX, SOAPY_SDR_CS16, channels, numchun, NULL);

    SoapySDRDevice_activateStream(sdr, rxStream, 0, 0, 0); // start streaming
    SoapySDRDevice_activateStream(sdr, txStream, 0, 0, 0); // start streaming

    // Get the MTU (Maximum Transmission Unit), in our case - the size of the buffers.
    size_t rx_mtu = SoapySDRDevice_getStreamMTU(sdr, rxStream);
    size_t tx_mtu = SoapySDRDevice_getStreamMTU(sdr, txStream);

    return make_tuple(sdr, rxStream, txStream, rx_mtu, tx_mtu);
}

void deinit(SoapySDRDevice *sdr, SoapySDRStream *rxStream, SoapySDRStream *txStream)
{
    // stop streaming
    SoapySDRDevice_deactivateStream(sdr, rxStream, 0, 0);
    SoapySDRDevice_deactivateStream(sdr, txStream, 0, 0);

    // shutdown the stream
    SoapySDRDevice_closeStream(sdr, rxStream);
    SoapySDRDevice_closeStream(sdr, txStream);

    // cleanup device handle
    SoapySDRDevice_unmake(sdr);
}

FILE *output_pcm()
{
    char repo_path[128];
    char fullpath[1024];
    char filename_out[64];

    if (getenv("USER") && strcmp(getenv("USER"), "excalibur") == 0)
        strcpy(repo_path, "/home/excalibur/code");
    else if (getenv("HOME"))
        strcpy(repo_path, getenv("HOME"));

    printf("Введите имя выходного файла: ");
    scanf("%63s", filename_out);
    snprintf(fullpath, sizeof(fullpath), "%s/sdr/pluto/dev/%s.pcm", repo_path, filename_out);
    printf("Output file: %s\n", fullpath);

    FILE *file = fopen(fullpath, "wb");
    return file;
}

int main(void)
{

    auto [sdr, rxStream, txStream, rx_mtu, tx_mtu] = init();
    if (!sdr)
    {
        printf("Initialization error\n");
        return -1;
    }

    vector<int> bits = {1, 0, 0, 1, 0, 1, 0, 0, 1, 0};
    auto [bpsk_tx_buffs, bpsk_rx_buffs] = bpsk(bits);
    auto [qpsk_tx_buffs, qpsk_rx_buffs] = qpsk(bits);

    FILE *output_file = output_pcm();

    int flags;
    long long timeNs;
    long long last_time = 0;
    long timeoutUs = 400000;
    flags = SOAPY_SDR_HAS_TIME;

    for (size_t b = 0; b < 10; b++)
    {
        int sr = SoapySDRDevice_readStream(sdr, rxStream, bpsk_rx_buffs, rx_mtu, &flags, &timeNs, timeoutUs);

        long long tx_time = timeNs + (4 * 1000 * 1000); // Schedule TX 4ms ahead
        if (b == 3)
        {
            int st = SoapySDRDevice_writeStream(sdr, txStream, bpsk_tx_buffs, tx_mtu, &flags, tx_time, timeoutUs);

            if (st < 0)
                printf("TX Failed on buffer %zu: %i\n", b, st);
            printf("Buffer: %lu - Samples: %i, Flags: %i, Time: %lli, TimeDiff: %lli\n", b, sr, flags, timeNs, (timeNs - last_time) * (last_time > 0));
        }
        fwrite(bpsk_rx_buffs[0], 2 * sr * sizeof(int16_t), 1, output_file);
        last_time = tx_time;
    }

    fclose(output_file);
    deinit(sdr, rxStream, txStream);
    return 0;
}
