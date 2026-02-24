#pragma once
#include <SoapySDR/Device.hpp>
#include <complex>
#include <cstddef>
#include <vector>
#include <cstring>
#include <cstdint>

enum class Flags : uint16_t
{
    None = 0,
    APPLY_GAIN = 1 << 0,
    APPLY_FREQUENCY = 1 << 1,
    APPLY_BANDWIDTH = 1 << 2,
    APPLY_SAMPLE_RATE = 1 << 3,
    REINIT = 1 << 4,
    REMODULATION = 1 << 5,
    SEND = 1 << 6,
    EXIT = 1 << 7,
    IS_ACTIVE = 1 << 8,
};

inline Flags operator|(Flags a, Flags b)
{
    return static_cast<Flags>(
        static_cast<uint16_t>(a) |
        static_cast<uint16_t>(b));
}

inline Flags operator&(Flags a, Flags b)
{
    return static_cast<Flags>(
        static_cast<uint16_t>(a) &
        static_cast<uint16_t>(b));
}

inline Flags operator~(Flags a)
{
    return static_cast<Flags>(
        ~static_cast<uint16_t>(a));
}

inline Flags &operator|=(Flags &a, Flags b)
{
    a = a | b;
    return a;
}

inline Flags &operator&=(Flags &a, Flags b)
{
    a = static_cast<Flags>(
        static_cast<uint16_t>(a) &
        static_cast<uint16_t>(b));
    return a;
}

inline bool has_flag(Flags flags, Flags f)
{
    return (flags & f) != Flags::None;
}

inline bool has_any_except(Flags flags, Flags excluded)
{
    return (flags & ~excluded) != Flags::None;
}

typedef struct sdr_config_s
{
    std::string sdr_name;
    int sdr_id;
    Flags flags = Flags::None;

    int modulation_type;
    int buffer_size;
    double sample_rate;

    double tx_carrier_freq;
    double rx_carrier_freq;
    double rx_bandwidth;
    double tx_bandwidth;

    float tx_gain;
    float rx_gain;

    std::vector<int16_t> buffer;
    int n;
    int ncp;
    int ps;
    size_t channels[1] = { 0 };
    SoapySDR::Device *sdr;
    SoapySDR::Stream *rxStream;
    SoapySDR::Stream *txStream;
    SoapySDR::Kwargs args;

    sdr_config_s(std::string name, int buf, double sr,
        double tx_f, double rx_f, float tx_g, float rx_g)
        : sdr_name(name),
        modulation_type(1),
        buffer_size(buf),
        sample_rate(sr),
        tx_carrier_freq(tx_f),
        rx_carrier_freq(rx_f),
        tx_gain(tx_g),
        rx_gain(rx_g),
        channels{ 0 },
        sdr(nullptr),
        rxStream(nullptr),
        txStream(nullptr),
        n(40),
        ncp(4),
        ps(37)
    {
        buffer.resize(buf * 2);
        auto list = SoapySDR::Device::enumerate();
        if (!list.empty())
        {
            args = list[0];
            flags |= Flags::IS_ACTIVE;
        }
    }
} sdr_config_t;

int init(sdr_config_t *config);
int deinit(sdr_config_t *config);
void reinit(sdr_config_t &context);
int add_args(SoapySDR::Kwargs &args);
void apply_runtime(sdr_config_t &context);
void rrc(double beta, int sps, int N, std::vector<double> &h);
void file_to_bits(const std::string &path, std::vector<int> &bits);
void bpsk_mapper(const std::vector<int> &bits, std::vector<std::complex<double>> &symbols);
void qpsk_mapper(const std::vector<int> &bits, std::vector<std::complex<double>> &symbols);
void bpsk_mapper_3gpp(const std::vector<int> &bits, std::vector<std::complex<double>> &symbols);
void qpsk_mapper_3gpp(const std::vector<int> &bits, std::vector<std::complex<double>> &symbols);
void qam16_mapper_3gpp(const std::vector<int> &bits, std::vector<std::complex<double>> &symbols);
void upsample(const std::vector<std::complex<double>> &symbols, std::vector<std::complex<double>> &upsampled, int up = 10);
void filter_i(const std::vector<std::complex<double>> &a, const std::vector<double> &b, std::vector<double> &y);
void filter_q(const std::vector<std::complex<double>> &a, const std::vector<double> &b, std::vector<double> &y);
void filter_rrc(const std::vector<std::complex<double>> &a, const std::vector<double> &b, std::vector<std::complex<double>> &y);
void bpsk(const std::vector<int> &bits, std::vector<int16_t> &buffer, bool timestamp = false, int sps = 10);
void qpsk(const std::vector<int> &bits, std::vector<int16_t> &buffer, bool timestamp = false, int sps = 10);
void bpsk_3gpp(const std::vector<int> &bits, std::vector<int16_t> &buffer, bool timestamp, int sps = 10);
void qpsk_3gpp(const std::vector<int> &bits, std::vector<int16_t> &buffer, bool timestamp, int sps = 10);
void qam16_3gpp(const std::vector<int> &bits, std::vector<int16_t> &buffer, bool timestamp, int sps = 10);
void qam16_3gpp_rrc(const std::vector<int> &bits, std::vector<int16_t> &buffer, bool timestamp, int sps = 10);
void ofdm(const std::vector<int> &bits, std::vector<int16_t> &buffer, int N, int Ncp, int pilot_spacing = 4);
void implement_barker(std::vector<int16_t> &symbols, int sps = 10);
void gen_bits(int N, std::vector<int> &bits);
int16_t *read_pcm(const char *filename, size_t *sample_count);
