#include "pluto_lib.h"
#include "fftw3.h"
#include <SoapySDR/Constants.h>
#include <SoapySDR/Formats.hpp>
#include <SoapySDR/Device.hpp>
#include <iostream>
#include <fstream>
#include <algorithm>

void bpsk_mapper_3gpp(const std::vector<int> &bits, std::vector<std::complex<double>> &symbols)
{
    for (size_t i = 0; i < symbols.size(); ++i)
        symbols[i] = std::complex<double>(
            bits[i] * -2.0 + 1.0,
            bits[i] * -2.0 + 1.0)
        / sqrt(2);
}

void qpsk_mapper_3gpp(const std::vector<int> &bits, std::vector<std::complex<double>> &symbols)
{
    for (size_t i = 0; i < symbols.size(); ++i)
        symbols[i] = std::complex<double>(
            bits[2 * i + 0] * -2.0 + 1.0,
            bits[2 * i + 1] * -2.0 + 1.0)
        / sqrt(2.0);
}

void qam16_mapper_3gpp(const std::vector<int> &bits, std::vector<std::complex<double>> &symbols)
{
    for (size_t i = 0; i < symbols.size(); ++i)
        symbols[i] = std::complex<double>(
            (1 - 2 * bits[4 * i + 0]) * (2 - (1 - 2 * bits[4 * i + 2])),
            (1 - 2 * bits[4 * i + 1]) * (2 - (1 - 2 * bits[4 * i + 3])))
        / sqrt(10.0);
}

void qam128_mapper_3gpp(const std::vector<int> &bits, std::vector<std::complex<double>> &symbols)
{
    for (size_t i = 0; i < symbols.size(); ++i)
        symbols[i] = std::complex<double>(
            (1 - 2 * bits[7 * i + 0]) * (4 - (1 - 2 * bits[7 * i + 2]) * (2 - (1 - 2 * bits[7 * i + 4]))),
            (1 - 2 * bits[7 * i + 1]) * (4 - (1 - 2 * bits[7 * i + 3]) * (2 - (1 - 2 * bits[7 * i + 5]))))
        / sqrt(42.0);
}

void upsample(const std::vector<std::complex<double>> &symbols, std::vector<std::complex<double>> &upsampled, int up)
{
    if (upsampled.size() < symbols.size() * up)
    {
        std::cout << "Wrong upsampled vector size!\n";
        return;
    }
    fill(upsampled.begin(), upsampled.end(), std::complex<double>(0, 0));

    for (size_t i = 0; i < symbols.size(); ++i)
        upsampled[i * up] = symbols[i];
}

void filter_complex(const std::vector<std::complex<double>> &a, const std::vector<double> &b, std::vector<std::complex<double>> &y)
{
    size_t nb = b.size();
    size_t na = a.size();

    y.assign(na, std::complex<double>(0, 0));

    for (size_t n = 0; n < na; ++n)
    {
        std::complex<double> acc(0, 0);
        for (size_t m = 0; m < nb; ++m)
        {
            if (n - m >= 0)
                acc += a[n - m] * b[m];
        }
        y[n] = acc;
    }
}

void rrc(double beta, int sps, int N, std::vector<double> &h)
{
    int len = N * sps + 1;
    h.resize(static_cast<size_t>(len), 0.0);
    constexpr double eps = 1e-10;

    double T = 1.0;
    int mid = len / 2;

    for (int i = 0; i < len; ++i)
    {
        double t = (i - mid) / double(sps);
        if (t == 0.0)
            h[i] = 1.0 - beta + 4 * beta / M_PIf;
        else if (std::abs(std::abs(t) - T / (4 * beta)) < eps)
            h[i] = (beta / sqrt(2)) * ((1 + 2 / M_PIf) * sin(M_PIf / (4 * beta)) +
                (1 - 2 / M_PIf) * cos(M_PIf / (4 * beta)));
        else
            h[i] = (sin(M_PIf * t * (1 - beta) / T) + 4 * beta * t / T * cos(M_PIf * t * (1 + beta) / T)) / (M_PIf * t * (1 - (4 * beta * t / T) * (4 * beta * t / T)));
    }
}

void filter_rrc(const std::vector<std::complex<double>> &a, const std::vector<double> &b, std::vector<std::complex<double>> &y)
{
    size_t nb = b.size();
    size_t na = a.size();
    y.resize(na + nb - 1, { 0.0f, 0.0f });
    for (size_t n = 0; n < na + nb - 1; ++n)
    {
        std::complex<double> acc{ 0.0f, 0.0f };
        for (size_t m = 0; m < nb; ++m)
        {
            size_t k = n - m;
            if (k >= 0 && k < na)
                acc += a[k] * b[m];
        }
        y[n] = acc;
    }
}

