#include <GL/glew.h>
#include <SDL2/SDL.h>

#include <complex>
#include <cstddef>
#include <cstdint>
#include <atomic>
#include <algorithm>
#include <iostream>
#include <thread>

#include "backends/imgui_impl_opengl3.h"
#include "backends/imgui_impl_sdl2.h"
#include "imgui.h"
#include "implot.h"
#include "fftw3.h"
#include "gui.h"
#include <dsp_module.h>
#include <SoapySDR/Device.hpp>
#include <vector>

static double gardner_bor = 1;
static double costas_bor = 15e-4;
static float coef = 1.0;
static bool flag_barker = false;
static bool changed = false;
static bool stopped = false;
static bool can_be_stopped = false;
static float threshold = 2.0f;
static float timed = 1.0f;
static int countdown = 0;
static int current_ofdm_mod = 0;
static int ofdm_mod = 0;
std::vector<float> time_history;

namespace
{
    static GLuint wfTex = 0;
    static bool x_init = false;
    static int wfHead = 0;

    static std::vector<uint8_t> wfRow(gui::NFFT * 3);
    static std::vector<float> x_hz(gui::NFFTW);
    static std::vector<float> spec_smooth(gui::NFFT, -120.0f);
    static const float alpha = 0.15f;
    static std::vector<float> metrics(1920, 0);
    static std::vector<int> send_history;
    static std::vector<float> receive_historyi;
    static std::vector<float> receive_historyq;
    std::vector<std::vector<float>> waterfall;
    static std::vector<float> plato;
    static int offset = 0;
    static bool fps_lock = 1;
    int max_index = 0;
    float cfo = 0.0f;

}
enum class ModulationType {
    BPSK,
    QPSK,
    QAM16,
    OFDM,
};

enum class SDFlags : uint16_t
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


void ofdm_equalize(std::vector<std::complex<float>> &signal, int N, int ps)
{
    const int DC = N / 2;
    const std::complex<float> known_pilot = { 1.0f, 0.0f };

    std::vector<int> pilots;
    std::vector<bool> is_pilot(N, false);

    int counter = 0;
    for (int k = 1; k < N; ++k)
    {
        if (k == DC) continue;

        if (counter % ps == 0)
        {
            pilots.push_back(k);
            is_pilot[k] = true;
        }
        counter++;
    }
    is_pilot[DC] = true;

    std::vector<std::complex<float>> H_prev(N, { 1,0 });
    const float beta = 0.8f;   // коэффициент сглаживания

    for (size_t i = 0; i + N <= signal.size(); i += N)
    {
        std::vector<std::complex<float>> sym(signal.begin() + i,
            signal.begin() + i + N);

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

        for (int k = 0; k < N; ++k)
        {
            if (k == DC)
                continue;

            if (std::abs(H[k]) > 1e-12f)
                equalized[k] = sym[k] / H[k];
            else
                equalized[k] = sym[k];
        }

        float phase = 0;
        for (auto k : pilots)
            phase += std::arg(equalized[k] / known_pilot);

        phase /= pilots.size();

        std::complex<float> rot = std::exp(std::complex<float>(0, -phase));

        for (int k = 0; k < N; ++k)
            if (k != DC)
                equalized[k] *= rot;

        for (int k = 0; k < N; ++k)
        {
            // if (k == DC) continue;
            if (!is_pilot[k])
                signal[k + i] = equalized[k];
            else
                signal[k + i] = std::complex<float>(0.0f, 0.0f);
        }
    }

}

float estimate_cfo(const std::vector<std::complex<float>> &rx, int N, int max_index, float Fs)
{
    int L = N / 2;
    std::complex<float> P_cfo(0.0f, 0.0f);

    // считаем сумму произведений первой и второй половины преамбулы
    for (int n = 0; n < L; ++n)
        P_cfo += rx[max_index + n] * std::conj(rx[max_index + n + L]);

    float Ts = 1.0f / Fs;                      // период дискретизации
    float cfo = std::arg(P_cfo) / (2.0f * M_PI * L * Ts);

    return cfo;  // CFO в герцах
}

