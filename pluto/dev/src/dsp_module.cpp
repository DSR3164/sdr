#include <dsp_module.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#include <vector>
#include <complex>
#include <cstdint>
#include <cmath>
#include <cstddef>

static inline std::complex<float> iq_to_cf(const int16_t I, const int16_t Q, float scale)
{
    return { static_cast<float>(I) * scale, static_cast<float>(Q) * scale };
}

GardnerResult gardner_timing_recovery(const std::vector<int16_t>& iq_interleaved,
                           float BnTs, int SPS,
                           float scale)
{
    GardnerResult res;

    if (SPS <= 0) return res;
    if (iq_interleaved.size() < 2) return res;
    if ((iq_interleaved.size() & 1u) != 0u) return res; // must be even

    const std::ptrdiff_t N = static_cast<std::ptrdiff_t>(iq_interleaved.size() / 2); // complex samples
    if (N <= 0) return res;

    const std::ptrdiff_t M = N / SPS - 1;
    if (M <= 0) return res;

    const std::ptrdiff_t out_len = M / 2;
    if (out_len <= 0) return res;

    res.fixed.resize(static_cast<size_t>(out_len));
    res.offsets.resize(static_cast<size_t>(out_len));

    const float zeta = std::sqrt(2.0f) / 2.0f;
    const float Kp = 1.0f;
    const float teta = (BnTs / 10.0f) / (zeta + 1.0f / (4.0f * zeta));
    const float K1 = (-4.0f * zeta * teta) / ((1.0f + 2.0f * zeta * teta + teta * teta) * Kp);
    const float K2 = (-4.0f * teta * teta) / ((1.0f + 2.0f * zeta * teta + teta * teta) * Kp);

    float p2 = 0.0f;
    int offset = 0;

    auto get_cf = [&](std::ptrdiff_t idx) -> std::complex<float> {
        // idx in [0..N-1]
        const size_t b = static_cast<size_t>(2 * idx);
        return iq_to_cf(iq_interleaved[b], iq_interleaved[b + 1], scale);
    };

    for (std::ptrdiff_t i = 0; i < out_len; ++i)
    {
        const std::ptrdiff_t base = static_cast<std::ptrdiff_t>(SPS) * i;
        const std::ptrdiff_t idx0 = base + offset;
        const std::ptrdiff_t idx1 = base + offset + SPS;
        const std::ptrdiff_t idxm = base + offset + SPS / 2;

        // Защита от выхода за границы (в pybind версии этого не было, но тут нужно)
        if (idx0 < 0 || idxm < 0 || idx1 < 0 || idx0 >= N || idxm >= N || idx1 >= N)
            break;

        const std::complex<float> s1 = get_cf(idx1);
        const std::complex<float> s0 = get_cf(idx0);
        const std::complex<float> sm = get_cf(idxm);

        const float e =
            (std::real(s1) - std::real(s0)) * std::real(sm) +
            (std::imag(s1) - std::imag(s0)) * std::imag(sm);

        const float p1 = e * K1;
        p2 += p1 + e * K2;
        p2 -= std::floor(p2);

        offset = static_cast<int>(std::lround(p2 * SPS));

        const std::ptrdiff_t read_idx = static_cast<std::ptrdiff_t>(SPS) * i + offset;
        if (read_idx < 0 || read_idx >= N) break;

        res.fixed[static_cast<size_t>(i)] = get_cf(read_idx);
        res.offsets[static_cast<size_t>(i)] = offset;
    }

    return res;
}


std::vector<std::complex<float>> costas_loop(
    const std::vector<std::complex<float>> &samples,
    float Ki)
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
        if (error > 1.0f)
            error = 1.0f;
        if (error < -1.0f)
            error = -1.0f;

        freq += Ki * error;
        theta += freq + Kp * error;

        if (theta > M_PI)
            theta -= 2.0f * static_cast<float>(M_PI);
        if (theta < -M_PI)
            theta += 2.0f * static_cast<float>(M_PI);
    }

    return out;
}

