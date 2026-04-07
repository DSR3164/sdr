#include "dsp_module.h"
#include "pluto_lib.h"

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
            if (current_sig_energy < 0) current_sig_energy = 0;
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

    std::vector<std::complex<float>> H_prev(N, { 1,0 });

    for (size_t i = 0; i + N <= temp.size(); i += N)
    {
        std::vector<std::complex<float>> sym(temp.begin() + i,
            temp.begin() + i + N);

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
            if (da > M_PIf) da -= 2 * M_PIf;
            if (da < -M_PIf) da += 2 * M_PIf;

            float m1 = std::abs(H1);
            float m2 = std::abs(H2);

            for (int k = k1 + 1; k < k2; ++k)
            {
                if (is_guard[k]) continue;

                float alpha = float(k - k1) / float(k2 - k1);

                float a = a1 + alpha * da;
                float m = m1 + alpha * (m2 - m1);

                H[k] = std::polar(m, a);
            }
        }

        for (int k = 0; k < pilots.front(); ++k)
            if (!is_guard[k]) H[k] = H[pilots.front()];

        for (int k = pilots.back() + 1; k < N; ++k)
            if (!is_guard[k]) H[k] = H[pilots.back()];

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
        if (count > 0) cpe /= count;

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
    if (rx_size < N) return 0.0f;

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
