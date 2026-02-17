#include "pluto_lib.h"
#include <SoapySDR/Formats.hpp>
#include <SoapySDR/Device.hpp>
#include <chrono>
#include <thread>
#include <iostream>
#include <fstream>
#include <algorithm>

void bpsk_mapper_3gpp(const std::vector<int> &bits, std::vector<std::complex<double>> &symbols)
{
    for (size_t i = 0; i < symbols.size(); ++i)
        symbols[i] = std::complex<double>(bits[i] * -2.0 + 1.0, bits[i] * -2.0 + 1.0) / sqrt(2);
}

void qpsk_mapper_3gpp(const std::vector<int> &bits, std::vector<std::complex<double>> &symbols)
{
    for (size_t i = 0; i < symbols.size(); ++i)
        symbols[i] = std::complex<double>(bits[2 * i] * -2.0 + 1.0, bits[2 * i + 1] * -2.0 + 1.0) / sqrt(2.0);
}

void qam16_mapper_3gpp(const std::vector<int> &bits, std::vector<std::complex<double>> &symbols)
{
    for (size_t i = 0; i < symbols.size(); ++i)
        symbols[i] = std::complex<double>((1 - 2 * bits[4 * i + 0]) * (2 - (1 - 2 * bits[4 * i + 2])),
                                          (1 - 2 * bits[4 * i + 1]) * (2 - (1 - 2 * bits[4 * i + 3]))) /
                     sqrt(10.0);
}

void bpsk_mapper(const std::vector<int> &bits, std::vector<std::complex<double>> &symbols)
{
    /*
    Map input bits to QPSK symbols and store them in 'symbols'.
    'bits' is the input std::vector of bits (0s and 1s).
    'symbols' is the output std::vector of complex symbols.
    00 -> +1 + 1j
    01 -> +1 - 1j
    10 -> -1 + 1j
    11 -> -1 - 1j
    */

    for (size_t i = 0; i < symbols.size(); ++i)
        symbols[i] = std::complex<double>(bits[i] * -2.0 + 1.0, bits[i] * -2.0 + 1.0);
}

void qpsk_mapper(const std::vector<int> &bits, std::vector<std::complex<double>> &symbols)
{
    /*
    Map input bits to QPSK symbols and store them in 'symbols'.
    'bits' is the input std::vector of bits (0s and 1s).
    'symbols' is the output std::vector of complex symbols.
    00 -> +1 + 1j
    01 -> +1 - 1j
    10 -> -1 + 1j
    11 -> -1 - 1j
    */

    for (size_t i = 0; i < symbols.size(); ++i)
        symbols[i] = std::complex<double>(bits[2 * i] * -2.0 + 1.0, bits[2 * i + 1] * -2.0 + 1.0);
}

void upsample(const std::vector<std::complex<double>> &symbols, std::vector<std::complex<double>> &upsampled, int up)
{
    /*
    Upsample the input symbols with zeros by a factor of 'up' and store the result in 'upsampled'.
    'symbols' is the input std::vector of complex symbols.
    'upsampled' is the output std::vector of complex samples after upsampling.
    'up' is the upsampling factor (default is 10).
    */

    if (upsampled.size() < symbols.size() * up)
    {
        printf("Ошибка: недостаточный размер вектора для апсемплинга!\n");
        return;
    }
    fill(upsampled.begin(), upsampled.end(), std::complex<double>(0, 0));

    for (size_t i = 0; i < symbols.size(); ++i)
    {
        upsampled[i * up] = symbols[i];
    }
}

void filter_i(const std::vector<std::complex<double>> &a, const std::vector<double> &b, std::vector<double> &y)
{
    /*
    Convolve input signal 'a' with filter coefficients 'b' and store the result in 'y'.
    'a' is a std::vector of complex samples.
    'b' is a std::vector of filter coefficients (real numbers), constant 1 in our case.
    'y' is the output std::vector of integers (filtered signal).
    */
    const int nb = (int)b.size();
    const int na = (int)a.size();

    y.assign(na, 0);

    for (int n = 0; n < na; ++n)
    {
        double acc = 0;
        for (int m = 0; m < nb; ++m)
        {
            if (n - m >= 0)
                acc += a[n - m].real() * b[m];
        }
        y[n] = acc;
    }
}