float schmidl_cox_detect(const std::vector<std::complex<float>> &rx, int N, float &cfo_est, int &max_index)
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
            P += rx[n + i] * rx[n + L + i];
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

std::vector<float> minn_metric(const std::vector<std::complex<float>> &rx, int N, int &index)
{
    plato.resize(rx.size(), 0);
    int L = N / 4;
    float last_metric = .0f;
    std::complex<float> p = { 0,0 };
    float R = 0;

    for (size_t i = 0; i < rx.size() - N; ++i)
    {
        for (size_t m = 0; m < 2; ++m)
        {
            for (size_t k = 0; k < N / 4 - 1; ++k)
            {
                p = rx[i + N / 2 * m * k]
                    * rx[i + m * N / 2 + k + N / 4];
                R = std::norm(p);
            }

            plato[i] = std::norm(p) / std::norm(R);

            if (last_metric < plato[i])
            {
                index = i;
                last_metric = plato[i];
            }
        }
    }
    return plato;
}

int context_edit_window(sdr_config_t &context, SharedData_t &data)
{
    static int current_uri = -1;
    static int current_mod = 1;
    static std::string preview_uri;
    try
    {
        preview_uri = context.args.at("uri");
    }
    catch (const std::exception &e)
    {
        preview_uri = "none";
    }

    static std::string preview_mod = "QPSK";
    static std::string preview_ofdm_mod = "QAM16";
    static int current_rx_borhwidth = 10;
    static int current_tx_borhwidth = 1;
    static std::vector<float> values = { 0.2e6, 1e6, 2e6, 3e6, 4e6, 5e6, 6e6, 7e6, 8e6, 9e6, 10e6 };
    static SoapySDR::KwargsList list;
    static bool is_scanning = false;
    std::vector<std::string> modulations = { "BPSK", "QPSK", "QAM16", "QAM16 RRC", "OFDM" };
    std::vector<std::string> ofdm_modulations = { "BPSK", "QPSK", "QAM16" };
    if (ImGui::Checkbox("FPS Lock", &fps_lock))
        SDL_GL_SetSwapInterval(fps_lock);

    bool changed_tx_g = ImGui::SliderFloat("TX Gain", &context.tx_gain, 0.0f, 89.0f, "%.3f");
    bool changed_rx_g = ImGui::SliderFloat("RX Gain", &context.rx_gain, 0.0f, 73.0f, "%.3f");
    bool changed_tx_f = ImGui::InputDouble("TX Frequency", &context.tx_carrier_freq, 10e3, 10e5, "%e");
    bool changed_rx_f = ImGui::InputDouble("RX Frequency", &context.rx_carrier_freq, 10e3, 10e5, "%e");
    ImGui::SliderFloat("Coefficient", &coef, 0.0f, 2000.0f, "%.3f");
    ImGui::InputDouble("Gardner", &gardner_bor, 1e-6, 1, "%e");
    ImGui::InputDouble("Costas", &costas_bor, 1e-6, 1, "%e");
    if (ImGui::SliderInt("TX Borwidth", &current_tx_borhwidth, 0, values.size() - 1, std::to_string(values[current_tx_borhwidth]).c_str()))
    {
        context.tx_bandwidth = values[current_tx_borhwidth];
        context.flags |= Flags::APPLY_BANDWIDTH;
    }
    if (ImGui::SliderInt("RX Borwidth", &current_rx_borhwidth, 0, values.size() - 1, std::to_string(values[current_rx_borhwidth]).c_str()))
    {
        context.rx_bandwidth = values[current_rx_borhwidth];
        context.flags |= Flags::APPLY_BANDWIDTH;
    }

    if (changed_tx_g || changed_rx_g)
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
                    is_scanning = false;
                });

            scan.detach();
        }

        for (int i = 0; i < (int)list.size(); ++i)
        {
            bool is_selected = (i == current_uri);
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
    if (ImGui::BeginCombo("Modulation", preview_mod.c_str(), ImGuiComboFlags_WidthFitPreview))
    {
        for (int i = 0; i < (int)modulations.size(); ++i)
        {
            bool is_selected = (i == current_mod);
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
    if (context.modulation_type == 4)
    {
        if (ImGui::BeginCombo("OFDM Modulation", preview_ofdm_mod.c_str(), ImGuiComboFlags_WidthFitPreview))
        {
            for (int i = 0; i < (int)ofdm_modulations.size(); ++i)
            {
                bool is_selected = (i == current_ofdm_mod);
                if (ImGui::Selectable(ofdm_modulations[i].c_str(), is_selected))
                {
                    current_ofdm_mod = i;
                    ofdm_mod = i;
                    preview_ofdm_mod = ofdm_modulations[i];
                    context.flags |= Flags::REMODULATION;
                }
                if (is_selected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        ImGui::Checkbox("CFO Correction", &data.mod.cfo);
        if (ImGui::SliderInt("OFDM subcarriers", &data.mod.n, 4, std::round(context.sample_rate / 15e3)))
            context.flags |= Flags::REMODULATION;
        if (ImGui::SliderInt("OFDM CP len", &data.mod.ncp, 4, 64))
            context.flags |= Flags::REMODULATION;
        if (ImGui::SliderInt("OFDM Pilot Spacing", &data.mod.ps, 2, std::round(context.sample_rate / 15e3) - 3))
            context.flags |= Flags::REMODULATION;
        ImGui::InputInt("OFDM Symbol Offset", &offset, 1, -1);
    }

    bool send_enabled = (context.flags & Flags::SEND) != Flags::None;
    ImGui::Checkbox("Barker", &flag_barker);
    ImGui::SameLine();

    if (ImGui::Checkbox("Send", &send_enabled) or countdown > 0)
    {
        if (context.rx_stream && context.tx_stream && (context.sample_rate > 3e6 || countdown > 0))
        {
            countdown += countdown == 0 ? 200 : -1;

            // Принудительный popup над чекбоксом
            ImVec2 pos = ImGui::GetItemRectMin();
            pos.y -= 30; // чуть выше
            ImGui::SetNextWindowPos(pos);
            ImGui::Begin("send_warning", nullptr,
                ImGuiWindowFlags_NoTitleBar |
                ImGuiWindowFlags_AlwaysAutoResize |
                ImGuiWindowFlags_NoMove |
                ImGuiWindowFlags_NoSavedSettings |
                ImGuiWindowFlags_NoInputs);

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

    ImGui::SameLine();
    ImGui::Checkbox("Stop", &stopped);
    ImGui::Checkbox("Can be stopped", &can_be_stopped);
    if (ImGui::Button("Reinit"))
    {
        context.flags |= Flags::REINIT;
        x_init = false;
    }

    ImGui::SameLine();
    ImGui::Text("+-1000Hz");
    ImGui::SameLine();

    float spacing = ImGui::GetStyle().ItemInnerSpacing.x;
    if (ImGui::ArrowButton("##left", ImGuiDir_Left))
    {
        context.tx_carrier_freq -= 1e3;
        context.rx_carrier_freq -= 1e3;
        context.flags |= Flags::APPLY_FREQUENCY;
    }
    ImGui::SameLine(0.0f, spacing);
    if (ImGui::ArrowButton("##right", ImGuiDir_Right))
    {
        context.tx_carrier_freq += 1e3;
        context.rx_carrier_freq += 1e3;
        context.flags |= Flags::APPLY_FREQUENCY;
    }
    return 1;
}

void run_gui(sdr_config_t &context, SharedData_t &data)
{
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER);
    SDL_Window *window = SDL_CreateWindow(
        "ImGUI RF", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        1520, 720, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
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
    std::vector<std::complex<float>> raw(context.buffer_size);
    std::vector<std::complex<float>> ofdm(context.buffer_size);
    time_history.reserve(4000);

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
            ofdm = data.mod.ofdm;

            ImGui::Begin("Settings");
            context_edit_window(context, data);
            ImGui::End();

            if (data.mod.ModulationType == 4)
            {
                ImGui::Begin("OFDM Constellation");
                if (ImPlot::BeginPlot("Raw", ImGui::GetContentRegionAvail()))
                {
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
                        reinterpret_cast<const float *>(ofdm.data()),        // X = I
                        reinterpret_cast<const float *>(ofdm.data()) + 1,    // Y = Q
                        ofdm.size(),
                        0,                                                // flags
                        0,                                                // offset
                        sizeof(std::complex<float>)                       // stride
                    );

                    ImPlot::EndPlot();
                }
                ImGui::End();

                ImGui::Begin("OFDM");
                if (ImPlot::BeginPlot("I:Q", ImGui::GetContentRegionAvail()))
                {
                    ImPlot::PlotLine("I",
                        reinterpret_cast<const float *>(ofdm.data()),
                        ofdm.size(),
                        1.0, 0.0, 0, 0,
                        sizeof(std::complex<float>));
                    ImPlot::PlotLine("Q",
                        reinterpret_cast<const float *>(ofdm.data()) + 1,
                        ofdm.size(),
                        1.0, 0.0, 0, 0,
                        sizeof(std::complex<float>));
                    ImPlot::EndPlot();
                }
                ImGui::End();
            }

            ImGui::Begin("Constellation");
            ImGui::Text("SDR Cycle: %.f", timed);
            ImGui::Text("FPS: %.1f (%.3f ms)", io.Framerate, 1000.0f / io.Framerate);
            ImGui::Text("Index %d", max_index);
            ImGui::Text("raw size = %zu", raw.size());
            if (ImPlot::BeginPlot("Raw", ImGui::GetContentRegionAvail()))
            {
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
                    reinterpret_cast<const float *>(raw.data()),        // X = I
                    reinterpret_cast<const float *>(raw.data()) + 1,    // Y = Q
                    raw.size(),
                    0,                                                // flags
                    0,                                                // offset
                    sizeof(std::complex<float>)                       // stride
                );

                ImPlot::EndPlot();
            }
            ImGui::End();

            ImGui::Begin("Time domain raw");
            if (ImPlot::BeginPlot("I:Q", ImGui::GetContentRegionAvail()))
            {
                ImPlot::PlotLine("I",
                    reinterpret_cast<const float *>(raw.data()),
                    raw.size(),
                    1.0, 0.0, 0, 0,
                    sizeof(std::complex<float>));
                ImPlot::PlotLine("Q",
                    reinterpret_cast<const float *>(raw.data()) + 1,
                    raw.size(),
                    1.0, 0.0, 0, 0,
                    sizeof(std::complex<float>));
                ImPlot::EndPlot();
            }
            ImGui::End();

            ImGui::Begin("Time domain sync");
            if (ImPlot::BeginPlot("I:Q", ImGui::GetContentRegionAvail()))
            {
                ImPlot::PlotLine("Met", metrics.data(), metrics.size());
                ImPlot::EndPlot();
            }
            ImGui::End();

            ImGui::Begin("Plato");
            if (ImPlot::BeginPlot("Corr", ImGui::GetContentRegionAvail()))
            {
                ImPlot::PlotLine("Corr", plato.data(), plato.size());
                ImPlot::EndPlot();
            }
            ImGui::End();

            ImGui::Begin("Time");
            if (ImPlot::BeginPlot("Time", ImGui::GetContentRegionAvail()))
            {
                ImPlot::PlotLine("Time", time_history.data(), time_history.size());
                ImPlot::EndPlot();
            }
            ImGui::End();

            ImGui::Begin("history");
            if (ImPlot::BeginPlot("I:Q", ImGui::GetContentRegionAvail()))
            {
                ImPlot::PlotLine("I", reinterpret_cast<const float *>(data.receive_history.data()), data.receive_history.size(), 0, 0, 0, 0, sizeof(std::complex<float>));
                ImPlot::PlotLine("I", reinterpret_cast<const float *>(data.receive_history.data()) + 1, data.receive_history.size(), 0, 0, 0, 0, sizeof(std::complex<float>));

                ImPlot::EndPlot();
            }
            ImGui::End();

            // ImGui::Begin("TX");
            // if (ImPlot::BeginPlot("I:Q", ImGui::GetContentRegionAvail()))
            // {
            //     ImPlot::SetupAxes("N", "Magnitude", ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
            //     ImPlot::SetupAxesLimits(0, context.buffer_size, -50, 50, ImPlotCond_Once);
            //     static bool reset_view = false;
            //     if (reset_view)
            //     {
            //         ImPlot::SetupAxesLimits(0, 192, -4096, 4096, ImPlotCond_Always);
            //         reset_view = false;
            //     }

            //     if (ImPlot::BeginLegendPopup("I") or ImPlot::BeginLegendPopup("Q"))
            //     {
            //         if (ImGui::Button("Reset view"))
            //             reset_view = true;
            //         ImPlot::EndLegendPopup();
            //     }
            //     ImPlot::PlotLine("I", txraw_i.data(), txraw_i.size());
            //     ImPlot::PlotLine("Q", txraw_q.data(), txraw_q.size());
            //     ImPlot::EndPlot();
            // }
            // ImGui::End();


            ImGui::Begin("Sync");

            if (ImPlot::BeginPlot("Sync", ImGui::GetContentRegionAvail()))
            {
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
                    reinterpret_cast<const float *>(data.mod.sync.data()),        // X = I
                    reinterpret_cast<const float *>(data.mod.sync.data()) + 1,    // Y = Q
                    data.mod.sync.size(),
                    0,                                                // flags
                    0,                                                // offset
                    sizeof(std::complex<float>)                       // stride
                );
                ImPlot::EndPlot();
            }
            ImGui::End();

            ImGui::Begin("Conv");

            if (ImPlot::BeginPlot("Conv", ImGui::GetContentRegionAvail()))
            {
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
                    reinterpret_cast<const float *>(data.mod.conv.data()),        // X = I
                    reinterpret_cast<const float *>(data.mod.conv.data()) + 1,    // Y = Q
                    data.mod.conv.size(),
                    0,                                                // flags
                    0,                                                // offset
                    sizeof(std::complex<float>)                       // stride
                );
                ImPlot::EndPlot();
            }
            ImGui::End();

            ImGui::Begin("Gardner");

            if (ImPlot::BeginPlot("just", ImGui::GetContentRegionAvail()))
            {
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
                    reinterpret_cast<const float *>(data.mod.demodul.data()),        // X = I
                    reinterpret_cast<const float *>(data.mod.demodul.data()) + 1,    // Y = Q
                    data.mod.demodul.size(),
                    0,                                                // flags
                    0,                                                // offset
                    sizeof(std::complex<float>)                       // stride
                );
                ImPlot::EndPlot();
            }
            ImGui::End();

            // ImGui::Begin("Waterfall");

            // if (wfTex == 0)
            // {
            //     glGenTextures(1, &wfTex);
            //     glBindTexture(GL_TEXTURE_2D, wfTex);

            //     glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, gui::NFFT, gui::WF_H, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);

            //     glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            //     glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            //     glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            //     glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            //     glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            // }
            // if (!x_init)
            // {
            //     x_hz.resize(context.buffer_size);
            //     gui::compute_hz(context, x_hz);
            //     x_init = true;
            // }

            // if (!stopped)
            // {
            //     static double last_wf = 0.0;
            //     double now = ImGui::GetTime();
            //     if (now - last_wf > (1.0 / 60.0))
            //     {
            //         static std::vector<int16_t> iqpad(gui::NFFT * 2, 0);
            //         int avail_complex = std::min((int)rx_copy.size() / 2, 1920);
            //         for (int n = 0; n < gui::NFFT; n++)
            //         {
            //             if (n < avail_complex)
            //             {
            //                 iqpad[2 * n + 0] = rx_copy[2 * n + 0];
            //                 iqpad[2 * n + 1] = rx_copy[2 * n + 1];
            //             }
            //             else
            //             {
            //                 iqpad[2 * n + 0] = 0;
            //                 iqpad[2 * n + 1] = 0;
            //             }
            //         }
            //         gui::compute_wf_row_u8(iqpad.data(), wfRow.data());

            //         glBindTexture(GL_TEXTURE_2D, wfTex);
            //         glTexSubImage2D(GL_TEXTURE_2D, 0, 0, wfHead, gui::NFFT, 1, GL_RGB, GL_UNSIGNED_BYTE, wfRow.data());
            //         wfHead = (wfHead + 1) % gui::WF_H;

            //         last_wf = now;
            //     }
            // }

            // ImVec2 avail = ImGui::GetContentRegionAvail();
            // if (avail.x < 50)
            //     avail.x = 50;
            // if (avail.y < 50)
            //     avail.y = 50;
            // // if (avail.y > ImGui::GetWindowSize().y * 3 / 4)
            //     // avail.y = ImGui::GetWindowSize().y * 3 / 4;

            // ImDrawList *dl = ImGui::GetWindowDrawList();
            // ImVec2 p0 = ImGui::GetCursorScreenPos();
            // ImVec2 p1 = ImVec2(p0.x + avail.x, p0.y + avail.y);

            // ImTextureID tid = (ImTextureID)(intptr_t)wfTex;
            // float split = (float)wfHead / (float)gui::WF_H; // 0..1
            // float hA = (1.0f - split) * avail.y;



            // if (hA > 0.5f)
            // {
            //     ImVec2 a0 = p0;
            //     ImVec2 a1 = ImVec2(p1.x, p0.y + hA);
            //     ImVec2 uv0 = ImVec2(0.0f, split);
            //     ImVec2 uv1 = ImVec2(1.0f, 1.0f);
            //     dl->AddImage(tid, a0, a1, uv0, uv1);
            // }
            // float hB = split * avail.y;
            // if (hB > 0.5f)
            // {
            //     ImVec2 b0 = ImVec2(p0.x, p0.y + hA);
            //     ImVec2 b1 = p1;
            //     ImVec2 uv0 = ImVec2(0.0f, 0.0f);
            //     ImVec2 uv1 = ImVec2(1.0f, split);
            //     dl->AddImage(tid, b0, b1, uv0, uv1);
            // }

            // ImGui::End();

            // ImGui::Begin("Spectrum");
            // spec_smooth.resize(fft_vec.size());
            // for (size_t i = 0; i < fft_vec.size(); ++i)
            //     spec_smooth[i] = alpha * fft_vec[i] + (1.0f - alpha) * spec_smooth[i];

            // ImVec2 sz = ImGui::GetContentRegionAvail();

            // if (ImPlot::BeginPlot("FFT", sz))
            // {

            //     ImPlot::SetupAxisLimits(ImAxis_X1, context.sample_rate / -2, context.sample_rate / 2, ImGuiCond_Always);
            //     ImPlot::SetupAxisLimits(ImAxis_Y1, -80.0, 30.0, ImGuiCond_Always);
            //     ImPlot::SetupAxis(ImAxis_Y1, "", ImPlotAxisFlags_NoTickLabels | ImPlotAxisFlags_NoTickMarks);

            //     ImPlot::PlotLine("Spectrum", x_hz.data(), spec_smooth.data(), fft_vec.size());
            //     if (ImPlot::IsPlotHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            //     {
            //         ImPlotPoint mp = ImPlot::GetPlotMousePos(ImAxis_X1, ImAxis_Y1);
            //         double f_click = mp.x;
            //         context.rx_carrier_freq = context.rx_carrier_freq + f_click;
            //         context.flags |= Flags::APPLY_FREQUENCY;
            //     }

            //     ImPlot::EndPlot();
            // }
            // ImGui::End();
            ImGui::Begin("Time domain Send History");
            if (ImPlot::BeginPlot("Send History", ImGui::GetContentRegionAvail()))
            {
                ImPlot::SetupAxesLimits(0, 1920 * 4, -10, 4000, ImPlotCond_Once);
                ImPlot::PlotLine("Met", send_history.data(), send_history.size());
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

int run_sdr(sdr_config_t &context, SharedData_t &data)
{
    Flags apply = Flags::APPLY_BANDWIDTH | Flags::APPLY_FREQUENCY | Flags::APPLY_GAIN | Flags::APPLY_SAMPLE_RATE;
    while (!has_flag(context.flags, Flags::IS_ACTIVE))
    {
        if (has_flag(context.flags, Flags::EXIT))
            return 0;
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    if (init(&context) != 0)
    {
        std::cerr << "Initialization error\n";
        return -1;
    }
    int N = data.mod.n * 10 * 4;
    std::vector<int> bits(N, 0);
    std::vector<int16_t> tx_buffer;
    gen_bits(N, bits);
    qpsk_3gpp(bits, tx_buffer, true);

    void *tx_buffs[] = { tx_buffer.data() };

    int flags = SOAPY_SDR_HAS_TIME;
    long long timeNs;
    long timeoutUs = 400000;
    size_t k = 0;
    float buff_count = tx_buffer.size() / (context.buffer_size * 2.0);

    while (!has_flag(context.flags, Flags::EXIT))
    {
        k += 1;
        auto start = std::chrono::steady_clock::now();

        if (has_flag(context.flags, Flags::REINIT))
            reinit(context);
        if (has_flag(context.flags, apply))
            apply_runtime(context);
        if (has_flag(context.flags, Flags::REMODULATION))
        {
            gui::change_modulation(context, tx_buffer, bits, data, ofdm_mod);
            k = 0;
            buff_count = tx_buffer.size() / (context.buffer_size * 2.0);
        }
        if (flag_barker)
        {
            implement_barker(tx_buffer);
            changed = true;
        }
        else if (changed && !flag_barker)
        {
            changed = false;
            qpsk_3gpp(bits, tx_buffer, true);
        }
        while (stopped)
            std::this_thread::sleep_for(std::chrono::milliseconds(50));

        int wi = data.write_index.load(std::memory_order_relaxed);

        void *rxbuffs[] = { data.rx[wi].data() };

        int ret = context.sdr->readStream(
            context.rxStream,
            rxbuffs,
            context.buffer_size,
            flags,
            timeNs,
            timeoutUs);

        if (ret > 0)
        {
            data.ready.store(true, std::memory_order_release);
            data.write_index.store(wi ^ 1, std::memory_order_relaxed);
        }
        else if (ret == SOAPY_SDR_OVERFLOW)
            std::cout << "OVERFLOW\n";
        else
            std::cout << "ERR " << ret << std::endl;

        tx_buffs[0] = static_cast<void *>(tx_buffer.data() + context.buffer_size * 2 * (k < buff_count ? k : 0));

        if (has_flag(context.flags, Flags::SEND))
        {
            int send = context.sdr->writeStream(context.txStream, (const void *const *)tx_buffs, context.buffer_size, flags, timeNs + (4 * 1000 * 1000), timeoutUs);
            send_history.push_back(send);
            if (send_history.size() > context.buffer_size * 4)
                send_history.erase(send_history.begin());
        }
        auto end = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        timed = 0.01 * duration.count() + 0.99 * timed;
    }
    deinit(&context);
    return 0;
}

int run_dsp(sdr_config_t &context, SharedData_t &data)
{
    FFTWPlan fft(data.mod.n, true);
    auto &raw = data.mod.raw;
    std::vector<std::complex<float>> for_ofdm;
    for_ofdm.reserve(context.buffer_size);

    while (!has_flag(context.flags, Flags::EXIT))
    {
        if (data.ready.load(std::memory_order_acquire))
        {
            int ri = data.read_index.load(std::memory_order_relaxed);

            auto rawi16 = data.rx[ri];

            for (size_t i = 0; i < rawi16.size() / 2; ++i)
                raw[i] = std::complex<float>(rawi16[2 * i + 0], rawi16[2 * i + 1]);
            data.ready.store(false, std::memory_order_relaxed);
            data.read_index.store(ri ^ 1, std::memory_order_relaxed);
        }

        // if (has_flag(context.flags, Flags::REMODULATION))
        //     fft = FFTWPlan(data.mod.n, true);

        if (data.mod.ModulationType == 0 or // BPSK
            data.mod.ModulationType == 1 or // QPSK
            data.mod.ModulationType == 2) // QAM16
        {
            data.mod.conv = convolve_ones(raw, 10);
            float max_val = 0.0f;
            for (const auto &x : data.mod.conv)
                max_val = std::max(max_val, std::abs(x));

            if (max_val > 0.0f)
            {
                for (auto &x : data.mod.conv)
                    x = coef * x / max_val;
            }
            data.mod.sync = costas_loop(data.mod.conv, costas_bor);
            data.mod.demodul = gardner(data.mod.sync, gardner_bor, 2);
        }
        else if (data.mod.ModulationType == 4) //OFDM
        {
            int next = 0;
            for_ofdm = raw;
            // data.mod.ofdm.clear();
            // schmidl_cox_detect(for_ofdm, data.mod.n, cfo, max_index);
            plato = minn_metric(for_ofdm, data.mod.n, max_index);
            if (data.mod.cfo)
            {
                cfo = estimate_cfo(for_ofdm, data.mod.n, max_index, context.sample_rate);
                for (size_t n = 0; n < for_ofdm.size(); ++n)
                {
                    std::complex<float> phase = std::exp(std::complex<float>(0, -2.0f * M_PI * cfo * n * (1.0f / context.sample_rate)));
                    for_ofdm[n] *= phase;
                }
            }

            if (for_ofdm.size() > max_index + data.mod.n * 2)
                next += max_index + data.mod.n + offset;
            for (size_t n = 0; n < 10; ++n)
            {
                if (for_ofdm.size() - next < data.mod.n + data.mod.ncp)
                    break;
                next += data.mod.ncp;
                for (size_t i = 0; i < data.mod.n; ++i)
                {
                    fft.in[i][0] = std::real(for_ofdm[next + i]);
                    fft.in[i][1] = std::imag(for_ofdm[next + i]);
                }
                fftwf_execute(fft.plan);

                for (size_t i = 0; i < data.mod.n; ++i)
                    data.mod.ofdm[i + n * static_cast<size_t>(data.mod.n)] = std::complex<float>(fft.out[i][0], fft.out[i][1]);

                next += data.mod.n;
                // std::rotate(data.mod.ofdm.begin() + n * data.mod.n,
                    // data.mod.ofdm.begin() + n * data.mod.n + data.mod.n / 2,
                    // data.mod.ofdm.begin() + (n + 1) * data.mod.n);
            }

            ofdm_equalize(data.mod.ofdm, data.mod.n, data.mod.ps);
        }

        int next = 0;
        time_history.push_back(timed / 1e3);
        if (time_history.size() > 4000)
            time_history.erase(time_history.begin());

        if (!stopped)
        {
            data.receive_history.insert(data.receive_history.end(), raw.begin(), raw.end());
            if (data.receive_history.size() > context.buffer_size * 10)
                data.receive_history.erase(data.receive_history.begin(), data.receive_history.begin() + 1920);
            metrics.push_back(max_index);
            if (metrics.size() > context.buffer_size)
                metrics.erase(metrics.begin());
            if (metrics[metrics.size() - 1] > threshold)
                stopped = can_be_stopped ? true : false;
        }

        gui::compute_fftw(raw, data.signal_spectrum);

    }
    std::cout << "Closing DSP thread\n";
    return 0;
}

int main(int argc, char *argv[])
{
    fftwf_init_threads();
    fftwf_plan_with_nthreads(std::thread::hardware_concurrency());
    (void)argc;
    (void)argv;
    sdr_config_t sdr(
        "", 1920,
        1.92e6,
        300e6, 300e6,
        89.0, 25.0,
        true, true);
    int subcarrier_count = static_cast<int>(1.92e6 / 15e3);
    SharedData_t data(sdr.buffer_size, subcarrier_count, subcarrier_count / 8, 18, 1);

    std::thread gui_thread(run_gui, std::ref(sdr), std::ref(data));
    std::thread sdr_thread(run_sdr, std::ref(sdr), std::ref(data));
    std::thread dsp_thread(run_dsp, std::ref(sdr), std::ref(data));

    gui_thread.join();
    sdr_thread.join();
    dsp_thread.join();

    return 0;
}