std::vector<std::complex<float>> convolve_ones(
    const std::vector<std::complex<float>> &x,
    int SPS)
{
    const std::ptrdiff_t N = static_cast<std::ptrdiff_t>(x.size());
    const std::ptrdiff_t M = (N > 0 && SPS > 0) ? (N + SPS - 1) : 0;

    std::vector<std::complex<float>> y(static_cast<size_t>(M), {0.0f, 0.0f});

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

static inline std::complex<float> nearest_qam16(const std::complex<float> &r)
{
    auto round_level = [](float x) -> float
    {
        if (x < -2.0f)
            return -3.0f;
        if (x < 0.0f)
            return -1.0f;
        if (x < 2.0f)
            return 1.0f;
        return 3.0f;
    };

    return {round_level(std::real(r)), round_level(std::imag(r))};
}

std::vector<std::complex<float>> costas_loop_qam16(
    const std::vector<std::complex<float>> &samples,
    float Ki)
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

        const std::complex<float> nearest = nearest_qam16(r);

        float error = (std::real(r) * std::imag(nearest) - std::imag(r) * std::real(nearest));
        error = std::max(-1.0f, std::min(1.0f, error));

        freq += Ki * error;
        theta += freq + Kp * error;

        if (theta > M_PI)
            theta -= 2.0f * static_cast<float>(M_PI);
        if (theta < -M_PI)
            theta += 2.0f * static_cast<float>(M_PI);
    }

    return out;
}

static inline float phase_detector_qpsk(const std::complex<float> &r)
{
    const float I = r.real();
    const float Q = r.imag();

    const float I_hat = (I >= 0.0f) ? 1.0f : -1.0f;
    const float Q_hat = (Q >= 0.0f) ? 1.0f : -1.0f;

    return I_hat * Q - Q_hat * I;
}

CostasQpskResult costas_loop_qpsk(
    const std::vector<std::complex<float>> &samples,
    float loop_bw)
{
    const std::ptrdiff_t N = static_cast<std::ptrdiff_t>(samples.size());

    CostasQpskResult res;
    res.out.resize(samples.size());
    res.freq.resize(samples.size());
    res.phase.resize(samples.size());
    res.error.resize(samples.size());

    float d_phase = 0.0f;
    float d_freq = 0.0f;

    const float damping = 0.707f;
    const float denom = 1.0f + 2.0f * damping * loop_bw + loop_bw * loop_bw;
    const float Kp = (2.0f * damping * loop_bw) / denom;
    const float Ki = (loop_bw * loop_bw) / denom;

    for (std::ptrdiff_t n = 0; n < N; ++n)
    {
        const std::complex<float> nco = std::polar(1.0f, -d_phase);
        const std::complex<float> r = samples[static_cast<size_t>(n)] * nco;

        res.out[static_cast<size_t>(n)] = r;

        float e = phase_detector_qpsk(r);
        if (e > 1.0f)
            e = 1.0f;
        if (e < -1.0f)
            e = -1.0f;
        res.error[static_cast<size_t>(n)] = e;

        d_freq += Ki * e;
        d_phase += d_freq + Kp * e;

        if (d_phase > M_PI)
            d_phase -= 2.0f * static_cast<float>(M_PI);
        if (d_phase < -M_PI)
            d_phase += 2.0f * static_cast<float>(M_PI);

        res.freq[static_cast<size_t>(n)] = d_freq;
        res.phase[static_cast<size_t>(n)] = d_phase;
    }

    return res;
}

std::vector<std::complex<double>> rrc_mf(
    const std::vector<std::complex<double>> &samples,
    double beta, int sps, int span)
{
    const int N = static_cast<int>(samples.size());

    std::vector<double> h;
    rrc(beta, sps, span, h);
    const int L = static_cast<int>(h.size());

    const int N_out = (N > 0 && L > 0) ? (N + L - 1) : 0;
    std::vector<std::complex<double>> out(static_cast<size_t>(N_out), std::complex<double>(0.0f, 0.0f));

    for (int n = 0; n < N_out; ++n)
    {
        std::complex<double> acc(0.0f, 0.0f);
        for (int k = 0; k < L; ++k)
        {
            const int xi = n - k;
            if (xi >= 0 && xi < N)
            {
                acc += samples[static_cast<size_t>(xi)] *
                       std::complex<double>(static_cast<float>(h[L - 1 - k]), 0.0f);
            }
        }
        out[static_cast<size_t>(n)] = acc;
    }

    return out;
}
