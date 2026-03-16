#pragma once
#include <SoapySDR/Device.hpp>
#include <complex>
#include <cstddef>
#include <iostream>
#include <fftw3.h>
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

    std::vector<int16_t> rxbuffer;
    std::vector<int16_t> txbuffer;
    size_t channels[1] = { 0 };
    SoapySDR::Device *sdr;
    SoapySDR::Stream *rxStream;
    SoapySDR::Stream *txStream;
    SoapySDR::Kwargs args;
    bool tx_stream = true;
    bool rx_stream = true;

    sdr_config_s(std::string name, int buf, double sr,
        double tx_f, double rx_f, float tx_g, float rx_g, bool t = true, bool r = true)
        : sdr_name(name),
        modulation_type(1),
        buffer_size(buf),
        sample_rate(sr),
        tx_carrier_freq(tx_f),
        rx_carrier_freq(rx_f),
        rx_bandwidth(10e6),
        tx_bandwidth(1e6),
        tx_gain(tx_g),
        rx_gain(rx_g),
        channels{ 0 },
        sdr(nullptr),
        rxStream(nullptr),
        txStream(nullptr),
        tx_stream(t),
        rx_stream(r)

    {
        rxbuffer.resize(buffer_size * 2);
        auto list = SoapySDR::Device::enumerate();
        if (!list.empty())
        {
            args = list[0];
            for (auto &x : args)
                std::cout << x.first << "\t" << x.second << "\n";
            flags |= Flags::IS_ACTIVE;
        }
    }
} sdr_config_t;

struct FFTWPlan
{
    std::vector<float> window;
    fftwf_complex *in = nullptr;
    fftwf_complex *out = nullptr;
    fftwf_plan plan = nullptr;

    FFTWPlan(int size, bool direction = true) : window(size)
    {
        for (int i = 0; i < size; ++i)
            window[i] = 0.5f - 0.5f * std::cos(2.0f * float(M_PI) * float(i) / float(size - 1));

        in = reinterpret_cast<fftwf_complex *>(fftwf_malloc(sizeof(fftwf_complex) * size));
        out = reinterpret_cast<fftwf_complex *>(fftwf_malloc(sizeof(fftwf_complex) * size));
        if (!in || !out)
            throw std::bad_alloc{};

        plan = fftwf_plan_dft_1d(size, in, out, direction ? FFTW_FORWARD : FFTW_BACKWARD, FFTW_MEASURE);
        if (!plan)
            throw std::runtime_error("fftwf_plan_dft_1d failed");
    }

    ~FFTWPlan()
    {
        if (plan)
            fftwf_destroy_plan(plan);
        if (in)
            fftwf_free(in);
        if (out)
            fftwf_free(out);
    }

    // move constructor
    FFTWPlan(FFTWPlan &&other) noexcept
        : window(std::move(other.window)),
        in(other.in),
        out(other.out),
        plan(other.plan)
    {
        other.in = nullptr;
        other.out = nullptr;
        other.plan = nullptr;
    }

    // move assignment
    FFTWPlan &operator=(FFTWPlan &&other) noexcept
    {
        if (this != &other)
        {
            if (plan) fftwf_destroy_plan(plan);
            if (in)   fftwf_free(in);
            if (out)  fftwf_free(out);

            window = std::move(other.window);
            in = other.in;
            out = other.out;
            plan = other.plan;

            other.in = nullptr;
            other.out = nullptr;
            other.plan = nullptr;
        }
        return *this;
    }
    FFTWPlan(const FFTWPlan &) = delete;
    FFTWPlan &operator=(const FFTWPlan &) = delete;
};

/*!
 * \brief Initialize SDR device and configure RX/TX streams.
 *
 * Creates a SoapySDR device using parameters from `config`,
 * sets sample rate, frequency, gain, bandwidth,
 * and activates RX and/or TX streams in CS16 format.
 *
 * \param config Pointer to SDR configuration structure.
 * \return 0 on success, non-zero on failure.
*/
int init(sdr_config_t *config);

/*!
 * \brief Deinitialize SDR device and release resources.
 *
 * Deactivates and closes RX/TX streams, then destroys
 * the associated SoapySDR device instance.
 *
 * \param config Pointer to SDR configuration structure.
 * \return 0 on completion.
*/
int deinit(sdr_config_t *config);

