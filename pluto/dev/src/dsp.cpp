#include "dsp.h"
#include "gui.h"

#include <SoapySDR/Formats.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstddef>
#include <fstream>
#include <thread>
#include <vector>

void bpsk_mapper_3gpp(const std::vector<int> &bits, std::vector<std::complex<double>> &symbols)
{
    for (size_t i = 0; i < symbols.size(); ++i)
        symbols[i] = std::complex<double>(
                         bits[i] * -2.0 + 1.0,
                         bits[i] * -2.0 + 1.0
                     )
                     / sqrt(2);
}

void qpsk_mapper_3gpp(const std::vector<int> &bits, std::vector<std::complex<double>> &symbols)
{
    for (size_t i = 0; i < symbols.size(); ++i)
        symbols[i] = std::complex<double>(
                         bits[2 * i + 0] * -2.0 + 1.0,
                         bits[2 * i + 1] * -2.0 + 1.0
                     )
                     / sqrt(2.0);
}

void qam16_mapper_3gpp(const std::vector<int> &bits, std::vector<std::complex<double>> &symbols)
{
    for (size_t i = 0; i < symbols.size(); ++i)
        symbols[i] = std::complex<double>(
                         (1 - 2 * bits[4 * i + 0]) * (2 - (1 - 2 * bits[4 * i + 2])),
                         (1 - 2 * bits[4 * i + 1]) * (2 - (1 - 2 * bits[4 * i + 3]))
                     )
                     / sqrt(10.0);
}

void qam64_mapper_3gpp(const std::vector<int> &bits, std::vector<std::complex<double>> &symbols)
{
    for (size_t i = 0; i < symbols.size(); ++i)
        symbols[i] = std::complex<double>(
                         (1 - 2 * bits[6 * i + 0]) * (4 - (1 - 2 * bits[6 * i + 2]) * (2 - (1 - 2 * bits[6 * i + 4]))),
                         (1 - 2 * bits[6 * i + 1]) * (4 - (1 - 2 * bits[6 * i + 3]) * (2 - (1 - 2 * bits[6 * i + 5])))
                     )
                     / sqrt(42.0);
}

void qam256_mapper_3gpp(const std::vector<int> &bits, std::vector<std::complex<double>> &symbols)
{
    for (size_t i = 0; i < symbols.size(); ++i)
    {
        symbols[i] = std::complex<double>(
                         (1 - 2 * bits[8 * i + 0]) * (8 - (1 - 2 * bits[8 * i + 2]) * (4 - (1 - 2 * bits[8 * i + 4]) * (2 - (1 - 2 * bits[8 * i + 6])))),
                         (1 - 2 * bits[8 * i + 1]) * (8 - (1 - 2 * bits[8 * i + 3]) * (4 - (1 - 2 * bits[8 * i + 5]) * (2 - (1 - 2 * bits[8 * i + 7]))))
                     )
                     / sqrt(170.0f);
    }
}

static std::pair<uint8_t, uint8_t> demap_component_3gpp(float val)
{
    uint8_t b_sign = (val < 0.0f) ? 1 : 0;
    uint8_t b_amp = (std::abs(val) < 2.0f) ? 0 : 1;
    return { b_sign, b_amp };
}

void bpsk_demapper_3gpp(const std::vector<std::complex<float>> &symbols, std::vector<int> &bits)
{
    bits.resize(symbols.size());

    for (size_t i = 0; i < symbols.size(); ++i)
        bits[i] = demap_component_3gpp(symbols[i].real()).first;
}

void qpsk_demapper_3gpp(const std::vector<std::complex<float>> &symbols, std::vector<int> &bits)
{
    bits.resize(symbols.size() * 2);

    for (size_t i = 0; i < symbols.size(); ++i)
    {
        bits[2 * i + 0] = demap_component_3gpp(symbols[i].real()).first;
        bits[2 * i + 1] = demap_component_3gpp(symbols[i].imag()).first;
    }
}

void qam16_demapper_3gpp(const std::vector<std::complex<float>> &symbols, std::vector<int> &bits)
{
    bits.resize(symbols.size() * 4);
    const float scale = std::sqrt(10.0f);

    for (size_t i = 0; i < symbols.size(); ++i)
    {
        auto [b0, b2] = demap_component_3gpp(symbols[i].real() * scale);
        auto [b1, b3] = demap_component_3gpp(symbols[i].imag() * scale);

        bits[4 * i + 0] = b0;
        bits[4 * i + 1] = b1;
        bits[4 * i + 2] = b2;
        bits[4 * i + 3] = b3;
    }
}

void qam64_demapper_3gpp(const std::vector<std::complex<float>> &symbols, std::vector<int> &bits)
{
    bits.resize(symbols.size() * 6);
    const float scale = sqrt(42.0f);

    for (size_t i = 0; i < symbols.size(); ++i)
    {
        float I = symbols[i].real() * scale;
        float Q = symbols[i].imag() * scale;

        bits[6 * i + 0] = (I < 0) ? 1 : 0;
        bits[6 * i + 1] = (Q < 0) ? 1 : 0;

        bits[6 * i + 2] = (std::abs(I) < 4.0f) ? 0 : 1;
        bits[6 * i + 3] = (std::abs(Q) < 4.0f) ? 0 : 1;

        float absI2 = std::abs(std::abs(I) - 4.0f);
        float absQ2 = std::abs(std::abs(Q) - 4.0f);
        bits[6 * i + 4] = (absI2 < 2.0f) ? 0 : 1;
        bits[6 * i + 5] = (absQ2 < 2.0f) ? 0 : 1;
    }
}

