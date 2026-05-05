#include "dsp.h"
#include "gui.h"

#include <GL/glew.h>
#include <SDL2/SDL.h>
#include <SoapySDR/Device.hpp>
#include <SoapySDR/Formats.hpp>
#include <algorithm>
#include <backends/imgui_impl_opengl3.h>
#include <backends/imgui_impl_sdl2.h>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fftw3.h>
#include <imgui.h>
#include <implot.h>
#include <thread>
#include <vector>

FFTWPlan &fftw_singleton(size_t size)
{
    static FFTWPlan p{ size };
    return p;
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
};

static inline float hann(int n, int N)
{
    return 0.5f - 0.5f * std::cos(2.0f * (float)M_PI * (float)n / (float)(N - 1));
}

void compute_fftw(const std::vector<std::complex<float>> &iq, std::vector<float> &out_db)
{
    size_t maxim = iq.size();
    std::vector<float> avg(maxim, 0.0f);
    float alpha = 0.1f;
    auto &p = fftw_singleton(maxim);

    out_db.clear();
    out_db.resize(maxim);

    for (size_t i = 0; i < maxim; ++i)
    {
        constexpr float inv = 1.0f / 32768.0f;
        float im = std::real(iq[i]) * inv;
        float re = std::imag(iq[i]) * inv;
        p.in[i][0] = re * p.window[i];
        p.in[i][1] = im * p.window[i];
    }

    fftwf_execute(p.plan);

    constexpr float eps = 1e-12f;
    for (size_t i = 0; i < maxim; ++i)
    {
        int idx = static_cast<int>((i + maxim) / 2) % static_cast<int>(maxim);
        float re = p.out[idx][0];
        float im = p.out[idx][1];
        float pow_ = re * re + im * im + eps;
        avg[i] = (1.0f - alpha) * avg[i] + alpha * pow_;
        out_db[i] = 10.0f * log10(avg[i] + eps);
    }
}