void bpsk_3gpp(const std::vector<int> &bits, std::vector<int16_t> &buffer, int sps)
{
    std::vector<std::complex<double>> symbols(bits.size() / 2);
    std::vector<std::complex<double>> upsampled(symbols.size() * sps);
    std::vector<std::complex<double>> signal(symbols.size() * sps);
    std::vector<double> signal_q(symbols.size() * sps);
    std::vector<double> b(sps, 1.0);

    bpsk_mapper_3gpp(bits, symbols);
    upsample(symbols, upsampled, sps);
    filter_complex(upsampled, b, signal);

    size_t size = signal.size();
    buffer.clear();
    buffer.resize(signal.size() * 2);
    for (size_t i = 0; i < size; i += 2)
    {
        buffer[i] = static_cast<int16_t>(signal[i].real() * 16000.0f);
        buffer[i + 1] = static_cast<int16_t>(signal[i].imag() * 16000.0f);
    }
}

void qpsk_3gpp(const std::vector<int> &bits, std::vector<int16_t> &buffer, int sps)
{
    std::vector<std::complex<double>> symbols(bits.size() / 2);
    std::vector<std::complex<double>> upsampled(symbols.size() * sps);
    std::vector<std::complex<double>> signal(symbols.size() * sps);
    std::vector<double> signal_q(symbols.size() * sps);
    std::vector<double> b(sps, 1.0);

    qpsk_mapper_3gpp(bits, symbols);
    upsample(symbols, upsampled, sps);
    filter_complex(upsampled, b, signal);

    size_t size = signal.size();
    buffer.clear();
    buffer.resize(signal.size() * 2);
    for (size_t i = 0; i < size; i += 2)
    {
        buffer[i] = static_cast<int16_t>(signal[i].real() * 16000.0f);
        buffer[i + 1] = static_cast<int16_t>(signal[i].imag() * 16000.0f);
    }
}

void qam16_3gpp(const std::vector<int> &bits, std::vector<int16_t> &buffer, int sps)
{
    std::vector<std::complex<double>> symbols(bits.size() / 4);
    std::vector<std::complex<double>> upsampled(symbols.size() * sps);
    std::vector<std::complex<double>> signal(symbols.size() * sps);
    std::vector<double> signal_q(symbols.size() * sps);
    std::vector<double> b(sps, 1.0);

    qam16_mapper_3gpp(bits, symbols);
    upsample(symbols, upsampled, sps);
    filter_complex(upsampled, b, signal);

    size_t size = signal.size();
    buffer.clear();
    buffer.resize(signal.size() * 2);
    for (size_t i = 0; i < size; i += 2)
    {
        buffer[i] = static_cast<int16_t>(signal[i].real() * 16000.0f);
        buffer[i + 1] = static_cast<int16_t>(signal[i].imag() * 16000.0f);
    }
}

void qam16_3gpp_rrc(const std::vector<int> &bits, std::vector<int16_t> &buffer, int sps)
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
        {
            return std::real(a) < std::real(b);
        });
    double max = maxcp.real();

    size_t size = signal.size();
    buffer.clear();
    buffer.resize(signal.size() * 2);
    for (size_t i = 0; i < size; ++i)
    {
        buffer[2 * i] = static_cast<int16_t>((signal[i].real() / max) * 16000);
        buffer[2 * i + 1] = static_cast<int16_t>((signal[i].imag() / max) * 16000);
    }
}

std::vector<std::complex<float>> generate_zc(int L, int q)
{
    std::vector<std::complex<float>> zc(L);

    for (int n = 0; n < L; ++n)
    {
        float phase = -M_PIf * q * n * (n + 1) / L;
        zc[n] = std::exp(std::complex<float>(0, phase));
    }

    return zc;
}