void demodulate(SharedData_t &data, const std::vector<std::complex<float>> &symbols, std::vector<int> &bits)
{
    auto &mod = data.ofdm_cfg.mod;
    bits.clear();

    switch (mod)
    {
    case 0:
        bpsk_demapper_3gpp(symbols, bits);
        break;
    case 1:
        bits.resize(symbols.size() * 2);
        qpsk_demapper_3gpp(symbols, bits);
        break;
    case 2:
        bits.resize(symbols.size() * 4);
        qam16_demapper_3gpp(symbols, bits);
        break;
    case 3:
        bits.resize(symbols.size() * 6);
        qam64_demapper_3gpp(symbols, bits);
        break;
    default:
        std::cout << "No such demapper\n";
        break;
    }
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
            h[i] = (beta / sqrt(2)) * ((1 + 2 / M_PIf) * sin(M_PIf / (4 * beta)) + (1 - 2 / M_PIf) * cos(M_PIf / (4 * beta)));
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

void ofdm(const std::vector<int> &bits, std::vector<int16_t> &buffer, SharedData_t::OFDMConfig ofdm_config)
{
    int Ncp = ofdm_config.n_cp;
    int N = ofdm_config.n_subcarriers;
    int pilot_spacing = ofdm_config.pilot_spacing;
    int modulation_type = ofdm_config.mod;

    if (N < 4 or pilot_spacing < 2)
        return;

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
        symbols.resize(bits.size() / 6);
        qam64_mapper_3gpp(bits, symbols);
        break;
    case 4:
        symbols.resize(bits.size() / 8);
        qam256_mapper_3gpp(bits, symbols);
        break;
    default:
        symbols.resize(bits.size() / 4);
        qam16_mapper_3gpp(bits, symbols);
        break;
    }

    FFTWPlan ifft(N, false);

    int total_qpsk = (int)symbols.size();
    std::vector<int> data;
    std::vector<int> pilots;
    std::vector<bool> is_guard;
    std::vector<bool> is_pilot;
    calculate_pilots_and_guard(ofdm_config, pilots, data, is_pilot, is_guard);

    int symbols_per_ofdm = static_cast<int>(data.size());
    int num_ofdm_symbols = total_qpsk / symbols_per_ofdm;

    ifft.in[0][0] = 0;
    ifft.in[0][1] = 0;

    if (*ofdm_config.preamble == 0)
    {
        for (size_t i = 1; i <= 127; ++i)
        {
            ifft.in[i][0] = zc[i - 1].real();
            ifft.in[i][1] = zc[i - 1].imag();
        }
    }
    else
    {
        for (size_t i = 1; i < N; i += 2)
        {
            ifft.in[i][0] = 1.0f;
            ifft.in[i][1] = 0.0f;
        }
    }

    fftwf_execute(ifft.plan);

    // Norm
    for (int n = 0; n < N; ++n)
    {
        ifft.out[n][0] /= (float)(N / (3.0 * 16000.0));
        ifft.out[n][1] /= (float)(N / (3.0 * 16000.0));
    }

    // Cyclic Prefix
    for (int n = N - Ncp; n < N; ++n)
    {
        buffer.push_back((int16_t)ifft.out[n][0]);
        buffer.push_back((int16_t)ifft.out[n][1]);
    }

    // Data
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

        for (int k : pilots)
        {
            ifft.in[k][0] = 1.0f;
            ifft.in[k][1] = 0.0f;
        }

        for (int i = 0; i < data.size(); ++i)
        {
            int idx = sym * symbols_per_ofdm + i;
            int k = data[i];

            ifft.in[k][0] = (float)std::real(symbols[idx]);
            ifft.in[k][1] = (float)std::imag(symbols[idx]);
        }

        fftwf_execute(ifft.plan);

        // Norm
        for (int n = 0; n < N; ++n)
        {
            ifft.out[n][0] /= (float)(N / (3.0 * 16000.0f));
            ifft.out[n][1] /= (float)(N / (3.0 * 16000.0f));
        }

        // Cyclic Prefix
        for (int n = N - Ncp; n < N; ++n)
        {
            buffer.push_back((int16_t)ifft.out[n][0]);
            buffer.push_back((int16_t)ifft.out[n][1]);
        }

        // Data
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
    sdr->setGainMode(SOAPY_SDR_RX, 0, config->rx_agc);
    // sdr->setBandwidth(SOAPY_SDR_RX, 0, config->rx_bandwidth);

    // TX parameters
    sdr->setSampleRate(SOAPY_SDR_TX, 0, config->sample_rate);
    sdr->setFrequency(SOAPY_SDR_TX, 0, config->tx_carrier_freq);
    sdr->setGain(SOAPY_SDR_TX, 0, config->tx_gain);
    sdr->setGainMode(SOAPY_SDR_TX, 0, config->tx_agc);
    // sdr->setBandwidth(SOAPY_SDR_TX, 0, config->tx_bandwidth);

    // sdr->setDCOffsetMode(SOAPY_SDR_RX, 0, true);
    // sdr->setDCOffsetMode(SOAPY_SDR_TX, 0, true);
    // sdr->setIQBalanceMode(SOAPY_SDR_RX, 0, true);
    // sdr->setIQBalanceMode(SOAPY_SDR_TX, 0, true);

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
        context.sdr->setGainMode(SOAPY_SDR_RX, 0, context.rx_agc);
        context.sdr->setGainMode(SOAPY_SDR_TX, 0, context.tx_agc);
        context.flags &= ~Flags::APPLY_GAIN;
    }

    if ((context.flags & Flags::APPLY_SAMPLE_RATE) != Flags::None)
    {
        context.sdr->setSampleRate(SOAPY_SDR_RX, 0, context.sample_rate);
        context.sdr->setSampleRate(SOAPY_SDR_TX, 0, context.sample_rate);
        context.flags &= ~Flags::APPLY_SAMPLE_RATE;
    }
}

