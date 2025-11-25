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

using namespace std;
using cp = complex<double>;
using ci = complex<int16_t>;

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
        symbols[i] = cp(bits[2 * i] * -2.0 + 1.0, bits[2 * i + 1] * -2.0 + 1.0);
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
    {
        upsampled[i * up] = symbols[i];
    }
}

void filter_i(const vector<cp> &a, const vector<double> &b, vector<int> &y)
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

void qpsk(vector<int> &bits, vector<int16_t> &buffer)
{
    const int up = 10;
    vector<cp> symbols(bits.size() / 2);
    vector<cp> upsampled(symbols.size() * up);
    vector<int> signal_i(symbols.size() * up);
    vector<int> signal_q(symbols.size() * up);
    vector<double> b(up, 1.0);

    mapper_q(bits, symbols);
    upsample(symbols, upsampled, up);
    filter_i(upsampled, b, signal_i);
    filter_q(upsampled, b, signal_q);
    for (size_t i = 0; i < signal_q.size(); ++i)
    {
        if (((signal_i[i] * signal_i[i]) != 1) || ((signal_q[i] * signal_q[i]) != 1))
        {
            cout << "\nошибка в сигнале\n"; break;
        }
    }

    size_t size = signal_i.size();
    for (size_t i = 0; i < buffer.size(); i += 2)
    {
        buffer[i] = i <= size ? ((signal_i[i / 2] * 1000) << 4) : 0;
        buffer[i + 1] = i <= size ? ((signal_q[i / 2] * 1000) << 4) : 0;
    }
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

tuple<SoapySDRDevice *, SoapySDRStream *, SoapySDRStream *, size_t, size_t> init(int sample_rate = 1e6, int carrier_freq = 807e6, bool usb_or_ip = 1)
{
    SoapySDRKwargs args = {};
    SoapySDRKwargs_set(&args, "driver", "plutosdr");
    if (usb_or_ip)
    {
        SoapySDRKwargs_set(&args, "uri", "usb:");
    }
    else
    {
        SoapySDRKwargs_set(&args, "uri", "ip:192.168.2.1");
    }
    SoapySDRKwargs_set(&args, "direct", "1");
    SoapySDRKwargs_set(&args, "timestamp_every", "1920");
    SoapySDRKwargs_set(&args, "loopback", "0");
    SoapySDRDevice *sdr = SoapySDRDevice_make(&args);
    SoapySDRKwargs_clear(&args);

    if (!sdr)
    {
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

int main(void)
{

    auto [sdr, rxStream, txStream, rx_mtu, tx_mtu] = init();
    if (!sdr)
    {
        printf("Initialization error\n");
        return -1;
    }

    int N = 1920;
    vector<int> bits(N);

    for(int i = 0; i < N; ++i)
    {
        bits[i] = rand() % 2;
    }
    vector<int16_t> qpsk_tx_buffer(1920*2);
    vector<int16_t> rx_buffer(1920*2);

    qpsk(bits, qpsk_tx_buffer);

    for(size_t i = 0; i < 2; i++)
    {
        qpsk_tx_buffer[0 + i] = 0xffff;
        // 8 x timestamp words
        qpsk_tx_buffer[10 + i] = 0xffff;
    }

    void *tx_buffs[] = {qpsk_tx_buffer.data()}; // Buffer for transmitting samples
    void *rx_buffs[] = {rx_buffer.data()}; // Buffer for transmitting samples

    FILE *file_tx = fopen("/home/plutoSDR/sdr/pluto/dev/tx1.pcm", "wb");
    FILE *file_rx = fopen("/home/plutoSDR/sdr/pluto/dev/rx1.pcm", "wb");

    int flags;
    long long timeNs;
    long long last_time = 0;
    long timeoutUs = 400000;
    flags = SOAPY_SDR_HAS_TIME;

    fwrite(tx_buffs[0], 2 * rx_mtu * sizeof(int16_t), 1, file_tx);

    for (size_t b = 0; b < 4; b++)
    {
        int sr = SoapySDRDevice_readStream(sdr, rxStream, rx_buffs, rx_mtu, &flags, &timeNs, timeoutUs);

        long long tx_time = timeNs + (4 * 1000 * 1000); // Schedule TX 4ms ahead
        if (b == 0)
        {
            int st = SoapySDRDevice_writeStream(sdr, txStream, (const void * const*)tx_buffs, tx_mtu, &flags, tx_time, timeoutUs);

            if (st < 0)
                printf("TX Failed on buffer %zu: %i\n", b, st);
            printf("Buffer: %lu - Samples: %i, Flags: %i, Time: %lli, TimeDiff: %lli\n", b, sr, flags, timeNs, (timeNs - last_time) * (last_time > 0));
        }
        fwrite(rx_buffs[0], 2 * rx_mtu * sizeof(int16_t), 1, file_rx);
        last_time = tx_time;
    }

    fclose(file_rx);
    fclose(file_tx);
    deinit(sdr, rxStream, txStream);
    return 0;
}