/*!
 * \brief Reinitialize SDR device.
 *
 * Performs full deinitialization and reinitialization
 * of the SDR backend and clears the REINIT flag.
 *
 * \param context SDR configuration structure.
*/
void reinit(sdr_config_t &context);

/*!
 * \brief Add driver-specific arguments to SoapySDR configuration.
 *
 * Inserts runtime parameters such as direct buffer mode,
 * timestamp generation interval, and loopback control.
 *
 * \param args SoapySDR keyword arguments structure to modify.
 * \return 0 on success.
*/
int add_args(SoapySDR::Kwargs &args);

/*!
 * \brief Apply runtime configuration changes to SDR device.
 *
 * Updates frequency, bandwidth, gain, and sample rate
 * according to active flags in `context`.
 * Each applied parameter clears its corresponding flag.
 *
 * \param context SDR configuration structure.
*/
void apply_runtime(sdr_config_t &context);

/*!
 * \brief Generate Root Raised Cosine (RRC) filter coefficients.
 *
 * Computes impulse response of an RRC filter with roll-off factor `beta`,
 * samples-per-symbol `sps`, and span `N` symbols.
 *
 * The resulting filter has length `N * sps + 1` and is centered in time.
 * Special cases for t = 0 and t = ±T/(4β) are handled to avoid singularities.
 *
 * \param beta Roll-off factor (0 < beta ≤ 1).
 * \param sps Samples per symbol.
 * \param N Filter span in symbols.
 * \param h Output vector of filter coefficients (resized automatically).
*/
void rrc(double beta, int sps, int N, std::vector<double> &h);
void file_to_bits(const std::string &path, std::vector<int> &bits);

/*!
 * \brief Map input `bits` to BPSK symbols and store them in `symbols`.
 *
 *
 * \details
 * Mapping:
 *
 * - `0 -> +0.77 + 0.77j`
 *
 * - `1 -> -0.77 - 0.77j`
 *
 * \param bits is the input std::vector of bits (0s and 1s).
 * \param symbols is the output std::vector of complex symbols.
*/
void bpsk_mapper_3gpp(const std::vector<int> &bits, std::vector<std::complex<double>> &symbols);

/*!
 * \brief Map input `bits` to QPSK symbols and store them in `symbols`.
 *
 * \details
 * Mapping:
 *
 * - `00 -> +0.77 + 0.77j`
 *
 * - `01 -> +0.77 - 0.77j`
 *
 * - `10 -> -0.77 + 0.77j`
 *
 * - `11 -> -0.77 - 0.77j`
 *
 * \param bits is the input std::vector of bits (0s and 1s).
 * \param symbols is the output std::vector of complex symbols.
*/
void qpsk_mapper_3gpp(const std::vector<int> &bits, std::vector<std::complex<double>> &symbols);

/*!
 * \brief Map input `bits` to 16-QAM symbols according to 3GPP standard.
 * \details
 * This function groups the input bits into 4-bit symbols and maps them to
 * complex constellation points with Gray coding as defined in 3GPP TS 36.212.
 * The output is normalized by 1/sqrt(10) to maintain unit average power.
 *
 * \param bits Input vector of bits (0s and 1s). Size must be multiple of 4.
 * \param symbols Output vector of std::complex<float> symbols. Resized automatically.
*/
void qam16_mapper_3gpp(const std::vector<int> &bits, std::vector<std::complex<double>> &symbols);

/*!
* \brief Upsample the input symbols with zeros by a factor of `up` and store the result in `upsampled`.
* \param symbols is the input std::vector of complex symbols.
* \param upsampled is the output std::vector of complex samples after upsampling.
* \param up is the upsampling factor (default is 10).
*/
void upsample(const std::vector<std::complex<double>> &symbols, std::vector<std::complex<double>> &upsampled, int up = 10);

/*!
 * \brief Perform a complex convolution of input signal `a` with complex filter `b`.
 *
 * \param a Input vector of complex samples.
 * \param b Vector of complex filter coefficients.
 * \param y Output vector of complex samples (size = a.size()).
 *
 * \note The convolution assumes zero-padding for indices where n-m < 0.
 */