void change_modulation(sdr_config_t &sdr_config, std::vector<int16_t> &tx_buffer, std::vector<int> &bits, SharedData_t &data)
{
    tx_buffer.clear();
    tx_buffer.reserve(1920 * 4);

    switch (sdr_config.modulation_type)
    {
    case 0:
        bpsk_3gpp(bits, tx_buffer);
        break;
    case 1:
        qpsk_3gpp(bits, tx_buffer);
        break;
    case 2:
        qam16_3gpp(bits, tx_buffer);
        break;
    case 3:
        qam16_3gpp_rrc(bits, tx_buffer);
        break;
    case 4:
        ofdm(bits, tx_buffer, data.ofdm_cfg);
        break;
    default:
        break;
    }

    sdr_config.flags &= ~Flags::REMODULATION;
}

std::vector<std::complex<float>> generate_minn_preamble(size_t N)
{
    std::vector<std::complex<float>> freq(N, { 0, 0 });

    for (size_t k = 1; k < N; k += 4)
        freq[k] = std::complex<float>{ 1.0, 0 }; // BPSK

    return freq;
}

std::vector<std::complex<float>> gardner(const std::vector<std::complex<float>> input, float BnTs, int SPS)
{
    size_t N = input.size();
    size_t M = N / SPS - 1;

    std::vector<std::complex<float>> output(M);

    float zeta = std::sqrt(2.0f) / 2.0f;
    float Kp = 1.0f;
    float teta = (BnTs / 10.0f) / (zeta + 1.0f / (4.0f * zeta));
    float K1 = (-4.0f * zeta * teta) / ((1.0f + 2.0f * zeta * teta + teta * teta) * Kp);
    float K2 = (-4.0f * teta * teta) / ((1.0f + 2.0f * zeta * teta + teta * teta) * Kp);

    float p2 = 0.0f;
    int offset = 0;

    for (size_t i = 0; i < M; ++i)
    {
        size_t base = SPS * i;

        size_t idx0 = base + offset;
        size_t idx1 = base + offset + SPS;
        size_t idxm = base + offset + SPS / 2;

        if (idx1 >= N || idxm >= N)
            break;

        std::complex<float> s1 = input[idx1];
        std::complex<float> s0 = input[idx0];
        std::complex<float> sm = input[idxm];

        float e = (std::real(s1) - std::real(s0)) * std::real(sm) + (std::imag(s1) - std::imag(s0)) * std::imag(sm);

        float p1 = e * K1;
        p2 += p1 + e * K2;
        p2 -= std::floor(p2);

        int new_offset = (int)std::round(p2 * SPS);

        offset = new_offset;

        size_t read_idx = SPS * i + offset;
        output[i] = input[read_idx];
    }

    return output;
}

std::vector<std::complex<float>> costas_loop(const std::vector<std::complex<float>> &samples, float Ki)
{
    const std::ptrdiff_t N = static_cast<std::ptrdiff_t>(samples.size());
    std::vector<std::complex<float>> out(samples.size());

    float theta = 0.0f;
    float freq = 0.0f;
    const float Kp = 0.02f;

    for (std::ptrdiff_t n = 0; n < N; ++n)
    {
        const std::complex<float> rot = std::polar(1.0f, -theta);
        const std::complex<float> r = samples[static_cast<size_t>(n)] * rot;
        out[static_cast<size_t>(n)] = r;

        const float I = std::real(r);
        const float Q = std::imag(r);

        const float I_hat = (I >= 0.0f) ? 1.0f : -1.0f;
        const float Q_hat = (Q >= 0.0f) ? 1.0f : -1.0f;

        float error = I_hat * Q - Q_hat * I;

        freq += Ki * error;
        theta += freq + Kp * error;

        if (theta > M_PI)
            theta -= 2.0f * M_PIf;
        if (theta < -M_PI)
            theta += 2.0f * M_PIf;
    }

    return out;
}

