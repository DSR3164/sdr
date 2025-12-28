#include <pybind11/pybind11.h>
#include <pybind11/complex.h>
#include <pybind11/numpy.h>
#include <complex>
#include <cstdint>
#include <cmath>
#include <iostream>
#include "pluto_lib.h"

namespace py = pybind11;

py::tuple gardner(py::array_t<std::complex<float>> s, float BnTs, int SPS)
{
    auto buf = s.unchecked<1>();
    ssize_t N = buf.shape(0);

    ssize_t M = N / SPS - 1;
    if (M <= 0)
        return py::make_tuple(py::array_t<std::complex<float>>(), py::array_t<int>());

    ssize_t out_len = M / 2;
    if (out_len <= 0)
        return py::make_tuple(py::array_t<std::complex<float>>(), py::array_t<int>());

    py::array fixed(py::buffer_info(
        nullptr,
        sizeof(std::complex<float>),
        py::format_descriptor<std::complex<float>>::format(),
        1,
        {out_len},
        {sizeof(std::complex<float>)}));

    py::array_t<int> offsets(out_len);

    auto fptr = static_cast<std::complex<float> *>(fixed.mutable_data());
    auto optr = static_cast<int *>(offsets.mutable_data());

    float zeta = std::sqrt(2.0F) / 2.0f;
    float Kp = 1.0f;
    float teta = (BnTs / 10) / (zeta + 1 / (4 * zeta));
    float K1 = (-4 * zeta * teta) / ((1 + 2 * zeta * teta + teta * teta) * Kp);
    float K2 = (-4 * teta * teta) / ((1 + 2 * zeta * teta + teta * teta) * Kp);

    float p2 = 0.0f;
    int offset = 0;

    for (ssize_t i = 0; i < out_len; ++i)
    {
        ssize_t base = static_cast<ssize_t>(SPS) * i;
        ssize_t idx0 = base + offset;
        ssize_t idx1 = base + offset + SPS;
        ssize_t idxm = base + offset + SPS / 2;

        std::complex<float> s1 = buf(idx1);
        std::complex<float> s0 = buf(idx0);
        std::complex<float> sm = buf(idxm);

        float e = (std::real(s1) - std::real(s0)) * std::real(sm) +
                  (std::imag(s1) - std::imag(s0)) * std::imag(sm);

        float p1 = e * K1;
        p2 += p1 + e * K2;
        p2 -= std::floor(p2);

        int new_offset = static_cast<int>(std::round(p2 * SPS));

        offset = new_offset;

        ssize_t read_idx = static_cast<ssize_t>(SPS) * i + offset;
        fptr[i] = buf(read_idx);
        optr[i] = offset;
    }

    return py::make_tuple(fixed, offsets);
}

py::array_t<std::complex<float>> costas_loop(
    py::array_t<std::complex<float>> samples,
    float Ki)
{
    auto in = samples.unchecked<1>();
    ssize_t N = in.shape(0);

    py::array out(py::buffer_info(
        nullptr,
        sizeof(std::complex<float>),
        py::format_descriptor<std::complex<float>>::format(),
        1,
        {N},
        {sizeof(std::complex<float>)}));

    auto fptr = static_cast<std::complex<float> *>(out.mutable_data());
    float theta = 0.0f;
    float freq = 0.0f;
    float Kp = 0.02f;

    py::gil_scoped_release release;

    for (ssize_t n = 0; n < N; n++)
    {
        std::complex<float> rot =
            std::polar(1.0f, -theta);

        auto r = in(n)*rot;
        fptr[n] = r;

        float I = std::real(r);
        float Q = std::imag(r);

        float I_hat = (I >= 0) ? 1.0f : -1.0f;
        float Q_hat = (Q >= 0) ? 1.0f : -1.0f;

        float error = I_hat * Q - Q_hat * I;
        if (error > 1.0f)
            error = 1.0f;
        if (error < -1.0f)
            error = -1.0f;

        freq += Ki * error;
        theta += freq + Kp * error;

        if (theta > M_PI)
            theta -= 2 * M_PI;
        if (theta < -M_PI)
            theta += 2 * M_PI;
    }

    return out;
}