void filter_q(const std::vector<std::complex<double>> &a, const std::vector<double> &b, std::vector<double> &y)
{
    /*
    Convolve input signal 'a' with filter coefficients 'b' and store the result in 'y'.
    'a' is a std::vector of complex samples.
    'b' is a std::vector of filter coefficients (real numbers), constant 1 in our case.
    'y' is the output std::vector of integers (filtered signal).
    */
    const int nb = (int)b.size();
    const int na = (int)a.size();

    y.assign(na, 0);

    for (int n = 0; n < na; ++n)
    {
        double acc = 0;
        for (int m = 0; m < nb; ++m)
        {
            if (n - m >= 0)
                acc += a[n - m].imag() * b[m];
        }
        y[n] = acc;
    }
}

void rrc(double beta, int sps, int N, std::vector<double> &h)
{
    int len = N * sps + 1;
    h.resize(len, 0.0);
    constexpr double eps = 1e-10;

    double T = 1.0;
    int mid = len / 2;

    for (int i = 0; i < len; ++i)
    {
        double t = (i - mid) / double(sps);
        if (t == 0.0)
            h[i] = 1.0 - beta + 4 * beta / M_PI;
        else if (std::abs(std::abs(t) - T / (4 * beta)) < eps)
            h[i] = (beta / sqrt(2)) * ((1 + 2 / M_PI) * sin(M_PI / (4 * beta)) +
                                       (1 - 2 / M_PI) * cos(M_PI / (4 * beta)));
        else
            h[i] = (sin(M_PI * t * (1 - beta) / T) + 4 * beta * t / T * cos(M_PI * t * (1 + beta) / T)) / (M_PI * t * (1 - (4 * beta * t / T) * (4 * beta * t / T)));
    }
}

void filter_rrc(const std::vector<std::complex<double>> &a, const std::vector<double> &b, std::vector<std::complex<double>> &y)
{
    int nb = (int)b.size();
    int na = (int)a.size();
    y.resize(na + nb - 1, {0.0f, 0.0f});
    for (int n = 0; n < na + nb - 1; ++n)
    {
        std::complex<double> acc{0.0f, 0.0f};
        for (int m = 0; m < nb; ++m)
        {
            int k = n - m;
            if (k >= 0 && k < na)
                acc += a[k] * b[m];
        }
        y[n] = acc;
    }
}

void bpsk(const std::vector<int> &bits, std::vector<int16_t> &buffer, bool timestamp, int sps)
{
    std::vector<std::complex<double>> symbols(bits.size());
    std::vector<std::complex<double>> upsampled(symbols.size() * sps);
    std::vector<double> signal_i(symbols.size() * sps);
    std::vector<double> signal_q(symbols.size() * sps);
    std::vector<double> b(sps, 1.0);

    bpsk_mapper(bits, symbols);
    upsample(symbols, upsampled, sps);
    filter_i(upsampled, b, signal_i);

    size_t size = signal_i.size();
    buffer.clear();
    buffer.resize(signal_i.size() * 2);
    for (size_t i = 0; i < size; ++i)
    {
        buffer[2 * i] = (signal_i[i] * 16000);
        buffer[2 * i + 1] = (signal_i[i] * 16000);
    }

    if (timestamp)
    {
        for (size_t i = 0; i < 2; i++) // Insert Timestamp
        {
            buffer[0 + i] = 32767;
            buffer[10 + i] = 32767;
        }
    }
}