std::vector<std::complex<float>> convolve_ones(const std::vector<std::complex<float>> &x, int SPS)
{
    const std::ptrdiff_t N = static_cast<std::ptrdiff_t>(x.size());
    const std::ptrdiff_t M = (N > 0 && SPS > 0) ? (N + SPS - 1) : 0;

    std::vector<std::complex<float>> y(static_cast<size_t>(M), { 0.0f, 0.0f });

    std::complex<float> acc(0.0f, 0.0f);

    for (std::ptrdiff_t i = 0; i < M; ++i)
    {
        if (i < N)
            acc += x[static_cast<size_t>(i)];

        const std::ptrdiff_t j = i - SPS;
        if (j >= 0 && j < N)
            acc -= x[static_cast<size_t>(j)];

        y[static_cast<size_t>(i)] = acc;
    }

    return y;
}

int zc_sync(const std::vector<std::complex<float>> &rx, const std::vector<std::complex<float>> &zadoff_chu, const float zc_energy, std::vector<float> &plato)
{
    const size_t N = zadoff_chu.size();
    const size_t L = rx.size();

    const float *__restrict r = reinterpret_cast<const float *>(rx.data());
    const float *__restrict zc = reinterpret_cast<const float *>(zadoff_chu.data());

    float max_norm = -1.f;
    int best_idx = -1;

    float current_sig_energy = 0.0f;
    for (size_t k = 0; k < N; ++k)
    {
        const float *__restrict r_offset = &r[2 * k];
        current_sig_energy += r_offset[0] * r_offset[0] + r_offset[1] * r_offset[1];
    }

    for (size_t n = 0; n <= L - N; ++n)
    {
        float sum_re = 0.0f, sum_im = 0.0f;
        const float *__restrict r_offset = &r[2 * n];

        for (size_t k = 0; k < N; ++k)
        {
            float sr = r_offset[2 * k];
            float si = r_offset[2 * k + 1];
            float zr = zc[2 * k];
            float zi = zc[2 * k + 1];

            sum_re += sr * zr + si * zi;
            sum_im += si * zr - sr * zi;
        }

        float norm = (sum_re * sum_re + sum_im * sum_im) / (current_sig_energy * zc_energy + 1e-12f);
        plato[n] = norm;

        if (n < L - N)
        {
            current_sig_energy -= (r_offset[0] * r_offset[0] + r_offset[1] * r_offset[1]);
            current_sig_energy += (r[2 * (n + N)] * r[2 * (n + N)] + r[2 * (n + N) + 1] * r[2 * (n + N) + 1]);
            if (current_sig_energy < 0)
                current_sig_energy = 0;
        }

        if (norm > max_norm)
        {
            max_norm = norm;
            best_idx = static_cast<int>(n);
        }
    }
    return best_idx;
}

int ofdm_cp_sync(const std::vector<std::complex<float>> &r, int N, int Lcp, std::vector<float> &plato)
{
    int size = r.size();
    float max_metric = 0.0f;
    int max_index = -1;

    std::complex<float> P = 0.0f;
    float R = 0.0f;

    for (int i = 0; i < Lcp; i++)
    {
        P += r[i] * std::conj(r[i + N]);
        R += std::norm(r[i + N]);
    }

    for (int d = 0; d < size - N - Lcp; d++)
    {

        float R_cp = 0.0f;
        float R_tail = 0.0f;

        for (int i = 0; i < Lcp; i++)
        {
            R_cp += std::norm(r[d + i]);
            R_tail += std::norm(r[d + i + N]);
        }

        float denom = 0.5f * (R_cp + R_tail);
        float metric = std::norm(P) / (denom * denom + 1e-12f);

        if (metric > max_metric)
        {
            max_metric = metric;
            max_index = d;
        }

        if (d + 1 >= size - N - Lcp)
            break;

        P -= r[d] * std::conj(r[d + N]);
        P += r[d + Lcp] * std::conj(r[d + N + Lcp]);

        R -= std::norm(r[d + N]);
        R += std::norm(r[d + N + Lcp]);
        plato[d] = metric;
    }

    return max_index;
}

void calculate_pilots_and_guard(SharedData_t::OFDMConfig ofdm_config, std::vector<int> &pilots, std::vector<int> &data, std::vector<bool> &is_pilot, std::vector<bool> &is_guard)
{
    size_t N = static_cast<size_t>(ofdm_config.n_subcarriers);
    int PS = ofdm_config.pilot_spacing;

    data.clear();
    pilots.clear();
    is_pilot.resize(N, false);
    is_guard.resize(N, false);

    int counter = 0;
    for (size_t k = 0; k < N; ++k)
    {
        if (k == 0 || (k >= 37 && k <= 91))
        {
            is_guard[k] = true;
            continue;
        }
        if ((counter % PS == 0) || (k == N / 2 - 28) || (k == N / 2 + 28) || (k == N - 1))
        {
            pilots.push_back(k);
            is_pilot[k] = true;
        }
        else
            data.push_back(k);
        counter++;
    }
};