void compute_hz(sdr_config_t &context, std::vector<float> &x_hz)
{
    const double Fs = context.sample_rate;
    for (size_t i = 0; i < x_hz.size(); ++i)
    {
        double f = ((double)i / static_cast<double>(x_hz.size()) - 0.5) * Fs;
        x_hz[i] = (float)f;
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
    static std::vector<std::complex<float>> x(NFFT);
    static std::vector<float> mag_db(NFFT);

    for (int n = 0; n < NFFT; n++)
    {
        constexpr float inv = 1.0f / 32768.0f;
        float I = (float)iq_interleaved[2 * n + 0] * inv;
        float Q = (float)iq_interleaved[2 * n + 1] * inv;
        float w = hann(n, NFFT);
        x[n] = std::complex<float>(I * w, Q * w);
    }

    fft_radix2_inplace(x);

    constexpr float eps = 1e-12f;
    for (int k = 0; k < NFFT; k++)
    {
        uint8_t r, g, b;
        int ks = (k + NFFT / 2) & (NFFT - 1);
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

int context_edit_window(sdr_config_t &context, SharedData_t &data)
{

    static int countdown = 0;
    static int current_uri = -1;
    static int current_mod = context.modulation_type;
    static std::string preview_uri;
    try
    {
        preview_uri = context.args.at("uri");
    }
    catch (const std::exception &e)
    {
        preview_uri = "Click here";
    }

    ImGuiIO &io = ImGui::GetIO();
    static int current_rx_borhwidth = 10;
    static int current_tx_borhwidth = 1;
    static std::vector<float> values = { 0.2e6, 1e6, 2e6, 3e6, 4e6, 5e6, 6e6, 7e6, 8e6, 9e6, 10e6 };
    static SoapySDR::KwargsList list;
    static bool is_scanning = false;
    std::vector<std::string> modulations = { "BPSK", "QPSK", "QAM16", "QAM16 RRC", "OFDM" };
    std::vector<std::string> syncs = { "ZC", "CP", "SC" };
    std::vector<std::string> ofdm_modulations = { "BPSK", "QPSK", "QAM16", "QAM64", "QAM256" };
    static std::string preview_mod = modulations[context.modulation_type];
    static std::string preview_ofdm_mod = "";
    if (data.mod.ModulationType)
        preview_ofdm_mod = ofdm_modulations[data.ofdm_cfg.mod];
    else
        preview_ofdm_mod = ofdm_modulations[2];
    if (ImGui::TreeNode("GUI"))
    {
        if (ImGui::Checkbox("FPS Lock", &data.gui.fps_lock))
            SDL_GL_SetSwapInterval(data.gui.fps_lock);
        ImGui::Checkbox("Can be stopped", &data.gui.can_be_stopped);
        ImGui::Checkbox("Stop", &data.gui.stopped);
        ImGui::TreePop();
        ImGui::Spacing();
    }

    if (ImGui::TreeNode("Debug"))
    {
        ImGui::Text("FPS: %.1f (%.3f ms)", io.Framerate, 1000.0f / io.Framerate);
        ImGui::Text("Frame start: %d", data.dsp.max_index);
        ImGui::Text("CFO: %.1f", data.dsp.cfo);
        ImGui::Text("AD9361: %.2fC", std::stof(context.sdr->readSensor("ad9361-phy_temp0")));
        ImGui::Text("XADC: %.2fC", std::stof(context.sdr->readSensor("xadc_temp0")));
        ImGui::Checkbox("Debug", &data.gui.debug);
        ImGui::TreePop();
        ImGui::Spacing();
    }

    if (ImGui::TreeNode("SDR"))
    {
        bool changed_rx_g = ImGui::SliderFloat("RX Gain", &context.rx_gain, 0.0f, 73.0f, "%.1f");
        bool changed_tx_g = ImGui::SliderFloat("TX Gain", &context.tx_gain, 0.0f, 89.0f, "%.1f");

        bool cragc = ImGui::Checkbox("RX AGC", &context.rx_agc);
        ImGui::SameLine();
        bool ctagc = ImGui::Checkbox("TX AGC", &context.tx_agc);

        if (context.rx_agc)
            context.rx_gain = context.sdr->getGain(SOAPY_SDR_RX, 0);
        if (context.tx_agc)
            context.tx_gain = context.sdr->getGain(SOAPY_SDR_TX, 0);

        bool changed_tx_f = ImGui::InputDouble("TX Frequency", &context.tx_carrier_freq, 10e3, 10e5, "%e");
        bool changed_rx_f = ImGui::InputDouble("RX Frequency", &context.rx_carrier_freq, 10e3, 10e5, "%e");
        if (ImGui::SliderInt("TX Bandwidth", &current_tx_borhwidth, 0, values.size() - 1, std::to_string(values[current_tx_borhwidth]).c_str()))
        {
            context.tx_bandwidth = values[current_tx_borhwidth];
            context.flags |= Flags::APPLY_BANDWIDTH;
        }
        if (ImGui::SliderInt("RX Bandwidth", &current_rx_borhwidth, 0, values.size() - 1, std::to_string(values[current_rx_borhwidth]).c_str()))
        {
            context.rx_bandwidth = values[current_rx_borhwidth];
            context.flags |= Flags::APPLY_BANDWIDTH;
        }

        if (changed_tx_g or changed_rx_g or cragc or ctagc)
            context.flags |= Flags::APPLY_GAIN;

        if (changed_tx_f || changed_rx_f)
            context.flags |= Flags::APPLY_FREQUENCY;

        ImGui::InputDouble("Sample Rate", &context.sample_rate, 0.5e6, 2e6, "%e");
        if (ImGui::BeginCombo("URI", preview_uri.c_str(), ImGuiComboFlags_WidthFitPreview))
        {
            if (!is_scanning)
            {
                is_scanning = true;
                auto scan = std::thread([&context]
                                        {
                            SoapySDR::Kwargs args;
                            args["driver"] = "plutosdr";
                            list = SoapySDR::Device::enumerate(args);
                            is_scanning = false; });

                scan.detach();
            }

            for (size_t i = 0; i < list.size(); ++i)
            {
                bool is_selected = (i == static_cast<size_t>(current_uri));
                if (ImGui::Selectable(list[i].at("uri").c_str(), is_selected))
                {
                    current_uri = i;
                    preview_uri = list[i].at("uri");
                    context.args = list[i];
                    context.flags |= Flags::IS_ACTIVE;
                }
                if (is_selected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        if (ImGui::Button("Reinit"))
        {
            context.flags |= Flags::REINIT;
            data.gui.x_init = false;
        }
        ImGui::TreePop();
        ImGui::Spacing();
    }

    ImGui::SetNextItemOpen(true, ImGuiCond_Once);
    if (ImGui::TreeNode("DSP"))
    {
        if (ImGui::BeginCombo("Modulation", preview_mod.c_str(), ImGuiComboFlags_WidthFitPreview))
        {
            for (size_t i = 0; i < modulations.size(); ++i)
            {
                bool is_selected = (i == static_cast<size_t>(current_mod));
                if (ImGui::Selectable(modulations[i].c_str(), is_selected))
                {
                    current_mod = i;
                    context.modulation_type = i;
                    data.mod.ModulationType = i;
                    preview_mod = modulations[i];
                    context.flags |= Flags::REMODULATION;
                }
                if (is_selected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        bool send_enabled = (context.flags & Flags::SEND) != Flags::None;
        ImGui::SameLine();

        if (ImGui::Checkbox("Send", &send_enabled) or countdown > 0)
        {
            if (context.rx_stream && context.tx_stream && (context.sample_rate > 3e6 || countdown > 0))
            {
                countdown += countdown == 0 ? 200 : -1;

                ImVec2 pos = ImGui::GetItemRectMin();
                pos.y -= 30;
                ImGui::SetNextWindowPos(pos);
                ImGui::Begin("send_warning", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoInputs);

                ImGui::TextColored(ImVec4(1, 1, 0, 1), "Warning: cannot stream with that sample rate!");
                ImGui::End();
            }
            else
            {
                if (send_enabled)
                    context.flags |= Flags::SEND;
                else
                    context.flags &= ~Flags::SEND;
            }
        }

        if (context.modulation_type == 4)
        {
            if (ImGui::BeginCombo("OFDM Modulation", preview_ofdm_mod.c_str(), ImGuiComboFlags_WidthFitPreview))
            {
                for (size_t i = 0; i < ofdm_modulations.size(); ++i)
                {
                    bool is_selected = (i == static_cast<size_t>(data.ofdm_cfg.mod));
                    if (ImGui::Selectable(ofdm_modulations[i].c_str(), is_selected))
                    {
                        data.ofdm_cfg.mod = i;
                        preview_ofdm_mod = ofdm_modulations[i];
                        context.flags |= Flags::REMODULATION;
                    }
                    if (is_selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            if (ImGui::BeginCombo("SYN", syncs[data.dsp.sync].c_str(), ImGuiComboFlags_WidthFitPreview))
            {
                for (size_t i = 0; i < syncs.size(); ++i)
                {
                    bool is_selected = (i == static_cast<size_t>(data.dsp.sync));
                    if (ImGui::Selectable(syncs[i].c_str(), is_selected))
                        data.dsp.sync = i;
                    if (is_selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            ImGui::Checkbox("PSS", &data.ofdm_cfg.pss);
            ImGui::SameLine();
            ImGui::Checkbox("CFO", &data.ofdm_cfg.cfo);
            ImGui::Checkbox("FFT", &data.ofdm_cfg.fft);
            ImGui::SameLine();
            ImGui::Checkbox("Equa", &data.ofdm_cfg.eq);
            ImGui::Text("OFDM subcarriers");
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::SliderInt("##OFDM subcarriers", &data.ofdm_cfg.n_subcarriers, 4, std::round(context.sample_rate / 15e3)))
                context.flags |= Flags::REMODULATION;
            ImGui::Text("CP lenght");
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::SliderInt("##OFDM CP len", &data.ofdm_cfg.n_cp, 4, 64))
                context.flags |= Flags::REMODULATION;
            ImGui::Text("Pilots spacing");
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::SliderInt("##OFDM Pilot Spacing", &data.ofdm_cfg.pilot_spacing, 2, std::round(context.sample_rate / 15e3) - 3))
                context.flags |= Flags::REMODULATION;
            ImGui::Text("Frame start offset");
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::InputInt("##OFDM Symbol Offset", &data.dsp.offset, 1, -1);
        }
        else
        {
            ImGui::InputDouble("Gardner", &data.dsp.gardner_band, 1e-6, 1, "%e");
            ImGui::InputDouble("Costas", &data.dsp.costas_band, 1e-6, 1, "%e");
            ImGui::SliderFloat("Coefficient", &data.dsp.scale_coef, 0.0f, 2000.0f, "%.3f");
        }

        ImGui::TreePop();
        ImGui::Spacing();
    }

    return 1;
}

void run_gui(sdr_config_t &context, SharedData_t &data)
{
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER);
    SDL_Window *window = SDL_CreateWindow(
        "ImGUI RF", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        1520, 720, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE
    );
    SDL_GLContext gl_context = SDL_GL_CreateContext(window);
    ImGui::CreateContext();
    ImPlot::CreateContext();
    SDL_GL_SetSwapInterval(1);

    ImGui_ImplSDL2_InitForOpenGL(window, gl_context);
    ImGui_ImplOpenGL3_Init("#version 330");

    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    bool running = true;
    static GLuint wfTex = 0;
    static int wfHead = 0;

    static std::vector<uint8_t> wfRow(NFFT * 3);
    static std::vector<float> x_hz(NFFTW);
    static std::vector<float> spec_smooth(NFFT, -120.0f);
    static const float alpha = 0.15f;
    std::vector<std::complex<float>> raw(context.buffer_size);
    std::vector<std::complex<float>> ofdm(context.buffer_size);
    data.history.sdrtime.reserve(4000);
    data.history.dsptime.reserve(4000);

    GLuint texture;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    while (running)
    {
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT)
                running = false;
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();
        ImGui::DockSpaceOverViewport(0, nullptr, ImGuiDockNodeFlags_None);

        { //  Demos
            ImGui::ShowDemoWindow();
            ImPlot::ShowDemoWindow();
        }

        { // Main window

            raw = data.mod.raw;
            std::vector<float> fft_vec;

            compute_fftw(raw, fft_vec);

            ImGui::Begin("Settings");
            context_edit_window(context, data);
            ImGui::End();

            if (data.mod.ModulationType == 4)
            {

                data.gui_buff.read(ofdm);

                ImGui::Begin("OFDM Constellation");
                if (ImPlot::BeginPlot("Raw", ImGui::GetContentRegionAvail()))
                {
                    ImPlot::SetNextMarkerStyle(ImPlotMarker_Square, 2.0f);
                    ImPlot::SetupAxesLimits(-200, 200, -200, 200, ImPlotCond_Once);
                    static bool reset_view = false;
                    if (reset_view)
                    {
                        ImPlot::SetupAxesLimits(-2048, 2048, -2048, 2048, ImPlotCond_Always);
                        reset_view = false;
                    }

                    if (ImPlot::BeginLegendPopup("Const"))
                    {
                        if (ImGui::Button("Reset view"))
                            reset_view = true;

                        ImPlot::EndLegendPopup();
                    }
                    ImPlot::PlotScatter(
                        "Const",
                        reinterpret_cast<const float *>(ofdm.data()),
                        reinterpret_cast<const float *>(ofdm.data()) + 1,
                        data.ofdm_cfg.n_subcarriers * 10,
                        0,
                        0,
                        sizeof(std::complex<float>)
                    );

                    ImPlot::EndPlot();
                }
                ImGui::End();

                ImGui::Begin("OFDM");
                if (ImPlot::BeginPlot("I:Q", ImGui::GetContentRegionAvail()))
                {
                    ImPlot::PlotLine("I", reinterpret_cast<const float *>(ofdm.data()), data.ofdm_cfg.n_subcarriers * 10, 1.0, 0.0, 0, 0, sizeof(std::complex<float>));
                    ImPlot::PlotLine("Q", reinterpret_cast<const float *>(ofdm.data()) + 1, data.ofdm_cfg.n_subcarriers * 10, 1.0, 0.0, 0, 0, sizeof(std::complex<float>));
                    ImPlot::EndPlot();
                }
                ImGui::End();
            }

            ImGui::Begin("Constellation");

            if (ImPlot::BeginPlot("Raw", ImGui::GetContentRegionAvail()))
            {
                ImPlot::SetNextMarkerStyle(ImPlotMarker_Square, 2.0f);
                ImPlot::SetupAxesLimits(-200, 200, -200, 200, ImPlotCond_Once);
                static bool reset_view = false;
                if (reset_view)
                {
                    ImPlot::SetupAxesLimits(-2048, 2048, -2048, 2048, ImPlotCond_Always);
                    reset_view = false;
                }

                if (ImPlot::BeginLegendPopup("Const"))
                {
                    if (ImGui::Button("Reset view"))
                        reset_view = true;

                    ImPlot::EndLegendPopup();
                }
                ImPlot::PlotScatter(
                    "Const",
                    reinterpret_cast<const float *>(raw.data()),
                    reinterpret_cast<const float *>(raw.data()) + 1,
                    raw.size(), 0, 0,
                    sizeof(std::complex<float>)
                );

                ImPlot::EndPlot();
            }
            ImGui::End();

            ImGui::Begin("Time domain raw");
            if (ImPlot::BeginPlot("I:Q", ImGui::GetContentRegionAvail()))
            {
                ImPlot::PlotLine("I", reinterpret_cast<const float *>(raw.data()), raw.size(), 1.0, 0.0, 0, 0, sizeof(std::complex<float>));
                ImPlot::PlotLine("Q", reinterpret_cast<const float *>(raw.data()) + 1, raw.size(), 1.0, 0.0, 0, 0, sizeof(std::complex<float>));
                ImPlot::EndPlot();
            }
            ImGui::End();

            ImGui::Begin("Time domain sync");
            if (ImPlot::BeginPlot("I:Q", ImGui::GetContentRegionAvail()))
            {
                ImPlot::PlotLine("Met", data.gui.metrics.data(), data.gui.metrics.size());
                ImPlot::EndPlot();
            }
            ImGui::End();

            ImGui::Begin("Plato");
            if (ImPlot::BeginPlot("Corr", ImGui::GetContentRegionAvail()))
            {
                ImPlot::PlotLine("Corr", data.gui.plato.data(), data.gui.plato.size());
                ImPlot::EndPlot();
            }
            ImGui::End();

            ImGui::Begin("history");
            if (ImPlot::BeginPlot("I:Q", ImGui::GetContentRegionAvail()))
            {
                ImPlot::PlotLine("I", reinterpret_cast<const float *>(data.history.receive.data()), data.history.receive.size(), 1.0, 0, 0, 0, sizeof(std::complex<float>));
                ImPlot::PlotLine("Q", reinterpret_cast<const float *>(data.history.receive.data()) + 1, data.history.receive.size(), 1.0, 0, 0, 0, sizeof(std::complex<float>));

                ImPlot::EndPlot();
            }
            ImGui::End();

            ImGui::Begin("OFDM Grid");
            {
                std::vector<bool> is_pilot(data.ofdm_cfg.n_subcarriers);
                std::vector<bool> is_guard(data.ofdm_cfg.n_subcarriers);
                std::vector<int> pilots(data.ofdm_cfg.n_subcarriers);
                std::vector<int> datas(data.ofdm_cfg.n_subcarriers);

                calculate_pilots_and_guard(data.ofdm_cfg, pilots, datas, is_pilot, is_guard);

                static int N = data.ofdm_cfg.n_subcarriers;
                std::vector<float> grid(N);
                for (int k = 0; k < N; ++k)
                {
                    if (is_guard[k])
                        grid[k] = 0.0f;
                    else if (is_pilot[k])
                        grid[k] = 1.0f;
                    else
                        grid[k] = 0.5f;
                }
                std::rotate(grid.begin(), grid.begin() + N / 2, grid.end());
                if (ImPlot::BeginPlot("OFDM Grid", ImGui::GetContentRegionAvail()))
                {
                    ImPlot::SetupAxes("Subcarrier", "", ImPlotAxisFlags_None, ImPlotAxisFlags_NoDecorations);
                    ImPlot::SetupAxisLimits(ImAxis_X1, -N / 2 - 0.5, N / 2 - 0.5, ImGuiCond_Always);
                    ImPlot::SetupAxisLimits(ImAxis_Y1, 0, 1, ImGuiCond_Always);
                    const char *labels[3] = { "Guard", "Data", "Pilot" };
                    static ImVec4 colors[] = {
                        ImVec4(0.1f, 0.1f, 0.1f, 1.0f), // Guard - тёмный
                        ImVec4(0.2f, 0.6f, 1.0f, 1.0f), // Data - синий
                        ImVec4(1.0f, 0.5f, 0.0f, 1.0f), // Pilot - оранжевый
                    };
                    static ImPlotColormap cmap = ImPlot::AddColormap("OFDM", colors, 3);

                    ImPlot::PushColormap(cmap);
                    ImPlot::PlotHeatmap("##grid", grid.data(), 1, N, 0, 1, nullptr, ImPlotPoint(-N / 2 - 0.5, 0), ImPlotPoint(N / 2 - 0.5, 1));

                    std::vector<int> counts = {
                        (int)(N - datas.size() - pilots.size()),
                        (int)datas.size(),
                        (int)pilots.size()
                    };

                    for (int i = 0; i < 3; ++i)
                    {
                        char label[32];
                        snprintf(label, sizeof(label), "%s %d", labels[i], counts[i]);
                        ImPlot::PushStyleColor(ImPlotCol_Line, colors[i]);
                        ImPlot::PlotDummy(label);
                        ImPlot::PopStyleColor();
                    }
                    ImPlot::PopColormap();

                    if (ImPlot::IsPlotHovered())
                    {
                        ImPlotPoint mp = ImPlot::GetPlotMousePos(ImAxis_X1, ImAxis_Y1);
                        int screen_pos = static_cast<int>(std::round(mp.x));
                        int k = (screen_pos + N) % N;
                        if (k >= 0 && k < N)
                        {
                            const char *type = is_guard[k] ? "Guard" : is_pilot[k] ? "Pilot"
                                                                                   : "Data";
                            ImGui::BeginTooltip();
                            ImGui::Text("k=%d  %s", k, type);
                            ImGui::EndTooltip();
                        }
                    }
                    ImPlot::EndPlot();
                }
            }
            ImGui::End();

            if (data.gui.debug)
            {
                ImGui::Begin("Channel estimation");
                {
                    if (ImPlot::BeginPlot("Channel est", ImGui::GetContentRegionAvail()))
                    {
                        std::vector<float> abs;
                        for (auto &x : data.gui.estimation)
                            abs.push_back(std::abs(x));
                        if (!data.gui.estimation.empty())
                        {
                            ImPlot::PlotLine("I", reinterpret_cast<const float *>(data.gui.estimation.data()), 1.0, 0, 0, sizeof(std::complex<float>));

                            ImPlot::PlotLine("Q", reinterpret_cast<const float *>(data.gui.estimation.data()) + 1, 1.0, 0, 0, sizeof(std::complex<float>));
                        }

                        ImPlot::EndPlot();
                    }

                    ImGui::End();
                    ImGui::Begin("Compare");
                    if (ImPlot::BeginPlot("Bits", ImGui::GetContentRegionAvail()))
                    {
                        ImPlot::PlotLine("Bits TX", data.dsp.bits_tx.data(), data.dsp.bits_tx.size());
                        ImPlot::PlotLine("Bits RX", data.dsp.bits_rx.data(), data.dsp.bits_rx.size());
                        ImPlot::EndPlot();
                    }
                }
                ImGui::End();
                ImGui::Begin("SDR Time");
                if (ImPlot::BeginPlot("Time", ImGui::GetContentRegionAvail()))
                {
                    ImPlot::PlotLine("Time", data.history.sdrtime.data(), data.history.sdrtime.size());
                    ImPlot::EndPlot();
                }
                ImGui::End();
                ImGui::Begin("DSP Time");
                if (ImPlot::BeginPlot("Time", ImGui::GetContentRegionAvail()))
                {
                    ImPlot::PlotLine("Time", data.history.dsptime.data(), data.history.dsptime.size());
                    ImPlot::EndPlot();
                }
                ImGui::End();
                ImGui::Begin("Time domain Send History");
                if (ImPlot::BeginPlot("Send History", ImGui::GetContentRegionAvail()))
                {
                    ImPlot::SetupAxesLimits(0, 1920 * 4, -10, 4000, ImPlotCond_Once);
                    ImPlot::PlotLine("Met", data.history.send.data(), data.history.send.size());
                    ImPlot::EndPlot();
                }
                ImGui::End();
            }

            if (context.modulation_type != 4)
            {
                ImGui::Begin("Sync");

                if (ImPlot::BeginPlot("Sync", ImGui::GetContentRegionAvail()))
                {
                    ImPlot::SetNextMarkerStyle(ImPlotMarker_Square, 2.0f);
                    ImPlot::SetupAxesLimits(-200, 200, -200, 200, ImPlotCond_Once);
                    static bool reset_view = false;
                    if (reset_view)
                    {
                        ImPlot::SetupAxesLimits(-2048, 2048, -2048, 2048, ImPlotCond_Always);
                        reset_view = false;
                    }

                    if (ImPlot::BeginLegendPopup("Const"))
                    {
                        if (ImGui::Button("Reset view"))
                            reset_view = true;
                        ImPlot::EndLegendPopup();
                    }
                    ImPlot::PlotScatter(
                        "Const",
                        reinterpret_cast<const float *>(data.mod.sync.data()),
                        reinterpret_cast<const float *>(data.mod.sync.data()) + 1,
                        data.mod.sync.size(), 0, 0,
                        sizeof(std::complex<float>)
                    );
                    ImPlot::EndPlot();
                }
                ImGui::End();

                ImGui::Begin("Conv");

                if (ImPlot::BeginPlot("Conv", ImGui::GetContentRegionAvail()))
                {
                    ImPlot::SetNextMarkerStyle(ImPlotMarker_Square, 2.0f);
                    ImPlot::SetupAxesLimits(-200, 200, -200, 200, ImPlotCond_Once);
                    static bool reset_view = false;
                    if (reset_view)
                    {
                        ImPlot::SetupAxesLimits(-2048, 2048, -2048, 2048, ImPlotCond_Always);
                        reset_view = false;
                    }

                    if (ImPlot::BeginLegendPopup("Const"))
                    {
                        if (ImGui::Button("Reset view"))
                            reset_view = true;
                        ImPlot::EndLegendPopup();
                    }
                    ImPlot::PlotScatter(
                        "Const",
                        reinterpret_cast<const float *>(data.mod.conv.data()),
                        reinterpret_cast<const float *>(data.mod.conv.data()) + 1,
                        data.mod.conv.size(), 0, 0,
                        sizeof(std::complex<float>)
                    );
                    ImPlot::EndPlot();
                }
                ImGui::End();

                ImGui::Begin("Gardner");

                if (ImPlot::BeginPlot("just", ImGui::GetContentRegionAvail()))
                {
                    ImPlot::SetNextMarkerStyle(ImPlotMarker_Square, 2.0f);
                    ImPlot::SetupAxesLimits(-200, 200, -200, 200, ImPlotCond_Once);
                    static bool reset_view = false;
                    if (reset_view)
                    {
                        ImPlot::SetupAxesLimits(-2048, 2048, -2048, 2048, ImPlotCond_Always);
                        reset_view = false;
                    }

                    if (ImPlot::BeginLegendPopup("Const"))
                    {
                        if (ImGui::Button("Reset view"))
                            reset_view = true;
                        ImPlot::EndLegendPopup();
                    }
                    ImPlot::PlotScatter(
                        "Const",
                        reinterpret_cast<const float *>(data.mod.demodul.data()),
                        reinterpret_cast<const float *>(data.mod.demodul.data()) + 1,
                        data.mod.demodul.size(), 0, 0,
                        sizeof(std::complex<float>)
                    );
                    ImPlot::EndPlot();
                }
                ImGui::End();
            }

            ImGui::Begin("Waterfall");

            if (wfTex == 0)
            {
                glGenTextures(1, &wfTex);
                glBindTexture(GL_TEXTURE_2D, wfTex);

                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, NFFT, WF_H, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);

                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            }
            if (!data.gui.x_init)
            {
                x_hz.resize(context.buffer_size);
                compute_hz(context, x_hz);
                data.gui.x_init = true;
            }

            if (!data.gui.stopped)
            {
                static double last_wf = 0.0;
                double now = ImGui::GetTime();
                if (now - last_wf > (1.0 / 60.0))
                {
                    static std::vector<int16_t> iqpad(NFFT * 2, 0);
                    int avail_complex = std::min((int)raw.size(), 1920);
                    for (int n = 0; n < NFFT; n++)
                    {
                        if (n < avail_complex)
                        {
                            iqpad[2 * n + 0] = std::real(raw[n]);
                            iqpad[2 * n + 1] = std::imag(raw[n]);
                        }
                        else
                        {
                            iqpad[2 * n + 0] = 0;
                            iqpad[2 * n + 1] = 0;
                        }
                    }
                    compute_wf_row_u8(iqpad.data(), wfRow.data());

                    glBindTexture(GL_TEXTURE_2D, wfTex);
                    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, wfHead, NFFT, 1, GL_RGB, GL_UNSIGNED_BYTE, wfRow.data());
                    wfHead = (wfHead + 1) % WF_H;

                    last_wf = now;
                }
            }

            ImVec2 avail = ImGui::GetContentRegionAvail();
            if (avail.x < 50)
                avail.x = 50;
            if (avail.y < 50)
                avail.y = 50;

            ImDrawList *dl = ImGui::GetWindowDrawList();
            ImVec2 p0 = ImGui::GetCursorScreenPos();
            ImVec2 p1 = ImVec2(p0.x + avail.x, p0.y + avail.y);

            ImTextureID tid = (ImTextureID)(intptr_t)wfTex;
            float split = (float)wfHead / (float)WF_H; // 0..1
            float hA = (1.0f - split) * avail.y;

            if (hA > 0.5f)
            {
                ImVec2 a0 = p0;
                ImVec2 a1 = ImVec2(p1.x, p0.y + hA);
                ImVec2 uv0 = ImVec2(0.0f, split);
                ImVec2 uv1 = ImVec2(1.0f, 1.0f);
                dl->AddImage(tid, a0, a1, uv0, uv1);
            }
            float hB = split * avail.y;
            if (hB > 0.5f)
            {
                ImVec2 b0 = ImVec2(p0.x, p0.y + hA);
                ImVec2 b1 = p1;
                ImVec2 uv0 = ImVec2(0.0f, 0.0f);
                ImVec2 uv1 = ImVec2(1.0f, split);
                dl->AddImage(tid, b0, b1, uv0, uv1);
            }

            ImGui::End();

            ImGui::Begin("Spectrum");
            spec_smooth.resize(fft_vec.size());
            for (size_t i = 0; i < fft_vec.size(); ++i)
                spec_smooth[i] = alpha * fft_vec[i] + (1.0f - alpha) * spec_smooth[i];

            ImVec2 sz = ImGui::GetContentRegionAvail();

            if (ImPlot::BeginPlot("FFT", sz))
            {

                ImPlot::SetupAxisLimits(ImAxis_X1, context.sample_rate / -2, context.sample_rate / 2, ImGuiCond_Once);
                ImPlot::SetupAxisLimits(ImAxis_Y1, -80.0, 30.0, ImGuiCond_Once);
                ImPlot::SetupAxis(ImAxis_Y1, "", ImPlotAxisFlags_NoTickLabels | ImPlotAxisFlags_NoTickMarks);

                ImPlot::PlotLine("Spectrum", x_hz.data(), spec_smooth.data(), fft_vec.size());
                if (ImPlot::IsPlotHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                {
                    ImPlotPoint mp = ImPlot::GetPlotMousePos(ImAxis_X1, ImAxis_Y1);
                    double f_click = mp.x;
                    context.rx_carrier_freq = context.rx_carrier_freq + f_click;
                    context.flags |= Flags::APPLY_FREQUENCY;
                }

                ImPlot::EndPlot();
            }
            ImGui::End();
        }
        ImGui::Render();

        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);
    }

    context.flags = Flags::EXIT;

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();
    SDL_GL_DeleteContext(gl_context);
    SDL_DestroyWindow(window);
    SDL_Quit();
}