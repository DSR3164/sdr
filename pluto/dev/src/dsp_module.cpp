#include <dsp_module.h>
#include <pluto_lib.h>

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

int ofdm_zc_corr(const std::vector<std::complex<float>> &r, const std::vector<std::complex<float>> &zc, std::vector<float> &plato)
{

    const int N = zc.size();
    const int L = r.size();

    int best_pos = -1;
    float best_val = 0;

    const float *rptr = reinterpret_cast<const float *>(r.data());
    const float *zptr = reinterpret_cast<const float *>(zc.data());

    float zc_energy = 0;
    for (int n = 0; n < N; n++)
    {
        float br = zptr[2 * n];
        float bi = zptr[2 * n + 1];
        zc_energy += br * br + bi * bi;
    }

    for (int k = 0; k <= L - N; k++)
    {
        float re = 0, im = 0;
        float energy = 0;

        const float *rp = rptr + 2 * k;

        for (int n = 0; n < N; n++)
        {
            float ar = rp[2 * n];
            float ai = rp[2 * n + 1];

            float br = zptr[2 * n];
            float bi = zptr[2 * n + 1];

            re += ar * br + ai * bi;
            im += ai * br - ar * bi;

            energy += ar * ar + ai * ai;
        }

        float corr = re * re + im * im;
        float v = corr / (energy * zc_energy + 1e-12f);

        plato[k] = v;

        if (v > best_val)
        {
            best_val = v;
            best_pos = k;
        }
    }

    return best_pos;
}

int ofdm_cp_sync(const std::vector<std::complex<float>> &r, int N, int Lcp, std::vector<float> &plato)
{
    int size = r.size();
    float max_metric = 0.0f;
    int max_index = -1;

    std::complex<float> P = 0.0f;
    float R = 0.0f;
    int timeout = 3;

    for (int i = 0; i < Lcp; i++)
    {
        P += r[i] * std::conj(r[i + N]);
        R += std::norm(r[i + N]);
    }

    for (int d = 0; d < size - N - Lcp; d++)
    {

        float metric = std::norm(P) / (R * R + 1e-12f);

        if (metric > max_metric
            and metric > 0.75
            and d > 150)
        {
            max_metric = metric;
            max_index = d;
        }
        else if (max_metric > metric and metric > 0.75
            and d > 150)
        {
            if (timeout)
                timeout -= 1;
            else
                return max_metric;
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

void ofdm_equalize(std::vector<std::complex<float>> &input, int N, int ps)
{
    const int DC = N / 2;
    const std::complex<float> known_pilot = { 2.0f, 0.0f };

    std::vector<int> pilots;
    std::vector<bool> is_pilot(N, false);

    int counter = 0;
    for (int k = 1; k < N; ++k)
    {
        if (k > N / 2 - 28 and k < N / 2 + 27)
        {
            is_pilot[k] = true;
            continue;
        }
        if (counter % ps == 0)
        {
            pilots.push_back(k);
            is_pilot[k] = true;
        }
        counter++;
    }
    is_pilot[DC] = true;

    std::vector<std::complex<float>> H_prev(N, { 1,0 });

    for (size_t i = 0; i + N <= input.size(); i += N)
    {
        std::vector<std::complex<float>> sym(input.begin() + i,
            input.begin() + i + N);

        std::vector<std::complex<float>> H(N, { 0,0 });
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
            if (da > M_PI) da -= 2 * M_PI;
            if (da < -M_PI) da += 2 * M_PI;

            float m1 = std::abs(H1);
            float m2 = std::abs(H2);

            for (int k = k1 + 1; k < k2; ++k)
            {
                if (k == DC) continue;

                float alpha = float(k - k1) / float(k2 - k1);

                float a = a1 + alpha * da;
                float m = m1 + alpha * (m2 - m1);

                H[k] = std::polar(m, a);
            }
        }

        for (int k = 0; k < pilots.front(); ++k)
            if (k != DC) H[k] = H[pilots.front()];

        for (int k = pilots.back() + 1; k < N; ++k)
            if (k != DC) H[k] = H[pilots.back()];

        for (int k = 1; k < N; ++k)
            if (std::abs(H[k]) > 1e-12f)
                equalized[k] = sym[k] / H[k];
            else
                equalized[k] = sym[k];

        float phase = 0;
        for (auto k : pilots)
            phase += std::arg(equalized[k] / known_pilot);

        phase /= pilots.size();

        std::complex<float> rot = std::exp(std::complex<float>(0, -phase));

        for (int k = 0; k < N; ++k)
            if (k != DC)
                equalized[k] *= rot;

        for (int k = 1; k < N; ++k)
            if (!is_pilot[k])
                input[k + i] = (equalized[k]);
            else
                input[k + i] = { 0.0f, 0.0f };
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
    plato.resize(0);
    plato.reserve(1920);

    size_t L = N / 2;
    int size = rx.size();

    if (size < N + 1)
        return -1;

    std::complex<float> P = 0.0f;
    float R = 0.0f;
    float metric = 0.0f;
    float max_metric = 0.0f;
    size_t rx_size = rx.size();
    if (rx_size < N)
        return 0.0f;
    for (size_t i = 0; i + N <= rx_size; ++i)
    {
        metric = 0.0f;
        for (size_t n = 0; n < L; ++n)
        {
            P += rx[n + i] * std::conj(rx[n + L + i]);
            R += std::norm(rx[n + L + i]);
        }
        metric = std::norm(P) / std::norm(R);
        plato.push_back(metric);
        if (metric > max_metric)
        {
            max_index = i;
            max_metric = metric;
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
    float fs = context.sample_rate;
    int start = data.dsp.max_index + N;
    std::complex<float> corr = 0;



    for (int n = start; n < start + CP; n++)
    {
        if (signal.size() < start + 2 * N + CP)
            break;
        corr += std::conj(signal[n]) * signal[n + N];
    }

    float epsilon = std::arg(corr) / (2 * M_PI);

    float delta_f = epsilon * fs / N;

    data.dsp.cfo = delta_f;

    std::vector<std::complex<float>> corrected = signal;
    for (size_t n = start; n < signal.size(); n++)
    {
        float phase = -2 * M_PIf * delta_f * n / fs;
        corrected[n] *= std::complex<float>(std::cos(phase), std::sin(phase));
    }

    return corrected;
}