void calculate_pilots_and_guard(SharedData_t::OFDMConfig ofdm_config, std::vector<int> &pilots, std::vector<bool> &is_pilot, std::vector<bool> &is_guard)
{
    size_t N = static_cast<size_t>(ofdm_config.n_subcarriers);
    int PS = ofdm_config.pilot_spacing;

    pilots.clear();
    is_pilot.resize(N, false);
    is_guard.resize(N, false);

    int counter = 0;
    for (size_t k = 0; k < N; ++k)
    {
        if ((k > N / 2 - 28 and k < N / 2 + 27) or k == 0)
        {
            is_guard[k] = true;
            continue;
        }
        if ((counter % PS == 0) || (k == N / 2 - 28) || (k == N / 2 + 28) || (k == N - 1))
        {
            pilots.push_back(k);
            is_pilot[k] = true;
        }
        counter++;
    }
};

void ofdm_equalize(std::vector<std::complex<float>> &input, SharedData_t::OFDMConfig ofdm_config, std::vector<std::complex<float>> &h_est)
{
    int N = ofdm_config.n_subcarriers;
    float accumulated_phase = 0;
    const std::complex<float> known_pilot = { 1.0f, 0.0f };
    std::vector<std::complex<float>> temp = input;
    input.clear();
    h_est.clear();

    std::vector<int> pilots;
    std::vector<int> data;
    std::vector<bool> is_pilot(N, false);
    std::vector<bool> is_guard(N, false);

    calculate_pilots_and_guard(ofdm_config, pilots, data, is_pilot, is_guard);

    std::vector<std::complex<float>> H_prev(N, { 1, 0 });

    for (size_t i = 0; i + N <= temp.size(); i += N)
    {
        std::vector<std::complex<float>> sym(temp.begin() + i, temp.begin() + i + N);

        std::vector<std::complex<float>> H(N, { 0, 0 });
        std::vector<std::complex<float>> equalized(N);

        for (auto k : pilots)
            H[k] = sym[k] / known_pilot;

        for (size_t p = 0; p < pilots.size() - 1; ++p)
        {
            int k1 = pilots[p];
            int k2 = pilots[p + 1];

            auto H1 = H[k1];
            auto H2 = H[k2];

            float a1 = std::arg(H1);
            float a2 = std::arg(H2);

            float da = a2 - a1;
            if (da > M_PIf)
                da -= 2 * M_PIf;
            if (da < -M_PIf)
                da += 2 * M_PIf;

            float m1 = std::abs(H1);
            float m2 = std::abs(H2);

            for (int k = k1 + 1; k < k2; ++k)
            {
                if (is_guard[k])
                    continue;

                float alpha = float(k - k1) / float(k2 - k1);

                float a = a1 + alpha * da;
                float m = m1 + alpha * (m2 - m1);

                H[k] = std::polar(m, a);
            }
        }

        for (int k = 0; k < pilots.front(); ++k)
            if (!is_guard[k])
                H[k] = H[pilots.front()];

        for (int k = pilots.back() + 1; k < N; ++k)
            if (!is_guard[k])
                H[k] = H[pilots.back()];

        for (int k = 1; k < N; ++k)
            if (std::abs(H[k]) > 1e-12f)
                equalized[k] = sym[k] / H[k];
            else
                equalized[k] = sym[k];

        for (auto &k : H)
            h_est.push_back(k);

        float cpe = 0;
        int count = 0;
        for (auto k : pilots)
        {
            cpe += std::arg(equalized[k] / known_pilot);
            count++;
        }
        if (count > 0)
            cpe /= count;

        accumulated_phase += cpe;

        float mean_amp_pilots = 0;
        for (auto k : pilots)
            mean_amp_pilots += std::abs(equalized[k]);
        mean_amp_pilots /= pilots.size();

        for (int k = 0; k < N; ++k)
            if (!is_guard[k])
                equalized[k] /= mean_amp_pilots;

        std::complex<float> rot = std::exp(std::complex<float>(0, -accumulated_phase));
        for (int k = 0; k < N; ++k)
            if (!is_guard[k])
                equalized[k] *= rot;

        for (int k = 0; k < N; ++k)
            if (!is_pilot[k] and !is_guard[k])
                input.push_back(equalized[k]);
    }
}

float estimate_cfo(const std::vector<std::complex<float>> &rx, int N, int max_index, float Fs)
{
    int L = N / 2;
    std::complex<float> P_cfo(0.0f, 0.0f);

    for (int n = 0; n < L; ++n)
        P_cfo += rx[max_index + n] * std::conj(rx[max_index + n + L]);

    float Ts = 1.0f / Fs;
    float cfo = std::arg(P_cfo) / (2.0f * M_PI * L * Ts);

    return cfo;
}