py::array convolve_ones(py::array_t<std::complex<float>, py::array::c_style | py::array::forcecast> x,
                        int SPS)
{
    auto buf = x.unchecked<1>();
    ssize_t N = buf.shape(0);

    ssize_t M = (N > 0 && SPS > 0) ? (N + SPS - 1) : 0;

    py::array y(py::buffer_info(
        nullptr,
        sizeof(std::complex<float>),
        py::format_descriptor<std::complex<float>>::format(),
        1,
        {M},
        {sizeof(std::complex<float>)}));

    auto yptr = static_cast<std::complex<float> *>(y.mutable_data());

    for (ssize_t i = 0; i < M; ++i)
        yptr[i] = {0.0f, 0.0f};

    std::complex<float> acc(0.0f, 0.0f);

    for (ssize_t i = 0; i < M; ++i)
    {
        if (i < N)
            acc += buf(i);
        if (i - SPS >= 0 && i - SPS < N)
            acc -= buf(i - SPS);
        yptr[i] = acc;
    }

    return y;
}

inline std::complex<float> nearest_qam16(const std::complex<float> &r)
{
    // 16-QAM уровни: -3, -1, 1, 3
    auto round_level = [](float x)
    {
        if (x < -2)
            return -3.0f;
        if (x < 0)
            return -1.0f;
        if (x < 2)
            return 1.0f;
        return 3.0f;
    };

    return {round_level(std::real(r)), round_level(std::imag(r))};
}

py::array_t<std::complex<float>> costas_loop_qam16(
    py::array_t<std::complex<float>> samples,
    float Ki)
{
    auto in = samples.unchecked<1>();
    ssize_t N = in.shape(0);

    py::array out(py::buffer_info(
        nullptr,
        sizeof(std::complex<float>),
        py::format_descriptor<std::complex<float>>::format(),
        1,
        {N},
        {sizeof(std::complex<float>)}));

    auto fptr = static_cast<std::complex<float> *>(out.mutable_data());
    float theta = 0.0f;
    float freq = 0.0f;
    float Kp = 0.02f;

    for (ssize_t n = 0; n < N; n++)
    {
        std::complex<float> rot = std::polar(1.0f, -theta);
        auto r = in(n)*rot;
        fptr[n] = r;

        // Decision-directed: ближайший символ 16-QAM
        auto nearest = nearest_qam16(r);

        // Ошибка фазы
        float error = (std::real(r) * std::imag(nearest) - std::imag(r) * std::real(nearest));
        error = std::max(-1.0f, std::min(1.0f, error)); // ограничиваем [-1,1]

        freq += Ki * error;
        theta += freq + Kp * error;

        // Ограничение theta в [-pi, pi]
        if (theta > M_PI)
            theta -= 2 * M_PI;
        if (theta < -M_PI)
            theta += 2 * M_PI;
    }

    return out;
}

inline float phase_detector_qpsk(const std::complex<float> &r)
{
    float I = r.real();
    float Q = r.imag();

    float I_hat = (I >= 0.0f) ? 1.0f : -1.0f;
    float Q_hat = (Q >= 0.0f) ? 1.0f : -1.0f;

    return I_hat * Q - Q_hat * I;
}

