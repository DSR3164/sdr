#include <GL/glew.h>
#include <SDL2/SDL.h>

#include <iostream>
#include <thread>

#include "backends/imgui_impl_opengl3.h"
#include "backends/imgui_impl_sdl2.h"
#include "imgui.h"
#include "implot.h"
#include "implot3d.h"
#include "fftw3.h"
#include "gui.h"
#include <dsp_module.h>
#include <SoapySDR/Device.hpp>
#include <SoapySDR/Formats.hpp>
#include <vector>

static double gardner_band = 1;
static double costas_band = 15e-4;
static float coef = 150.0;
static bool flag_barker = false;
static bool changed = false;

namespace
{
    static GLuint wfTex = 0;
    static bool x_init = false;
    static int wfHead = 0;

    static std::vector<uint8_t> wfRow(gui::NFFT * 3);
    static std::vector<float> spec_db(gui::NFFTW);
    static std::vector<float> x_hz(gui::NFFTW);
    static std::vector<float> spec_smooth(gui::NFFT, -120.0f);
    static const float alpha = 0.15f;

}

int context_edit_window(sdr_config_t &context)
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
    static int current_rx_bandhwidth = 1;
    static int current_tx_bandhwidth = 1;
    static std::vector<float> values = { 0.2e6, 1e6, 2e6, 3e6, 4e6, 5e6, 6e6, 7e6, 8e6, 9e6, 10e6 };
    static SoapySDR::KwargsList list;
    static bool is_scanning = false;
    std::vector<std::string> modulations = { "BPSK", "QPSK", "QAM16", "QAM16 RRC", "OFDM" };
    bool changed_tx_g = ImGui::SliderFloat("TX Gain", &context.tx_gain, 0.0f, 89.0f, "%.3f");
    bool changed_rx_g = ImGui::SliderFloat("RX Gain", &context.rx_gain, 0.0f, 73.0f, "%.3f");
    bool changed_tx_f = ImGui::InputDouble("TX Frequency", &context.tx_carrier_freq, 10e3, 10e5, "%e");
    bool changed_rx_f = ImGui::InputDouble("RX Frequency", &context.rx_carrier_freq, 10e3, 10e5, "%e");
    ImGui::SliderFloat("Coefficient", &coef, 0.0f, 2000.0f, "%.3f");
    ImGui::InputDouble("Gardner", &gardner_band, 1e-6, 1, "%e");
    ImGui::InputDouble("Costas", &costas_band, 1e-6, 1, "%e");
    if (ImGui::SliderInt("TX Bandwidth", &current_tx_bandhwidth, 0, values.size() - 1, std::to_string(values[current_tx_bandhwidth]).c_str()))
    {
        context.tx_bandwidth = values[current_tx_bandhwidth];
        context.flags |= Flags::APPLY_BANDWIDTH;
    }
    if (ImGui::SliderInt("RX Bandwidth", &current_rx_bandhwidth, 0, values.size() - 1, std::to_string(values[current_rx_bandhwidth]).c_str()))
    {
        context.rx_bandwidth = values[current_rx_bandhwidth];
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
        if (ImGui::SliderInt("OFDM subcarriers", &context.n, 4, std::round(context.sample_rate / 25e3)))
            context.flags |= Flags::REMODULATION;
        if (ImGui::SliderInt("OFDM CP len", &context.ncp, 4, 64))
            context.flags |= Flags::REMODULATION;
        if (ImGui::SliderInt("OFDM Pilot Spacing", &context.ps, 2, 64))
            context.flags |= Flags::REMODULATION;
    }

    bool send_enabled = (context.flags & Flags::SEND) != Flags::None;
    ImGui::Checkbox("Barker", &flag_barker);
    ImGui::SameLine();

    if (ImGui::Checkbox("Send", &send_enabled))
    {
        if (send_enabled)
            context.flags |= Flags::SEND;
        else
            context.flags &= ~Flags::SEND;
    }

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