float schmidl_cox_detect(const std::vector<std::complex<float>> &rx, int N, float &cfo_est, int &max_index, std::vector<float> &plato)
{
    size_t L = N / 2;
    size_t rx_size = rx.size();
    if (rx_size < N)
        return 0.0f;

    std::complex<float> P = 0.0f;
    float R = 0.0f;
    float max_metric = 0.0f;

    for (size_t n = 0; n < L; ++n)
    {
        P += rx[n] * std::conj(rx[n + L]);
        R += std::norm(rx[n + L]);
    }

    for (size_t i = 0; i <= rx_size - N; ++i)
    {
        float metric = (R > 0) ? (std::norm(P) / (R * R)) : 0.0f;
        plato[i] = metric;

        if (metric > max_metric)
        {
            max_metric = metric;
            max_index = i;
        }

        if (i + N < rx_size)
        {
            std::complex<float> out_term = rx[i] * std::conj(rx[i + L]);
            std::complex<float> in_term = rx[i + L] * std::conj(rx[i + N]);

            P = P - out_term + in_term;
            R = R - std::norm(rx[i + L]) + std::norm(rx[i + N]);
        }
    }
    return max_metric;
}

std::vector<std::complex<float>> ofdm_zadoff_chu_symbol(SharedData_t &data)
{
    FFTWPlan ifft(data.ofdm_cfg.n_subcarriers, false);
    std::vector<std::complex<float>> zadoff_chu;
    auto zc = generate_zc(127, 5);
    zadoff_chu.reserve(data.ofdm_cfg.n_subcarriers);
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

    for (int n = 0; n < data.ofdm_cfg.n_subcarriers; ++n)
    {
        ifft.out[n][0] /= (float)(data.ofdm_cfg.n_subcarriers / (3.0 * 16000.0));
        ifft.out[n][1] /= (float)(data.ofdm_cfg.n_subcarriers / (3.0 * 16000.0));
    }

    for (int n = 0; n < data.ofdm_cfg.n_subcarriers; ++n)
        zadoff_chu.push_back(std::complex<float>(ifft.out[n][0], ifft.out[n][1]));

    return zadoff_chu;
};

std::vector<std::complex<float>> cfo_est(const std::vector<std::complex<float>> &signal, SharedData &data, sdr_config_s &context)
{
    int N = data.ofdm_cfg.n_subcarriers;
    int CP = data.ofdm_cfg.n_cp;
    float fs = static_cast<float>(context.sample_rate);
    int start = data.dsp.max_index + N;
    std::vector<std::complex<float>> corrected = signal;

    int symbol_len = N + CP;
    for (size_t i = 0; i < 10; ++i)
    {
        int sym_start = start + i * symbol_len;
        if (signal.size() < sym_start + N + CP)
            break;

        std::complex<float> corr = 0;
        for (int n = 0; n < CP; ++n)
            corr += std::conj(signal[sym_start + n]) * signal[sym_start + n + N];

        float epsilon = std::arg(corr) / (2 * M_PIf);
        float delta_f = epsilon * fs / N;

        // data.dsp.cfo = delta_f;

        for (int n = 0; n < N + CP; ++n)
        {
            float phase = -2 * M_PIf * delta_f * (sym_start + n) / fs;
            corrected[sym_start + n] *= std::complex<float>(std::cos(phase), std::sin(phase));
        }
    }

    return corrected;
}

float coarse_cfo(const std::vector<std::complex<float>> &r, int max_index, int N, int Lcp, float fs)
{
    std::complex<float> P = 0.0f;
    for (int i = 0; i < Lcp; ++i)
        P += r[max_index + i] * std::conj(r[max_index + i + N]);

    float epsilon = std::arg(P) / (2 * M_PIf);
    return epsilon * fs / N;
}