void filter_complex(const std::vector<std::complex<double>> &a, const std::vector<double> &b, std::vector<std::complex<double>> &y);

/*!
 * \brief Apply a root-raised-cosine (RRC) filter to the input signal.
 *
 * Convolves the complex input vector `a` with the real filter coefficients `b`
 * and stores the filtered complex output in `y`.
 *
 * \param a Input vector of complex samples.
 * \param b Vector of real RRC filter coefficients.
 * \param y Output vector of complex samples, resized automatically.
*/
void filter_rrc(const std::vector<std::complex<double>> &a, const std::vector<double> &b, std::vector<std::complex<double>> &y);

/*!
 * \brief Map input bits to BPSK symbols according to 3GPP standard.
 *
 * Each bit is mapped to +1 or -1 and optionally upsampled by `sps`.
 *
 * \param bits Input vector of bits (0s and 1s).
 * \param buffer Output buffer of int16_t samples.
 * \param sps Samples per symbol (upsampling factor), default is 10.
*/
void bpsk_3gpp(const std::vector<int> &bits, std::vector<int16_t> &buffer, int sps = 10);

/*!
 * \brief Map input bits to QPSK symbols according to 3GPP standard.
 *
 * Groups input bits into 2-bit symbols, maps them to QPSK constellation,
 * and optionally upsamples by `sps`.
 *
 * \param bits Input vector of bits (0s and 1s).
 * \param buffer Output buffer of int16_t samples.
 * \param sps Samples per symbol (upsampling factor), default is 10.
*/
void qpsk_3gpp(const std::vector<int> &bits, std::vector<int16_t> &buffer, int sps = 10);

/*!
 * \brief Map input bits to 16-QAM symbols according to 3GPP standard.
 *
 * Groups input bits into 4-bit symbols, maps them to 16-QAM constellation
 * with Gray coding, and optionally upsamples by `sps`.
 *
 * \param bits Input vector of bits (0s and 1s). Size must be multiple of 4.
 * \param buffer Output buffer of int16_t samples.
 * \param sps Samples per symbol (upsampling factor), default is 10.
*/
void qam16_3gpp(const std::vector<int> &bits, std::vector<int16_t> &buffer, int sps = 10);

/*!
 * \brief Map input bits to 16-QAM symbols and apply RRC pulse shaping.
 *
 * Combines 16-QAM mapping and RRC filtering. Upsamples symbols by `sps`.
 *
 * \param bits Input vector of bits (0s and 1s). Size must be multiple of 4.
 * \param buffer Output buffer of int16_t samples.
 * \param sps Samples per symbol (upsampling factor), default is 10.
*/
void qam16_3gpp_rrc(const std::vector<int> &bits, std::vector<int16_t> &buffer, int sps = 10);

/*!
 * \brief Generate an OFDM symbol buffer from input bits.
 *
 * Maps bits to QAM symbols, inserts pilots at given spacing, performs IFFT,
 * and adds cyclic prefix of length `Ncp`.
 *
 * \param bits Input vector of bits.
 * \param buffer Output buffer of int16_t OFDM samples.
 * \param N Number of subcarriers.
 * \param Ncp Length of cyclic prefix.
 * \param pilot_spacing Spacing between pilot subcarriers, default is 4.
*/
void ofdm(const std::vector<int> &bits, std::vector<int16_t> &buffer, int N, int Ncp, int pilot_spacing = 4, int modulation_type = 2);

/*!
 * \brief Insert a Barker sequence into the symbol buffer for synchronization.
 *
 * Upsamples the Barker sequence by `sps` and inserts in begin of `symbols`.
 *
 * \param symbols Output symbol buffer to insert Barker sequence into.
 * \param sps Samples per symbol (upsampling factor), default is 10.
*/
void implement_barker(std::vector<int16_t> &symbols, int sps = 10);

/*!
 * \brief Generate a random vector of bits.
 *
 * Fills `bits` with `N` random 0s and 1s.
 *
 * \param N Number of bits to generate.
 * \param bits Output vector of bits.
*/
void gen_bits(int N, std::vector<int> &bits);

std::vector<std::complex<float>> generate_minn_preamble(size_t N);
std::vector<std::complex<float>> generate_zc(int L, int q);
