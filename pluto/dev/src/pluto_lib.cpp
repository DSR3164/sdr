#include "pluto_lib.h"

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

void upsample(const vector<cp> &symbols, vector<cp> &upsampled, int up)
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

void qpsk(vector<int> &bits, vector<int16_t> &buffer, bool timestamp)
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
            cout << "\nошибка в сигнале\n";
            break;
        }
    }

    size_t size = signal_i.size();
    for (size_t i = 0; i < buffer.size(); i += 2)
    {
        buffer[i] = i <= size ? ((signal_i[i / 2] * 1000) << 4) : 0;
        buffer[i + 1] = i <= size ? ((signal_q[i / 2] * 1000) << 4) : 0;
    }

    if (timestamp)
    {
        for (size_t i = 0; i < 2; i++) // Insert Timestamp
        {
            buffer[0 + i] = 0xffff;
            buffer[10 + i] = 0xffff;
        }
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

int init(sdr_config_t *config)
{
    char buffer_size[10]; // Allocate enough space
    snprintf(buffer_size, sizeof(buffer_size), "%d", config->buffer_size);
    SoapySDRKwargs args = {};
    SoapySDRKwargs_set(&args, "driver", "plutosdr");
    SoapySDRKwargs_set(&args, "uri", config->name);
    SoapySDRKwargs_set(&args, "direct", "1");
    SoapySDRKwargs_set(&args, "timestamp_every", buffer_size);
    SoapySDRKwargs_set(&args, "loopback", "0");
    config->sdr = SoapySDRDevice_make(&args);
    SoapySDRKwargs_clear(&args);
    SoapySDRDevice *sdr = config->sdr;

    if (!sdr)
    {
        printf("No device found!\n");
        return 1;
    }

    // RX parameters
    SoapySDRDevice_setSampleRate(sdr, SOAPY_SDR_RX, 0, config->rx_sample_rate);
    SoapySDRDevice_setFrequency(sdr, SOAPY_SDR_RX, 0, config->rx_carrier_freq, NULL);

    // TX parameters
    SoapySDRDevice_setSampleRate(sdr, SOAPY_SDR_TX, 0, config->tx_sample_rate);
    SoapySDRDevice_setFrequency(sdr, SOAPY_SDR_TX, 0, config->tx_carrier_freq, NULL);

    // Initialize channel count for RX\TX (in AdalmPluto it is one, zero)
    size_t channel = 0;

    // Configure the gain settings for the receiver and transmitter
    SoapySDRDevice_setGain(sdr, SOAPY_SDR_RX, channel, config->rx_gain); // RX sensitivity
    SoapySDRDevice_setGain(sdr, SOAPY_SDR_TX, channel, config->tx_gain); // TX power

    size_t numchun = 0;
    size_t channels[] = {0};
    // Forming streams for transmitting and receiving samples
    config->rxStream = SoapySDRDevice_setupStream(sdr, SOAPY_SDR_RX, SOAPY_SDR_CS16, channels, numchun, NULL);
    config->txStream = SoapySDRDevice_setupStream(sdr, SOAPY_SDR_TX, SOAPY_SDR_CS16, channels, numchun, NULL);
    SoapySDRStream *rxStream = config->rxStream;
    SoapySDRStream *txStream = config->txStream;

    SoapySDRDevice_activateStream(sdr, rxStream, 0, 0, 0); // start streaming
    SoapySDRDevice_activateStream(sdr, txStream, 0, 0, 0); // start streaming
    return 0;
}

int deinit(sdr_config_t *config)
{
    // stop streaming
    SoapySDRDevice_deactivateStream(config->sdr, config->rxStream, 0, 0);
    SoapySDRDevice_deactivateStream(config->sdr, config->txStream, 0, 0);

    // shutdown the stream
    SoapySDRDevice_closeStream(config->sdr, config->rxStream);
    SoapySDRDevice_closeStream(config->sdr, config->txStream);

    // cleanup device handle
    SoapySDRDevice_unmake(config->sdr);
    return 0;
}