int run_dsp(sdr_config_t &context, SharedData_t &data)
{
    FFTWPlan fft(data.ofdm_cfg.n_subcarriers, true);
    std::chrono::steady_clock::time_point start;
    std::chrono::steady_clock::time_point end;
    auto &raw = data.mod.raw;
    float zc_energy = 0.0f;

    std::vector<bool> is_pilot(data.ofdm_cfg.n_subcarriers);
    std::vector<bool> is_guard(data.ofdm_cfg.n_subcarriers);
    std::vector<int> pilots(data.ofdm_cfg.n_subcarriers);
    std::vector<int> datas(data.ofdm_cfg.n_subcarriers);

    calculate_pilots_and_guard(data.ofdm_cfg, pilots, datas, is_pilot, is_guard);

    std::vector<std::complex<float>> for_ofdm;
    std::vector<int16_t> temp(context.buffer_size * 2, 0);
    std::vector<float> dummy(context.buffer_size, 0);
    for_ofdm.reserve(context.buffer_size * 2);
    std::vector<std::complex<float>> zadoff_chu = ofdm_zadoff_chu_symbol(data);
    static float cfo = 0.0f;
    const float *zptr = reinterpret_cast<const float *>(zadoff_chu.data());
    for (int n = 0; n < zadoff_chu.size() * 2; ++n)
        zc_energy += zptr[n] * zptr[n];

    while (!has_flag(context.flags, Flags::EXIT))
    {
        while (data.gui.stopped)
            std::this_thread::sleep_for(std::chrono::milliseconds(50));

        if (data.dsp_buff.read(temp) == 0)
        {
            int16_t *in = temp.data();
            std::complex<float> *out = raw.data();
            size_t n = context.buffer_size;

            for (size_t i = 0; i < n; ++i)
            {
                float I = in[0];
                float Q = in[1];

                out[i] = { I, Q };
                in += 2;
            }
        }
        else
            continue;

        std::atomic_signal_fence(std::memory_order_seq_cst);
        start = std::chrono::steady_clock::now();
        std::atomic_signal_fence(std::memory_order_seq_cst);

        if (data.mod.ModulationType == 0 or // BPSK
            data.mod.ModulationType == 1 or // QPSK
            data.mod.ModulationType == 2)   // QAM16
        {
            data.mod.conv = convolve_ones(raw, 10);
            float max_val = 0.0f;
            for (const auto &x : data.mod.conv)
                max_val = std::max(max_val, std::abs(x));

            if (max_val > 0.0f)
            {
                for (auto &x : data.mod.conv)
                    x = data.dsp.scale_coef * x / max_val;
            }
            data.mod.sync = costas_loop(data.mod.conv, data.dsp.costas_band);
            data.mod.demodul = gardner(data.mod.sync, data.dsp.gardner_band, 2);
        }
        else if (data.mod.ModulationType == 4) // OFDM
        {
            int next = 0;
            for_ofdm = raw;
            if (data.ofdm_cfg.pss)
            {
                switch (data.dsp.sync)
                {
                case 0:
                    data.dsp.max_index = zc_sync(for_ofdm, zadoff_chu, zc_energy, data.gui.plato);
                    break;
                case 1: {
                    static float coarse_mean = 0.0f;
                    data.dsp.max_index = ofdm_cp_sync(for_ofdm, data.ofdm_cfg.n_subcarriers, data.ofdm_cfg.n_cp, dummy);
                    float coarse = coarse_cfo(for_ofdm, data.dsp.max_index, data.ofdm_cfg.n_subcarriers, data.ofdm_cfg.n_cp, context.sample_rate);
                    for (size_t n = 0; n < for_ofdm.size(); ++n)
                    {
                        float phase = 2 * M_PIf * coarse * n / context.sample_rate;
                        for_ofdm[n] *= std::complex<float>(std::cos(phase), std::sin(phase));
                    }
                    coarse_mean = 0.1f * coarse + 0.9f * coarse_mean;
                    data.dsp.cfo = coarse_mean;
                    data.dsp.max_index = zc_sync(for_ofdm, zadoff_chu, zc_energy, data.gui.plato);
                }
                break;
                case 2:
                    schmidl_cox_detect(for_ofdm, data.ofdm_cfg.n_subcarriers, data.dsp.cfo, data.dsp.max_index, data.gui.plato);
                    break;
                }
                if (data.ofdm_cfg.cfo)
                    for_ofdm = cfo_est(for_ofdm, data, context);
            }
            if (data.ofdm_cfg.symbol_sync)
                ofdm_cp_sync(for_ofdm, data.ofdm_cfg.n_subcarriers, data.ofdm_cfg.n_cp, data.gui.plato);
            data.mod.ofdm.clear();
            data.mod.ofdm.resize(data.ofdm_cfg.n_subcarriers * 12);
            if (static_cast<int>(for_ofdm.size()) > data.dsp.max_index + data.ofdm_cfg.n_subcarriers * 2)
                next += data.dsp.max_index + (data.ofdm_cfg.symbol_sync ? 0 : data.ofdm_cfg.n_subcarriers) + data.dsp.offset;
            int last = -data.ofdm_cfg.n_subcarriers;
            int bsr = 1;
            switch (data.mod.ModulationType)
            {
            case 0:
                bsr = 1;
                break;
            case 1:
                bsr = 2;
                break;
            case 2:
                bsr = 4;
                break;
            case 4:
                bsr = 6;
                break;
            default:
                break;
            }
            size_t expected_sym_count = ((data.dsp.N / bsr) / (datas.size()));
            for (size_t n = 0; n < expected_sym_count; ++n)
            {
                if (static_cast<int>(for_ofdm.size()) - next < data.ofdm_cfg.n_subcarriers + data.ofdm_cfg.n_cp)
                    break;
                next += data.ofdm_cfg.n_cp;
                for (size_t i = 0; i < static_cast<size_t>(data.ofdm_cfg.n_subcarriers); ++i)
                {
                    fft.in[i][0] = std::real(for_ofdm[next + i]);
                    fft.in[i][1] = std::imag(for_ofdm[next + i]);
                }
                if (data.ofdm_cfg.fft)
                {
                    fftwf_execute(fft.plan);

                    for (size_t i = 0; i < static_cast<size_t>(data.ofdm_cfg.n_subcarriers); ++i)
                        data.mod.ofdm[i + n * static_cast<size_t>(data.ofdm_cfg.n_subcarriers)] = std::complex<float>(fft.out[i][0], fft.out[i][1]);
                }

                next += data.ofdm_cfg.n_subcarriers;
                last = next;
            }
            if (last > 0)
                data.mod.ofdm.erase(data.mod.ofdm.begin() + last, data.mod.ofdm.end());
            if (data.ofdm_cfg.eq)
                ofdm_equalize(data.mod.ofdm, data.ofdm_cfg, data.gui.estimation);
            data.gui_buff.write(data.mod.ofdm);
            demodulate(data, data.mod.ofdm, data.dsp.bits_rx);
        }

        std::atomic_signal_fence(std::memory_order_seq_cst);
        end = std::chrono::steady_clock::now();
        std::atomic_signal_fence(std::memory_order_seq_cst);

        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        data.history.sdrtime.push_back(data.gui.timed / 1e3);
        if (data.history.sdrtime.size() > 4000)
            data.history.sdrtime.erase(data.history.sdrtime.begin());
        data.history.dsptime.push_back(duration.count() / 1e3);
        if (data.history.dsptime.size() > 4000)
            data.history.dsptime.erase(data.history.dsptime.begin());

        if (!data.gui.stopped)
        {
            data.history.receive.insert(data.history.receive.end(), raw.begin(), raw.end());
            if (static_cast<int>(data.history.receive.size()) > context.buffer_size * 10)
                data.history.receive.erase(data.history.receive.begin(), data.history.receive.begin() + 1920);
            data.gui.metrics.push_back(data.dsp.max_index);
            if (static_cast<int>(data.gui.metrics.size()) > context.buffer_size)
                data.gui.metrics.erase(data.gui.metrics.begin());
            if (data.gui.plato[(data.dsp.max_index < 0 ? 0 : data.dsp.max_index)] > data.dsp.threshold)
                data.gui.stopped = data.gui.can_be_stopped ? true : false;
        }

        compute_fftw(raw, data.signal_spectrum);
    }
    std::cout << "Closing DSP thread\n";
    return 0;
}

