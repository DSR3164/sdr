#include <dsp_module.h>

#include <vector>
#include <complex>
#include <cmath>
#include <cstddef>

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

        float e =
            (std::real(s1) - std::real(s0)) * std::real(sm) +
            (std::imag(s1) - std::imag(s0)) * std::imag(sm);

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