void run_gui(sdr_config_t &context)
{
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER);
    SDL_Window *window = SDL_CreateWindow(
        "ImGUI RF", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        1520, 720, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    SDL_GLContext gl_context = SDL_GL_CreateContext(window);
    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImPlot3D::CreateContext();
    SDL_GL_SetSwapInterval(0);

    ImGui_ImplSDL2_InitForOpenGL(window, gl_context);
    ImGui_ImplOpenGL3_Init("#version 330");

    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    bool running = true;
    std::vector<std::vector<int16_t>> i_history;
    std::vector<std::vector<int16_t>> q_history;

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
            int Nb = context.buffer_size;
            std::vector<float> raw_i(Nb);
            std::vector<float> raw_q(Nb);
            std::vector<float> gardner_i(Nb);
            std::vector<float> gardner_q(Nb);
            std::vector<float> sync_i(Nb);
            std::vector<float> sync_q(Nb);
            std::vector<float> conv_i(Nb);
            std::vector<float> conv_q(Nb);
            std::vector<float> fft_vec(Nb);

            std::vector<std::complex<float>> demodul(context.buffer_size / 10);
            std::vector<std::complex<float>> sync(Nb);
            std::vector<std::complex<float>> conv(Nb);
            std::vector<std::complex<float>> raw(Nb);
            for (int i = 0; i < context.buffer_size; ++i)
                raw[i] = std::complex<float>(context.buffer[2 * i], context.buffer[2 * i + 1]);
            conv = convolve_ones(raw, 10);
            float max_val = 0.0f;
            for (const auto &x : conv)
                max_val = std::max(max_val, std::abs(x));

            if (max_val > 0.0f)
            {
                for (auto &x : conv)
                    x = coef * x / max_val;
            }

            gui::compute_fftw(context.buffer, fft_vec);
            sync = costas_loop(conv, costas_band);
            demodul = gardner(sync, gardner_band, 10);
            for (int i = 0; i < context.buffer_size; ++i)
            {
                raw_i[i] = std::real(raw[i]);
                raw_q[i] = std::imag(raw[i]);
                sync_i[i] = std::real(sync[i]);
                sync_q[i] = std::imag(sync[i]);
                conv_i[i] = std::real(conv[i]);
                conv_q[i] = std::imag(conv[i]);
                gardner_i[i] = std::real(demodul[i]);
                gardner_q[i] = std::imag(demodul[i]);
            }

            ImGui::Begin("Constellation", nullptr, ImGuiWindowFlags_MenuBar);
            ImGui::Text("FPS: %.1f (%.3f ms)", io.Framerate, 1000.0f / io.Framerate);
            if (ImGui::BeginMenuBar())
            {
                if (ImGui::BeginMenu("Config"))
                {
                    context_edit_window(context);
                    ImGui::EndMenu();
                }
                ImGui::EndMenuBar();
            }
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
                ImPlot::PlotScatter("Const", raw_i.data(), raw_q.data(), context.buffer_size);
                ImPlot::EndPlot();
            }
            ImGui::End();

            ImGui::Begin("Time domain sync");
            if (ImPlot::BeginPlot("I:Q", ImGui::GetContentRegionAvail()))
            {
                ImPlot::SetupAxesLimits(0, context.buffer_size, -50, 50, ImPlotCond_Once);
                static bool reset_view = false;
                if (reset_view)
                {
                    ImPlot::SetupAxesLimits(0, 192, -4096, 4096, ImPlotCond_Always);
                    reset_view = false;
                }

                if (ImPlot::BeginLegendPopup("I") or ImPlot::BeginLegendPopup("Q"))
                {
                    if (ImGui::Button("Reset view"))
                        reset_view = true;
                    ImPlot::EndLegendPopup();
                }
                ImPlot::PlotLine("I", sync_i.data(), context.buffer_size);
                ImPlot::PlotLine("Q", sync_q.data(), context.buffer_size);
                ImPlot::EndPlot();
            }
            ImGui::End();

            ImGui::Begin("Time domain conv");
            if (ImPlot::BeginPlot("I:Q", ImGui::GetContentRegionAvail()))
            {
                ImPlot::SetupAxesLimits(0, context.buffer_size, -50, 50, ImPlotCond_Once);
                static bool reset_view = false;
                if (reset_view)
                {
                    ImPlot::SetupAxesLimits(0, 192, -4096, 4096, ImPlotCond_Always);
                    reset_view = false;
                }

                if (ImPlot::BeginLegendPopup("I") or ImPlot::BeginLegendPopup("Q"))
                {
                    if (ImGui::Button("Reset view"))
                        reset_view = true;
                    ImPlot::EndLegendPopup();
                }
                ImPlot::PlotLine("I", conv_i.data(), context.buffer_size);
                ImPlot::PlotLine("Q", conv_q.data(), context.buffer_size);
                ImPlot::EndPlot();
            }
            ImGui::End();

            ImGui::Begin("Time domain raw");
            if (ImPlot::BeginPlot("I:Q", ImGui::GetContentRegionAvail()))
            {
                ImPlot::SetupAxesLimits(0, context.buffer_size, -50, 50, ImPlotCond_Once);
                static bool reset_view = false;
                if (reset_view)
                {
                    ImPlot::SetupAxesLimits(0, 192, -4096, 4096, ImPlotCond_Always);
                    reset_view = false;
                }

                if (ImPlot::BeginLegendPopup("I") or ImPlot::BeginLegendPopup("Q"))
                {
                    if (ImGui::Button("Reset view"))
                        reset_view = true;
                    ImPlot::EndLegendPopup();
                }
                ImPlot::PlotLine("I", raw_i.data(), context.buffer_size);
                ImPlot::PlotLine("Q", raw_q.data(), context.buffer_size);
                ImPlot::EndPlot();
            }
            ImGui::End();

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
                ImPlot::PlotScatter("Const", sync_i.data(), sync_q.data(), context.buffer_size);
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
                ImPlot::PlotScatter("Const", conv_i.data(), conv_q.data(), context.buffer_size);
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
                ImPlot::PlotScatter("Const", gardner_i.data(), gardner_q.data(), context.buffer_size);
                ImPlot::EndPlot();
            }
            ImGui::End();
        }

        ImGui::Begin("Waterfall");

        if (wfTex == 0)
        {
            glGenTextures(1, &wfTex);
            glBindTexture(GL_TEXTURE_2D, wfTex);

            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, gui::NFFT, gui::WF_H, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);

            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        }
        if (!x_init)
        {
            x_hz.resize(context.buffer_size);
            gui::compute_hz(context, x_hz);
            x_init = true;
        }

        static double last_wf = 0.0;
        double now = ImGui::GetTime();
        if (now - last_wf > (1.0 / 60.0))
        {
            static std::vector<int16_t> iqpad(gui::NFFT * 2, 0);
            int avail_complex = std::min((int)context.buffer.size() / 2, 1920);
            for (int n = 0; n < gui::NFFT; n++)
            {
                if (n < avail_complex)
                {
                    iqpad[2 * n + 0] = context.buffer[2 * n + 0];
                    iqpad[2 * n + 1] = context.buffer[2 * n + 1];
                }
                else
                {
                    iqpad[2 * n + 0] = 0;
                    iqpad[2 * n + 1] = 0;
                }
            }
            gui::compute_wf_row_u8(iqpad.data(), wfRow.data());

            glBindTexture(GL_TEXTURE_2D, wfTex);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, wfHead, gui::NFFT, 1, GL_RGB, GL_UNSIGNED_BYTE, wfRow.data());
            wfHead = (wfHead + 1) % gui::WF_H;

            last_wf = now;
        }

        ImVec2 avail = ImGui::GetContentRegionAvail();
        if (avail.x < 50)
            avail.x = 50;
        if (avail.y < 50)
            avail.y = 50;
        // if (avail.y > ImGui::GetWindowSize().y * 3 / 4)
            // avail.y = ImGui::GetWindowSize().y * 3 / 4;

        ImDrawList *dl = ImGui::GetWindowDrawList();
        ImVec2 p0 = ImGui::GetCursorScreenPos();
        ImVec2 p1 = ImVec2(p0.x + avail.x, p0.y + avail.y);

        ImTextureID tid = (ImTextureID)(intptr_t)wfTex;
        float split = (float)wfHead / (float)gui::WF_H; // 0..1
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
        gui::compute_fftw(context.buffer, spec_db);
        spec_smooth.resize(spec_db.size());
        for (int i = 0; i < spec_db.size(); ++i)
            spec_smooth[i] = alpha * spec_db[i] + (1.0f - alpha) * spec_smooth[i];

        ImVec2 sz = ImGui::GetContentRegionAvail();

        if (ImPlot::BeginPlot("FFT", sz))
        {

            ImPlot::SetupAxisLimits(ImAxis_X1, context.sample_rate / -2, context.sample_rate / 2, ImGuiCond_Always);
            ImPlot::SetupAxisLimits(ImAxis_Y1, -80.0, 30.0, ImGuiCond_Always);
            ImPlot::SetupAxis(ImAxis_Y1, "", ImPlotAxisFlags_NoTickLabels | ImPlotAxisFlags_NoTickMarks);

            ImPlot::PlotLine("Spectrum", x_hz.data(), spec_smooth.data(), spec_db.size());
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
    ImPlot3D::DestroyContext();
    ImGui::DestroyContext();
    SDL_GL_DeleteContext(gl_context);
    SDL_DestroyWindow(window);
    SDL_Quit();
}

