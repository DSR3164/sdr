#pragma once

#include <pluto_lib.h>
#include <vector>
#include <complex>
#include <cmath>
#include <atomic>

template <typename T>
class DoubleBuffer {
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
    std::atomic<int> write_index{ 0 };   // куда пишет SDR
    std::atomic<int> read_index{ 1 };    // откуда читает DSP
    std::atomic<bool> ready{ false };
};

typedef struct SharedData
{
    std::vector<int16_t> tx[2];
    std::vector<int16_t> rx[2];
    DoubleBuffer<int16_t> dsp_buff;
    DoubleBuffer<std::complex<float>> gui_buff;
    std::vector<std::complex<float>> receive_history;
    std::vector<float> signal_spectrum;

    struct Modulation
    {
        int ModulationType;
        int n; // N Subcarriers
        int ncp; // Len of CP
        int ps; // Spacing between pilots

        std::vector<std::complex<float>> raw;
        std::vector<std::complex<float>> conv;
        std::vector<std::complex<float>> sync;
        std::vector<std::complex<float>> demodul;

        std::vector<std::complex<float>> ofdm;

        bool cfo;

    } mod;

    SharedData(int samples_in_buffer, int n, int ncp, int ps, int mod_type)
        :
        dsp_buff(samples_in_buffer * 2),
        gui_buff(samples_in_buffer * 2)
    {
        rx[0].resize(samples_in_buffer * 2, 0);
        rx[1].resize(samples_in_buffer * 2, 0);
        tx[0].resize(samples_in_buffer * 2, 0);
        tx[1].resize(samples_in_buffer * 2, 0);
        receive_history.reserve(samples_in_buffer * 11);
        mod.raw.resize(samples_in_buffer);
        mod.conv.resize(samples_in_buffer);
        mod.sync.resize(samples_in_buffer);
        mod.demodul.resize(samples_in_buffer);
        mod.ofdm.resize(samples_in_buffer);
        mod.ModulationType = mod_type;
        mod.n = n;
        mod.ncp = ncp;
        mod.ps = ps;
        mod.cfo = 0;
    }

} SharedData_t;

std::vector<std::complex<float>> gardner(const std::vector<std::complex<float>> input, float BnTs, int SPS);
std::vector<std::complex<float>> costas_loop(const std::vector<std::complex<float>> &samples, float Ki);
std::vector<std::complex<float>> convolve_ones(const std::vector<std::complex<float>> &x, int SPS);
float estimate_cfo(const std::vector<std::complex<float>> &rx, int N, int max_index, float Fs);
float schmidl_cox_detect(const std::vector<std::complex<float>> &rx, int N, float &cfo_est, int &max_index, std::vector<float> &plato);
int ofdm_zc_corr(const std::vector<std::complex<float>> &r, const std::vector<std::complex<float>> &zc, std::vector<float> &plato);
int ofdm_cp_sync(const std::vector<std::complex<float>> &r, int N, int Lcp, std::vector<float> &plato);
void ofdm_equalize(std::vector<std::complex<float>> input, std::vector<std::complex<float>> &output, int N, int ps);
std::vector<std::complex<float>> ofdm_zadoff_chu_symbol(SharedData_t &data);