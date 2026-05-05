#pragma once

#include <vector>
#include <complex>
#include <cmath>
#include <atomic>
#include <fftw3.h>
#include <iostream>
#include <SoapySDR/Device.hpp>

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
    size_t buffer_size;
    double sample_rate;

    double tx_carrier_freq;
    double rx_carrier_freq;
    double rx_bandwidth;
    double tx_bandwidth;

    float tx_gain = 70.0f;
    float rx_gain = 70.0f;
    bool rx_agc = false;
    bool tx_agc = false;

    std::vector<int16_t> rxbuffer;
    std::vector<int16_t> txbuffer;
    size_t channels[1] = {0};
    SoapySDR::Device *sdr;
    SoapySDR::Stream *rxStream;
    SoapySDR::Stream *txStream;
    SoapySDR::Kwargs args;
    bool tx_stream = true;
    bool rx_stream = true;

    sdr_config_s(std::string name, size_t buf, double sr,
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
          channels{0},
          sdr(nullptr),
          rxStream(nullptr),
          txStream(nullptr),
          tx_stream(t),
          rx_stream(r)

    {
        rxbuffer.resize(buffer_size * 2);
        auto list = SoapySDR::Device::enumerate();
        if (!list.empty() and (list[0]["uri"] != "ip:pluto.local"))
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

    FFTWPlan(size_t size, bool direction = true) : window(size)
    {
        for (size_t i = 0; i < size; ++i)
            window[i] = 0.5f - 0.5f * std::cos(2.0f * float(M_PI) * float(i) / float(size - 1));

        in = reinterpret_cast<fftwf_complex *>(fftwf_malloc(sizeof(fftwf_complex) * size));
        out = reinterpret_cast<fftwf_complex *>(fftwf_malloc(sizeof(fftwf_complex) * size));
        if (!in || !out)
            throw std::bad_alloc{};

        plan = fftwf_plan_dft_1d(static_cast<int>(size), in, out, direction ? FFTW_FORWARD : FFTW_BACKWARD, FFTW_MEASURE);
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
            if (plan)
                fftwf_destroy_plan(plan);
            if (in)
                fftwf_free(in);
            if (out)
                fftwf_free(out);

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

template <typename T>
class DoubleBuffer
{
public:
    DoubleBuffer(size_t reserve_size = 4096)
    {
        buff[0].resize(reserve_size);
        buff[1].resize(reserve_size);
    }
    ~DoubleBuffer() = default;

    int read(std::vector<T> &buffer)
    {
        if (!ready.load(std::memory_order_acquire))
            return -1;
        else
        {
            int ri = read_index.load(std::memory_order_relaxed);
            buffer = buff[ri];
            ready.store(false, std::memory_order_relaxed);
            return 0;
        }
    };
    int write(std::vector<T> &buffer)
    {
        int wi = write_index.load(std::memory_order_relaxed);
        buff[wi] = buffer;
        ready.store(true, std::memory_order_release);
        read_index.store(wi, std::memory_order_relaxed);
        write_index.store(wi ^ 1, std::memory_order_relaxed);
        return 0;
    };
    std::vector<T> &get_write_buffer()
    {
        int index = write_index.load(std::memory_order_relaxed);
        return buff[index];
    };
    int swap()
    {
        int wi = write_index.load(std::memory_order_relaxed);
        read_index.store(wi, std::memory_order_relaxed);
        write_index.store(wi ^ 1, std::memory_order_relaxed);
        ready.store(true, std::memory_order_release);
        return 0;
    };
    bool is_ready() const
    {
        return ready.load(std::memory_order_relaxed);
    }

private:
    std::vector<T> buff[2];
    std::atomic<int> write_index{0}; // куда пишет SDR
    std::atomic<int> read_index{1};  // откуда читает DSP
    std::atomic<bool> ready{false};
};

typedef struct SharedData
{
    DoubleBuffer<int16_t> dsp_buff;
    DoubleBuffer<std::complex<float>> gui_buff;
    std::vector<float> signal_spectrum;

    struct DSP
    {
        size_t N = 128 * 2 * 60;
        std::vector<int> bits_tx;
        std::vector<int> bits_rx;
        double gardner_band = 1;
        double costas_band = 15e-4;
        float scale_coef = 1.0f;

        bool changed = false;

        float threshold = 0.3f;
        float timed = 1.0f;
        int countdown = 0;

        float cfo = 0.0f;
        int max_index = 0;
        int offset = -2;
        int sync = 0;
    } dsp;

    struct OFDMConfig
    {
        int mod = 2;

        bool pss = true;
        bool symbol_sync = false;
        bool eq = true;
        bool fft = true;
        int n_subcarriers = 128;
        int pilot_spacing = 25;
        int n_cp = 32;
        bool cfo = true;
        int *preamble;
    } ofdm_cfg;

    struct GUI
    {
        std::vector<float> metrics;
        std::vector<std::complex<float>> estimation;
        std::vector<float> plato;

        bool stopped = false;
        bool can_be_stopped = false;
        bool debug = false;
        bool fps_lock = true;
        float timed = 1.0f;

        bool x_init = false;
    } gui;

    struct History
    {
        std::vector<int> send;
        std::vector<std::complex<float>> receive;
        std::vector<float> sdrtime;
        std::vector<float> dsptime;
    } history;

    struct Modulation
    {
        int ModulationType;

        std::vector<std::complex<float>> raw;
        std::vector<std::complex<float>> conv;
        std::vector<std::complex<float>> sync;
        std::vector<std::complex<float>> demodul;
        std::vector<std::complex<float>> ofdm;
    } mod;

    SharedData(size_t samples_in_buffer, int n, int ncp, int ps, int mod_type)
        : dsp_buff(samples_in_buffer * 2),
          gui_buff(samples_in_buffer * 2)
    {
        history.receive.reserve(samples_in_buffer * 11);

        mod.raw.resize(samples_in_buffer);
        mod.conv.resize(samples_in_buffer);
        mod.sync.resize(samples_in_buffer);
        mod.demodul.resize(samples_in_buffer);
        mod.ofdm.resize(samples_in_buffer);
        dsp.bits_rx.reserve(dsp.N);
        dsp.bits_tx.reserve(dsp.N);

        mod.ModulationType = mod_type;
        ofdm_cfg.n_subcarriers = n;
        ofdm_cfg.pilot_spacing = ps;
        ofdm_cfg.n_cp = ncp;
        gui.plato.resize(1920);
        gui.metrics.resize(1920);
        history.dsptime.resize(1920);
        history.sdrtime.resize(1920);
        history.send.resize(1920);
        history.receive.resize(1920);

        ofdm_cfg.preamble = &dsp.sync;
    }

} SharedData_t;

void gen_bits(int N, std::vector<int> &bits);
void bpsk_3gpp(const std::vector<int> &bits, std::vector<int16_t> &buffer, int sps = 10);
void qpsk_3gpp(const std::vector<int> &bits, std::vector<int16_t> &buffer, int sps = 10);
void qam16_3gpp(const std::vector<int> &bits, std::vector<int16_t> &buffer, int sps = 10);
void qam16_3gpp_rrc(const std::vector<int> &bits, std::vector<int16_t> &buffer, int sps = 10);
void ofdm(const std::vector<int> &bits, std::vector<int16_t> &buffer, SharedData_t::OFDMConfig ofdm_config);
void change_modulation(sdr_config_t &sdr_config, std::vector<int16_t> &tx_buffer, std::vector<int> &bits, SharedData_t &data);
void calculate_pilots_and_guard(SharedData_t::OFDMConfig ofdm_config, std::vector<int> &pilots, std::vector<int> &data, std::vector<bool> &is_pilot, std::vector<bool> &is_guard);

int run_sdr(sdr_config_t &context, SharedData_t &data);
int run_dsp(sdr_config_t &context, SharedData_t &data);