void qpsk(const std::vector<int> &bits, std::vector<int16_t> &buffer, bool timestamp, int sps)
{
    std::vector<std::complex<double>> symbols(bits.size() / 2);
    std::vector<std::complex<double>> upsampled(symbols.size() * sps);
    std::vector<double> signal_i(symbols.size() * sps);
    std::vector<double> signal_q(symbols.size() * sps);
    std::vector<double> b(sps, 1.0);

    qpsk_mapper(bits, symbols);
    upsample(symbols, upsampled, sps);
    filter_i(upsampled, b, signal_i);
    filter_q(upsampled, b, signal_q);
    for (size_t i = 0; i < signal_q.size(); ++i)
    {
        if (((signal_i[i] * signal_i[i]) != 1) || ((signal_q[i] * signal_q[i]) != 1))
        {
            std::cout << "\nошибка в сигнале\n";
            break;
        }
    }

    size_t size = signal_i.size();
    buffer.clear();
    buffer.resize(signal_i.size() * 2);
    for (size_t i = 0; i < size; ++i)
    {
        buffer[2 * i] = (signal_i[i] * 16000);
        buffer[2 * i + 1] = (signal_q[i] * 16000);
    }

    if (timestamp)
    {
        for (size_t i = 0; i < 2; i++) // Insert Timestamp
        {
            buffer[0 + i] = 32767;
            buffer[10 + i] = 32767;
        }
    }
}

void bpsk_3gpp(const std::vector<int> &bits, std::vector<int16_t> &buffer, bool timestamp, int sps)
{
    std::vector<std::complex<double>> symbols(bits.size() / 2);
    std::vector<std::complex<double>> upsampled(symbols.size() * sps);
    std::vector<double> signal_i(symbols.size() * sps);
    std::vector<double> signal_q(symbols.size() * sps);
    std::vector<double> b(sps, 1.0);

    bpsk_mapper_3gpp(bits, symbols);
    upsample(symbols, upsampled, sps);
    filter_i(upsampled, b, signal_i);
    filter_q(upsampled, b, signal_q);

    size_t size = signal_i.size();
    buffer.clear();
    buffer.resize(signal_i.size() * 2);
    for (size_t i = 0; i < buffer.size(); i += 2)
    {
        buffer[i] = (i <= size) ? ((signal_i[i / 2] * 16000)) : 0;
        buffer[i + 1] = (i <= size) ? ((signal_q[i / 2] * 16000)) : 0;
    }

    if (timestamp)
    {
        for (size_t i = 0; i < 2; i++) // Insert Timestamp
        {
            buffer[0 + i] = 32767;
            buffer[10 + i] = 32767;
        }
    }
}

void qpsk_3gpp(const std::vector<int> &bits, std::vector<int16_t> &buffer, bool timestamp, int sps)
{
    std::vector<std::complex<double>> symbols(bits.size() / 2);
    std::vector<std::complex<double>> upsampled(symbols.size() * sps);
    std::vector<double> signal_i(symbols.size() * sps);
    std::vector<double> signal_q(symbols.size() * sps);
    std::vector<double> b(sps, 1.0);

    qpsk_mapper_3gpp(bits, symbols);
    upsample(symbols, upsampled, sps);
    filter_i(upsampled, b, signal_i);
    filter_q(upsampled, b, signal_q);

    size_t size = signal_i.size();
    buffer.clear();
    buffer.resize(signal_i.size() * 2);
    for (size_t i = 0; i < (symbols.size() * sps); i += 2)
    {
        buffer[i] = (int16_t)(signal_i[i / 2] * 16000);
        buffer[i + 1] = (int16_t)(signal_q[i / 2] * 16000);
    }

    // if (timestamp)
    // {
    //     for (size_t i = 0; i < 2; i++) // Insert Timestamp
    //     {
    //         buffer[0 + i] = 32767;
    //         buffer[10 + i] = 32767;
    //     }
    // }
}