py::tuple costas_loop_qpsk(py::array_t<std::complex<float>> samples,
                           float loop_bw)
{
    auto in = samples.unchecked<1>();
    ssize_t N = in.shape(0);

    py::array out(py::buffer_info(
        nullptr,
        sizeof(std::complex<float>),
        py::format_descriptor<std::complex<float>>::format(),
        1,
        {N},
        {sizeof(std::complex<float>)}));
    py::array freq(py::buffer_info(
        nullptr,
        sizeof(float),
        py::format_descriptor<float>::format(),
        1,
        {N},
        {sizeof(float)}));
    py::array phase(py::buffer_info(
        nullptr,
        sizeof(float),
        py::format_descriptor<float>::format(),
        1,
        {N},
        {sizeof(float)}));
    py::array error(py::buffer_info(
        nullptr,
        sizeof(float),
        py::format_descriptor<float>::format(),
        1,
        {N},
        {sizeof(float)}));
    auto optr = static_cast<std::complex<float> *>(out.mutable_data());
    auto fptr = static_cast<float *>(freq.mutable_data());
    auto pptr = static_cast<float *>(phase.mutable_data());
    auto eptr = static_cast<float *>(error.mutable_data());

    float d_phase = 0.0f;
    float d_freq = 0.0f;

    float damping = 0.707f;
    float denom = 1.0f + 2.0f * damping * loop_bw + loop_bw * loop_bw;
    float Kp = (2.0f * damping * loop_bw) / denom;
    float Ki = (loop_bw * loop_bw) / denom;

    for (ssize_t n = 0; n < N; n++)
    {
        // NCO
        std::complex<float> nco = std::polar(1.0f, -d_phase);

        // rotate
        std::complex<float> r = in(n)*nco;
        optr[n] = r;

        // phase detector (QPSK)
        float e = phase_detector_qpsk(r);
        if (e > 1.0f)
            e = 1.0f;
        if (e < -1.0f)
            e = -1.0f;
        eptr[n] = e;

        // loop filter (PI)
        d_freq += Ki * e;
        d_phase += d_freq + Kp * e;

        // wrap phase
        if (d_phase > M_PI)
            d_phase -= 2 * M_PI;
        if (d_phase < -M_PI)
            d_phase += 2 * M_PI;

        fptr[n] = d_freq;
        pptr[n] = d_phase;
    }

    return py::make_tuple(out, freq, phase, error);
}

py::array_t<cp> rrc_mf(
    py::array_t<cp, py::array::c_style | py::array::forcecast> samples,
    double beta, int sps, int span)
{
    auto in = samples.unchecked<1>();
    int N = in.shape(0);

    std::vector<double> h;
    rrc(beta, sps, span, h);
    int L = h.size();

    int N_out = N + L - 1;

    py::array out(py::buffer_info(
        nullptr,
        sizeof(std::complex<float>),
        py::format_descriptor<std::complex<float>>::format(),
        1,
        {N_out},
        {sizeof(std::complex<float>)}));
    auto buf_out = out.request();
    auto out_ptr = static_cast<std::complex<float> *>(out.mutable_data());

    for (int n = 0; n < N_out; ++n)
    {
        cp acc(0.0f, 0.0f);
        for (int k = 0; k < L; ++k)
        {
            int xi = n - k;
            if (xi >= 0 && xi < N)
                acc += in(xi)*cp(static_cast<float>(h[L - 1 - k]), 0.0f);
        }
        out_ptr[n] = acc;
    }

    return out;
}

PYBIND11_MODULE(cpp, m)
{
    m.def("gardner", &gardner, "Gardner timing recovery",
          py::arg("samples"), py::arg("BnTs") = 0.000005f, py::arg("SPS") = 10);
    m.def("costas_loop", &costas_loop, "Costas loop",
          py::arg("samples"), py::arg("Ki") = 0.0001f);
    m.def("convolve_ones", &convolve_ones, "Convolve with ones of lenght SPS",
          py::arg("symbols"), py::arg("SPS") = 10);
    m.def("costas_loop_qam16", &costas_loop_qam16, "Costas loop for 16-QAM",
          py::arg("symbols"), py::arg("Ki") = 0.0001f);
    m.def("costas_loop_qpsk", &costas_loop_qpsk,
          "Costas loop QPSK (like GNU Radio): returns (out, freq, phase, error)");
    m.def("rrc", [](double beta, int sps, int N)
          {std::vector<double> h;rrc(beta, sps, N, h);return h; });
    m.def("rrc_mf", &rrc_mf, "Matched Filter for RRC");
}