int run_sdr(sdr_config_t &context, SharedData_t &data)
{
    // код
    std::vector<std::string> modulations = { "BPSK", "QPSK", "QAM16", "QAM16 RRC", "OFDM" };
    Flags apply = Flags::APPLY_BANDWIDTH | Flags::APPLY_FREQUENCY | Flags::APPLY_GAIN | Flags::APPLY_SAMPLE_RATE;
    while (!has_flag(context.flags, Flags::IS_ACTIVE))
    {
        if (has_flag(context.flags, Flags::EXIT))
            return 0;
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    if (init(&context) != 0)
    {
        std::cerr << "Initialization error\n";
        return -1;
    }
    auto start = std::chrono::steady_clock::now();
    std::vector<int16_t> tx_buffer;
    gen_bits(data.dsp.N, data.dsp.bits_tx);
    change_modulation(context, tx_buffer, data.dsp.bits_tx, data);

    void *tx_buffs[] = { tx_buffer.data() };
    int flags = SOAPY_SDR_HAS_TIME;
    long long timeNs;
    long timeoutUs = 400000;
    size_t k = 0;
    float buff_count = tx_buffer.size() / (context.buffer_size * 2.0);
    auto end = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    data.gui.timed = 0.01 * duration.count() + 0.99 * data.gui.timed;
    std::cout << "First SDR init " << modulations[context.modulation_type] << " " << duration.count() << " mcs\n";
    std::cout << "With [SIZE] = " << data.dsp.bits_tx.size() << " bits\n";
    while (!has_flag(context.flags, Flags::EXIT))
    {
        if (k >= buff_count)
            k = 0;
        auto start = std::chrono::steady_clock::now();

        if (has_flag(context.flags, Flags::REINIT))
            reinit(context);
        if (has_flag(context.flags, apply))
            apply_runtime(context);
        if (has_flag(context.flags, Flags::REMODULATION))
        {
            auto start = std::chrono::steady_clock::now();
            change_modulation(context, tx_buffer, data.dsp.bits_tx, data);
            k = 0;
            buff_count = tx_buffer.size() / (context.buffer_size * 2.0);
            auto end = std::chrono::steady_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
            data.gui.timed = 0.01 * duration.count() + 0.99 * data.gui.timed;
        }
        while (data.gui.stopped)
            std::this_thread::sleep_for(std::chrono::milliseconds(50));

        void *rxbuffs[] = { data.dsp_buff.get_write_buffer().data() };

        int ret = context.sdr->readStream(
            context.rxStream,
            rxbuffs,
            context.buffer_size,
            flags,
            timeNs,
            timeoutUs
        );

        if (ret > 0)
            data.dsp_buff.swap();
        else if (ret == SOAPY_SDR_OVERFLOW)
            std::cout << "OVERFLOW\n";
        else
            std::cout << "ERR " << ret << std::endl;

        if (has_flag(context.flags, Flags::SEND))
        {
            tx_buffs[0] = static_cast<void *>(tx_buffer.data());
            int send = context.sdr->writeStream(context.txStream, (const void *const *)tx_buffs, tx_buffer.size() / 2, flags, timeNs + (4 * 1000 * 1000), timeoutUs);
            data.history.send.push_back(send);
            if (static_cast<int>(data.history.send.size()) > context.buffer_size * 4)
                data.history.send.erase(data.history.send.begin());
        }
        auto end = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        data.gui.timed = 0.01 * duration.count() + 0.99 * data.gui.timed;
        k += 1;
    }
    deinit(&context);
    return 0;
}