void qam16_3gpp(const std::vector<int> &bits, std::vector<int16_t> &buffer, bool timestamp, int sps)
{
    std::vector<std::complex<double>> symbols(bits.size() / 4);
    std::vector<std::complex<double>> upsampled(symbols.size() * sps);
    std::vector<double> signal_i(symbols.size() * sps);
    std::vector<double> signal_q(symbols.size() * sps);
    std::vector<double> b(sps, 1.0);

    qam16_mapper_3gpp(bits, symbols);
    upsample(symbols, upsampled, sps);
    filter_i(upsampled, b, signal_i);
    filter_q(upsampled, b, signal_q);

    size_t size = signal_i.size();
    buffer.clear();
    buffer.resize(signal_i.size() * 2);
    for (size_t i = 0; i < buffer.size(); i += 2)
    {
        buffer[i] = (i <= size) ? ((signal_i[i / 2] * 16000)) : 0;
        buffer[i + 1] = (i <= size) ? ((signal_q[i / 2] * 16000)) : 0;
    }

    // if (timestamp)
    // {
    //     for (size_t i = 0; i < 2; i++) // Insert Timestamp
    //     {
    //         buffer[0 + i] = 32767;
    //         buffer[10 + i] = 32767;
    //     }
    // }
}

void qam16_3gpp_rrc(const std::vector<int> &bits, std::vector<int16_t> &buffer, bool timestamp, int sps)
{
    std::vector<std::complex<double>> symbols(bits.size() / 4);
    std::vector<std::complex<double>> upsampled(symbols.size() * sps);
    std::vector<std::complex<double>> signal(symbols.size() * sps);
    std::vector<double> rrc_h;
    std::vector<double> b(sps, 1.0);
    int span = 12;
    double beta = 0.25;

    qam16_mapper_3gpp(bits, symbols);
    upsample(symbols, upsampled, sps);
    rrc(beta, sps, span, rrc_h);
    filter_rrc(upsampled, rrc_h, signal);
    std::complex<double> maxcp = *std::max_element(signal.begin(), signal.end(), [](const std::complex<double> &a, const std::complex<double> &b)
                                                   { return std::real(a) < std::real(b); });
    double max = maxcp.real();

    size_t size = signal.size();
    buffer.clear();
    buffer.resize(signal.size() * 2);
    for (size_t i = 0; i < size; ++i)
    {
        buffer[2 * i] = static_cast<int16_t>((signal[i].real() / max) * 16000);
        buffer[2 * i + 1] = static_cast<int16_t>((signal[i].imag() / max) * 16000);
    }

    if (timestamp)
    {
        for (size_t i = 0; i < 2; i++) // Insert Timestamp
        {
            buffer[0 + i] = 32767;
            buffer[10 + i] = 32767;
        }
    }
}

void implement_barker(std::vector<int16_t> &symbols, int sps)
{
    std::cout << "Len of buff: " << symbols.size() << std::endl;
    std::cout << "Start implementing. . ." << std::endl;
    std::vector<int> barker = {0, 0, 0, 0, 0, 1, 1, 0, 0, 1, 0, 1, 0};
    std::vector<int16_t> out(barker.size() * sps * 2);
    bpsk(barker, out, false);
    std::cout << "Size of out: " << out.size() << std::endl;
    for (int i = 0; i < (int)out.size(); ++i)
        symbols[i + 12] = out[i];
}

void file_to_bits(const std::string &path, std::vector<int> &bits)
{
    std::fstream f(path, std::ios::binary);
    if (!f)
        throw std::runtime_error("open failed");

    char c;
    while (f.get(c))
    {
        uint8_t b = static_cast<uint8_t>(c);
        for (int i = 0; i < 8; ++i)
            bits.push_back((b >> i) & 1);
    }
}

