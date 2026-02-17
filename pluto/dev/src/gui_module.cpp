#include <GL/glew.h>
#include <SDL2/SDL.h>

#include <SoapySDR/Device.hpp>
#include <SoapySDR/Formats.hpp>
#include "pluto_lib.h"
#include <stdexcept>
#include <cstring>
#include <fftw3.h>
#include "gui.h"
#include <thread>
#include <chrono>
#include "imgui.h"
#include <iostream>

namespace gui
{
    void change_modulation(sdr_config_t *sdr_config, std::vector<int16_t> &tx_buffer)
    {
        int N = 192000;
        std::vector<int> bits(N);
        gen_bits(N, bits);

        tx_buffer.clear();

        switch (sdr_config->modulation_type)
        {
        case 0:
            bpsk_3gpp(bits, tx_buffer, false);
            break;
        case 1:
            qpsk_3gpp(bits, tx_buffer, false);
            break;
        case 2:
            qam16_3gpp(bits, tx_buffer, false);
            break;
        case 3:
            qam16_3gpp_rrc(bits, tx_buffer, false);
            break;
        default:
            break;
        }

        sdr_config->flags &= ~Flags::REMODULATION;
    }

    namespace
    {

        struct FFTWPlan
        {
            std::vector<float> window;
            fftwf_complex *in = nullptr;
            fftwf_complex *out = nullptr;
            fftwf_plan plan = nullptr;

            FFTWPlan() : window(gui::NFFTW)
            {
                for (int i = 0; i < gui::NFFTW; ++i)
                    window[i] = 0.5f - 0.5f * std::cos(2.0f * float(M_PI) * float(i) / float(gui::NFFTW - 1));

                in = reinterpret_cast<fftwf_complex *>(fftwf_malloc(sizeof(fftwf_complex) * gui::NFFTW));
                out = reinterpret_cast<fftwf_complex *>(fftwf_malloc(sizeof(fftwf_complex) * gui::NFFTW));
                if (!in || !out)
                    throw std::bad_alloc{};

                plan = fftwf_plan_dft_1d(gui::NFFTW, in, out, FFTW_FORWARD, FFTW_MEASURE);
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

            FFTWPlan(const FFTWPlan &) = delete;
            FFTWPlan &operator=(const FFTWPlan &) = delete;
        };

        FFTWPlan &fftw_singleton()
        {
            static FFTWPlan p;
            return p;
        }

    } // namespace

    void compute_fftw(const int16_t *iq, float *out_db)
    {
        if (!iq || !out_db)
            return;

        auto &p = fftw_singleton();

        for (int i = 0; i < gui::NFFTW; ++i)
        {
            constexpr float inv = 1.0f / 32768.0f;
            float re = float(iq[2 * i + 0]) * inv;
            float im = float(iq[2 * i + 1]) * inv;
            p.in[i][0] = re * p.window[i];
            p.in[i][1] = im * p.window[i];
        }

        fftwf_execute(p.plan);

        constexpr float eps = 1e-12f;
        for (int i = 0; i < gui::NFFTW; ++i)
        {
            int idx = (i + gui::NFFTW / 2) % gui::NFFTW;
            float re = p.out[idx][0];
            float im = p.out[idx][1];
            float pow_ = re * re + im * im + eps;
            out_db[i] = 10.0f * std::log10(pow_);
        }
    }

    static inline float hann(int n, int N)
    {
        return 0.5f - 0.5f * std::cos(2.0f * (float)M_PI * (float)n / (float)(N - 1));
    }

    void compute_hz(sdr_config_t *cfg, std::vector<float> &x_hz)
    {
        const double Fs = cfg->sample_rate;
        for (int i = 0; i < gui::NFFTW; ++i)
        {
            double f = ((double)i / gui::NFFTW - 0.5) * Fs;
            x_hz[i] = (float)f;
        }
    }

    static inline void db_to_u8(float db, uint8_t &r, uint8_t &g, uint8_t &b, float db_min = -120.0f, float db_max = -20.0f)
    {
        float t = (db - db_min) / (db_max - db_min);
        if (t < 0.0f)
            t = 0.0f;
        if (t > 1.0f)
            t = 1.0f;

        t = std::pow(t, 0.6f);
        if (t == 0.0f)
        {
            r = 0;
            g = 0;
            b = 0;
        }
        else if (t < 0.7f)
        {
            float u = t * 2.0f;
            r = (uint8_t)std::lround(255.0f * u);
            g = 0;
            b = (uint8_t)std::lround(255.0f * (1.0f - u));
        }
        else
        {
            float u = (t - 0.5f) * 2.0f;
            r = 255;
            g = (uint8_t)std::lround(255.0f * u);
            b = (uint8_t)std::lround(255.0f * u);
        }
    }

    static void fft_radix2_inplace(std::vector<std::complex<float>> &a)
    {
        const int n = (int)a.size();
        for (int i = 1, j = 0; i < n; i++)
        {
            int bit = n >> 1;
            for (; j & bit; bit >>= 1)
                j ^= bit;
            j ^= bit;
            if (i < j)
                std::swap(a[i], a[j]);
        }
        for (int len = 2; len <= n; len <<= 1)
        {
            float ang = -2.0f * (float)M_PI / (float)len;
            std::complex<float> wlen(std::cos(ang), std::sin(ang));
            for (int i = 0; i < n; i += len)
            {
                std::complex<float> w(1.0f, 0.0f);
                int half = len >> 1;
                for (int j = 0; j < half; j++)
                {
                    std::complex<float> u = a[i + j];
                    std::complex<float> v = a[i + j + half] * w;
                    a[i + j] = u + v;
                    a[i + j + half] = u - v;
                    w *= wlen;
                }
            }
        }
    }

    void compute_wf_row_u8(const int16_t *iq_interleaved, uint8_t *outRow)
    {
        static std::vector<std::complex<float>> x(gui::NFFT);
        static std::vector<float> mag_db(gui::NFFT);

        for (int n = 0; n < gui::NFFT; n++)
        {
            constexpr float inv = 1.0f / 32768.0f;
            float I = (float)iq_interleaved[2 * n + 0] * inv;
            float Q = (float)iq_interleaved[2 * n + 1] * inv;
            float w = hann(n, gui::NFFT);
            x[n] = std::complex<float>(I * w, Q * w);
        }

        fft_radix2_inplace(x);

        constexpr float eps = 1e-12f;
        for (int k = 0; k < gui::NFFT; k++)
        {
            uint8_t r, g, b;
            int ks = (k + gui::NFFT / 2) & (gui::NFFT - 1);
            float re = x[ks].real();
            float im = x[ks].imag();
            float p = re * re + im * im;
            float db = 10.0f * std::log10(p + eps);
            db_to_u8(db, r, g, b);
            outRow[3 * k + 0] = r;
            outRow[3 * k + 1] = g;
            outRow[3 * k + 2] = b;
        }
    }

} // namespace gui