void ofdm(const std::vector<int> &bits, std::vector<int16_t> &buffer, int N, int Ncp, int pilot_spacing, int modulation_type)
{
    if (N < 4 or pilot_spacing < 2)
        return;

    buffer.clear();
    std::vector<std::complex<double>> symbols(bits.size() / 1);
    std::vector<std::complex<float>> schmidl(N);
    auto zc = generate_zc(127, 5);
    switch (modulation_type)
    {
    case 0:
        bpsk_mapper_3gpp(bits, symbols);
        break;
    case 1:
        symbols.resize(bits.size() / 2);
        qpsk_mapper_3gpp(bits, symbols);
        break;
    case 2:
        symbols.resize(bits.size() / 4);
        qam16_mapper_3gpp(bits, symbols);
        break;
    case 3:
        symbols.resize(bits.size() / 7);
        qam128_mapper_3gpp(bits, symbols);
        break;
    default:
        symbols.resize(bits.size() / 4);
        qam16_mapper_3gpp(bits, symbols);
        break;
    }

    FFTWPlan ifft(N, false);

    int total_qpsk = (int)symbols.size();
    std::vector<int> data_positions;
    std::vector<int> pilot_positions;

    int counter = 0;

    for (int k = 1; k < N; ++k)
    {
        if (k > N / 2 - 28 and k < N / 2 + 27)
            continue;

        if (counter % pilot_spacing == 0)
        {
            pilot_positions.push_back(k);
        }
        else
            data_positions.push_back(k);

        counter++;
    }

    int symbols_per_ofdm = static_cast<int>(data_positions.size());
    int num_ofdm_symbols = total_qpsk / symbols_per_ofdm;

    buffer.reserve((num_ofdm_symbols + Ncp) * (N + 2));

    ifft.in[0][0] = 0;
    ifft.in[0][1] = 0;

    for (size_t i = 1; i <= 63; ++i)
    {
        ifft.in[i][0] = zc[i - 1].real();
        ifft.in[i][1] = zc[i - 1].imag();
    }

    for (size_t i = 64; i <= 127; ++i)
    {
        ifft.in[i][0] = zc[i - 1].real();
        ifft.in[i][1] = zc[i - 1].imag();
    }

    fftwf_execute(ifft.plan);

    //Norm
    for (int n = 0; n < N; ++n)
    {
        ifft.out[n][0] /= (float)(N / (3.0 * 16000.0));
        ifft.out[n][1] /= (float)(N / (3.0 * 16000.0));
    }

    //Cyclic Prefix
    for (int n = N - Ncp; n < N; ++n)
    {
        buffer.push_back((int16_t)ifft.out[n][0]);
        buffer.push_back((int16_t)ifft.out[n][1]);
    }

    //Data
    for (int n = 0; n < N; ++n)
    {
        buffer.push_back((int16_t)ifft.out[n][0]);
        buffer.push_back((int16_t)ifft.out[n][1]);
    }

    for (int sym = 0; sym < num_ofdm_symbols; ++sym)
    {
        for (int i = 0; i < N; ++i)
        {
            ifft.in[i][0] = 0.0f;
            ifft.in[i][1] = 0.0f;
        }

        for (int k : pilot_positions)
        {
            ifft.in[k][0] = 1.0f;
            ifft.in[k][1] = 0.0f;
        }

        for (int i = 0; i < data_positions.size(); ++i)
        {
            int idx = sym * symbols_per_ofdm + i;
            int k = data_positions[i];

            ifft.in[k][0] = (float)std::real(symbols[idx]);
            ifft.in[k][1] = (float)std::imag(symbols[idx]);
        }

        fftwf_execute(ifft.plan);

        //Norm
        for (int n = 0; n < N; ++n)
        {
            ifft.out[n][0] /= (float)(N / (3.0 * 16000.0f));
            ifft.out[n][1] /= (float)(N / (3.0 * 16000.0f));
        }

        //Cyclic Prefix
        for (int n = N - Ncp; n < N; ++n)
        {
            buffer.push_back((int16_t)ifft.out[n][0]);
            buffer.push_back((int16_t)ifft.out[n][1]);
        }

        //Data
        for (int n = 0; n < N; ++n)
        {
            buffer.push_back((int16_t)ifft.out[n][0]);
            buffer.push_back((int16_t)ifft.out[n][1]);
        }
    }
}

void implement_barker(std::vector<int16_t> &symbols, int sps)
{
    std::vector<int> barker = { 0, 0, 0, 0, 0, 1, 1, 0, 0, 1, 0, 1, 0 };
    std::vector<int16_t> out(barker.size() * sps * 2);
    bpsk_3gpp(barker, out, 10);
    symbols.insert(symbols.begin(), out.begin(), out.end());
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
    bits.resize(0);
    for (int i = 0; i < N; ++i)
        bits.push_back(rand() % 2);
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
    if (config->rx_stream and config->tx_stream)
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
    sdr->setBandwidth(SOAPY_SDR_RX, 0, config->rx_bandwidth);

    // TX parameters
    sdr->setSampleRate(SOAPY_SDR_TX, 0, config->sample_rate);
    sdr->setFrequency(SOAPY_SDR_TX, 0, config->tx_carrier_freq);
    sdr->setGain(SOAPY_SDR_TX, 0, config->tx_gain);
    sdr->setGainMode(SOAPY_SDR_TX, 0, false);
    sdr->setBandwidth(SOAPY_SDR_TX, 0, config->tx_bandwidth);

    // Stream parameters
    std::vector<size_t> channels = { 0 };
    if (config->rx_stream)
    {
        config->rxStream = config->sdr->setupStream(SOAPY_SDR_RX, SOAPY_SDR_CS16, channels);
        SoapySDR::Stream *rxStream = config->rxStream;
        sdr->activateStream(rxStream, 0, 0, 0);
        std::cout << "\nActivate RX Stream" << "\n";
    }
    if (config->tx_stream)
    {
        config->txStream = config->sdr->setupStream(SOAPY_SDR_TX, SOAPY_SDR_CS16, channels);
        SoapySDR::Stream *txStream = config->txStream;
        sdr->activateStream(txStream, 0, 0, 0);
        std::cout << "\nActivate TX Stream" << "\n";
    }
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

std::vector<std::complex<float>> generate_minn_preamble(size_t N)
{
    std::vector<std::complex<float>> freq(N, { 0,0 });

    for (size_t k = 1; k < N; k += 4)
        freq[k] = std::complex<float>{ 1.0, 0 }; // BPSK

    return freq;
}