void gen_bits(int N, std::vector<int> &bits)
{
    bits.clear();
    for (int i = 0; i < N; ++i)
        bits.push_back(rand() % 2);
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

int add_args(SoapySDR::Kwargs &args)
{
    args["direct"] = "1";
    args["timestamp_every"] = "1920";
    args["loopback"] = "0";
    return 0;
}

int init(sdr_config_t *config)
{
    add_args(config->args);
    config->sdr = SoapySDR::Device::make(config->args);
    SoapySDR::Device *sdr = config->sdr;

    if (!config->sdr)
    {
        std::cerr << "Failed to create SDR " << config->sdr_id << "\n";
        return 1;
    }

    // RX parameters
    sdr->setSampleRate(SOAPY_SDR_RX, 0, config->sample_rate);
    sdr->setFrequency(SOAPY_SDR_RX, 0, config->rx_carrier_freq);
    sdr->setGain(SOAPY_SDR_RX, 0, config->rx_gain);
    sdr->setGainMode(SOAPY_SDR_RX, 0, false);

    // TX parameters
    sdr->setSampleRate(SOAPY_SDR_TX, 0, config->sample_rate);
    sdr->setFrequency(SOAPY_SDR_TX, 0, config->tx_carrier_freq);
    sdr->setGain(SOAPY_SDR_TX, 0, config->tx_gain);
    sdr->setGainMode(SOAPY_SDR_TX, 0, false);

    // Stream parameters
    std::vector<size_t> channels = {0};
    config->rxStream = config->sdr->setupStream(SOAPY_SDR_RX, SOAPY_SDR_CS16, channels);
    config->txStream = config->sdr->setupStream(SOAPY_SDR_TX, SOAPY_SDR_CS16, channels);
    SoapySDR::Stream *rxStream = config->rxStream;
    SoapySDR::Stream *txStream = config->txStream;

    sdr->activateStream(rxStream, 0, 0, 0);
    sdr->activateStream(txStream, 0, 0, 0);
    std::cout << "\nCreate SDR:" << config->args["uri"] << "\n";

    return 0;
}

int deinit(sdr_config_t *config)
{
    if (!config)
        return 0;

    if (config->sdr)
    {
        if (config->rxStream)
        {
            config->sdr->deactivateStream(config->rxStream, 0, 0);
            config->sdr->closeStream(config->rxStream);
            config->rxStream = nullptr;
        }
        if (config->txStream)
        {
            config->sdr->deactivateStream(config->txStream, 0, 0);
            config->sdr->closeStream(config->txStream);
            config->txStream = nullptr;
        }
        std::cout << "\nDelete SDR:" << config->args["uri"] << "\n";
        SoapySDR::Device::unmake(config->sdr);
        config->sdr = nullptr;
    }
    return 0;
}

void reinit(sdr_config_t &context)
{
    deinit(&context);
    init(&context);
    context.flags &= ~Flags::REINIT;
}

void apply_runtime(sdr_config_t &context)
{
    if (!context.sdr)
        return;

    if ((context.flags & Flags::APPLY_FREQUENCY) != Flags::None)
    {

        context.sdr->setFrequency(SOAPY_SDR_TX, 0, context.tx_carrier_freq);
        context.sdr->setFrequency(SOAPY_SDR_RX, 0, context.rx_carrier_freq);
        context.flags &= ~Flags::APPLY_FREQUENCY;
    }

    if ((context.flags & Flags::APPLY_BANDWIDTH) != Flags::None)
    {
        context.sdr->setBandwidth(SOAPY_SDR_TX, 0, context.tx_bandwidth);
        context.sdr->setBandwidth(SOAPY_SDR_RX, 0, context.rx_bandwidth);
        context.flags &= ~Flags::APPLY_BANDWIDTH;
    }

    if ((context.flags & Flags::APPLY_GAIN) != Flags::None)
    {
        context.sdr->setGain(SOAPY_SDR_TX, 0, context.tx_gain);
        context.sdr->setGain(SOAPY_SDR_RX, 0, context.rx_gain);
        context.flags &= ~Flags::APPLY_GAIN;
    }

    if ((context.flags & Flags::APPLY_SAMPLE_RATE) != Flags::None)
    {
        context.sdr->setSampleRate(SOAPY_SDR_RX, 0, context.sample_rate);
        context.sdr->setSampleRate(SOAPY_SDR_TX, 0, context.sample_rate);
        context.flags &= ~Flags::APPLY_SAMPLE_RATE;
    }
}