int sdr_backend(sdr_config_t &context)
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
    int N = 192000;
    std::vector<int> bits(N);
    std::vector<int16_t> tx_buffer;
    gen_bits(N, bits);
    qpsk_3gpp(bits, tx_buffer, true);

    void *rx_buffs[] = { context.buffer.data() };
    void *tx_buffs[] = { tx_buffer.data() };

    int flags = SOAPY_SDR_HAS_TIME;
    long long timeNs;
    long timeoutUs = 400000;
    int k = 0;
    float buff_count = (tx_buffer.size() / (context.buffer_size * 2));

    while (!has_flag(context.flags, Flags::EXIT))
    {

        if (has_flag(context.flags, Flags::REINIT))
            reinit(context);
        if (has_flag(context.flags, apply))
            apply_runtime(context);
        if (has_flag(context.flags, Flags::REMODULATION))
        {
            gui::change_modulation(context, tx_buffer);
            k = 0;
            buff_count = (float)(tx_buffer.size() / (context.buffer_size * 2));
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
        int total = 0;

        while (total < context.buffer_size)
        {
            void *buffs[] = {
                context.buffer.data() + total * 2 };

            int ret = context.sdr->readStream(
                context.rxStream,
                buffs,
                context.buffer_size - total,
                flags,
                timeNs,
                timeoutUs);

            tx_buffs[0] = static_cast<void *>(tx_buffer.data() + context.buffer_size * 2 * (k < buff_count ? k : 0));

            if (has_flag(context.flags, Flags::SEND))
                context.sdr->writeStream(context.txStream, (const void *const *)tx_buffs, context.buffer_size, flags, timeNs + (4 * 1000 * 1000), timeoutUs);
            k += 1;

            if (ret > 0)
                total += ret;
        }
    }
    deinit(&context);
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
        1e6,
        826e6, 826e6,
        89.0, 25.0);

    std::thread gui_thread(run_gui, std::ref(sdr));
    std::thread sdr_thread(sdr_backend, std::ref(sdr));

    gui_thread.join();
    sdr_thread.join();

    return 0;
}
