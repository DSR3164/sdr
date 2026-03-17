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
    static GLuint wfTex = 0;
    static int wfHead = 0;

    static std::vector<uint8_t> wfRow(gui::NFFT * 3);
    static std::vector<float> x_hz(gui::NFFTW);
    static std::vector<float> spec_smooth(gui::NFFT, -120.0f);
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

            gui::compute_fftw(raw, fft_vec);

            ImGui::Begin("Settings");
            gui::context_edit_window(context, data);
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
                    ImPlot::PlotLine("I",
                        reinterpret_cast<const float *>(ofdm.data()),
                        data.ofdm_cfg.n_subcarriers * 10, 1.0, 0.0, 0, 0,
                        sizeof(std::complex<float>));
                    ImPlot::PlotLine("Q",
                        reinterpret_cast<const float *>(ofdm.data()) + 1,
                        data.ofdm_cfg.n_subcarriers * 10, 1.0, 0.0, 0, 0,
                        sizeof(std::complex<float>));
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
                ImPlot::PlotLine("I",
                    reinterpret_cast<const float *>(raw.data()),
                    raw.size(), 1.0, 0.0, 0, 0,
                    sizeof(std::complex<float>));
                ImPlot::PlotLine("Q",
                    reinterpret_cast<const float *>(raw.data()) + 1,
                    raw.size(), 1.0, 0.0, 0, 0,
                    sizeof(std::complex<float>));
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
                ImPlot::PlotLine("I", reinterpret_cast<const float *>(data.history.receive.data()),
                    data.history.receive.size(), 1.0, 0, 0, 0, sizeof(std::complex<float>));
                ImPlot::PlotLine("Q", reinterpret_cast<const float *>(data.history.receive.data()) + 1,
                    data.history.receive.size(), 1.0, 0, 0, 0, sizeof(std::complex<float>));

                ImPlot::EndPlot();
            }
            ImGui::End();

            if (data.gui.debug)
            {
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

                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, gui::NFFT, gui::WF_H, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);

                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            }
            if (!data.gui.x_init)
            {
                x_hz.resize(context.buffer_size);
                gui::compute_hz(context, x_hz);
                data.gui.x_init = true;
            }

            if (!data.gui.stopped)
            {
                static double last_wf = 0.0;
                double now = ImGui::GetTime();
                if (now - last_wf > (1.0 / 60.0))
                {
                    static std::vector<int16_t> iqpad(gui::NFFT * 2, 0);
                    int avail_complex = std::min((int)raw.size(), 1920);
                    for (int n = 0; n < gui::NFFT; n++)
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
                    gui::compute_wf_row_u8(iqpad.data(), wfRow.data());

                    glBindTexture(GL_TEXTURE_2D, wfTex);
                    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, wfHead, gui::NFFT, 1, GL_RGB, GL_UNSIGNED_BYTE, wfRow.data());
                    wfHead = (wfHead + 1) % gui::WF_H;

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
            spec_smooth.resize(fft_vec.size());
            for (size_t i = 0; i < fft_vec.size(); ++i)
                spec_smooth[i] = alpha * fft_vec[i] + (1.0f - alpha) * spec_smooth[i];

            ImVec2 sz = ImGui::GetContentRegionAvail();

            if (ImPlot::BeginPlot("FFT", sz))
            {

                ImPlot::SetupAxisLimits(ImAxis_X1, context.sample_rate / -2, context.sample_rate / 2, ImGuiCond_Always);
                ImPlot::SetupAxisLimits(ImAxis_Y1, -80.0, 30.0, ImGuiCond_Always);
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

int run_sdr(sdr_config_t &context, SharedData_t &data)
{
    //код
    std::vector<std::string> modulations = { "BPSK", "QPSK", "QAM16", "QAM16 RRC", "OFDM" };
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
    auto start = std::chrono::steady_clock::now();
    int N = data.ofdm_cfg.n_subcarriers * 10 * 4;
    std::vector<int> bits(N, 0);
    std::vector<int16_t> tx_buffer;
    gen_bits(N, bits);
    gui::change_modulation(context, tx_buffer, bits, data, data.ofdm_cfg.mod);

    void *tx_buffs[] = { tx_buffer.data() };
    int flags = SOAPY_SDR_HAS_TIME;
    long long timeNs;
    long timeoutUs = 400000;
    size_t k = 0;
    float buff_count = tx_buffer.size() / (context.buffer_size * 2.0);
    auto end = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    data.gui.timed = 0.01 * duration.count() + 0.99 * data.gui.timed;
    std::cout << "First SDR init " << modulations[context.modulation_type] << " " << duration.count() << " mcs\n";
    std::cout << "With [SIZE] = " << bits.size() << " bits\n";
    while (!has_flag(context.flags, Flags::EXIT))
    {
        if (k >= buff_count) k = 0;
        auto start = std::chrono::steady_clock::now();

        if (has_flag(context.flags, Flags::REINIT))
            reinit(context);
        if (has_flag(context.flags, apply))
            apply_runtime(context);
        if (has_flag(context.flags, Flags::REMODULATION))
        {
            auto start = std::chrono::steady_clock::now();
            gui::change_modulation(context, tx_buffer, bits, data, data.ofdm_cfg.mod);
            k = 0;
            buff_count = tx_buffer.size() / (context.buffer_size * 2.0);
            auto end = std::chrono::steady_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
            data.gui.timed = 0.01 * duration.count() + 0.99 * data.gui.timed;
        }
        while (data.gui.stopped)
            std::this_thread::sleep_for(std::chrono::milliseconds(50));

        void *rxbuffs[] = { data.dsp_buff.get_write_buffer().data() };

        int ret = context.sdr->readStream(
            context.rxStream,
            rxbuffs,
            context.buffer_size,
            flags,
            timeNs,
            timeoutUs);

        if (ret > 0)
            data.dsp_buff.swap();
        else if (ret == SOAPY_SDR_OVERFLOW)
            std::cout << "OVERFLOW\n";
        else
            std::cout << "ERR " << ret << std::endl;


        if (has_flag(context.flags, Flags::SEND))
        {
            tx_buffs[0] = static_cast<void *>(tx_buffer.data());
            int send = context.sdr->writeStream(context.txStream, (const void *const *)tx_buffs, context.buffer_size, flags, timeNs + (4 * 1000 * 1000), timeoutUs);
            data.history.send.push_back(send);
            if (static_cast<int>(data.history.send.size()) > context.buffer_size * 4)
                data.history.send.erase(data.history.send.begin());
        }
        auto end = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        data.gui.timed = 0.01 * duration.count() + 0.99 * data.gui.timed;
        k += 1;
    }
    deinit(&context);
    return 0;
}

int run_dsp(sdr_config_t &context, SharedData_t &data)
{
    FFTWPlan fft(data.ofdm_cfg.n_subcarriers, true);
    std::chrono::steady_clock::time_point start;
    std::chrono::steady_clock::time_point end;
    auto &raw = data.mod.raw;

    std::vector<std::complex<float>> for_ofdm;
    std::vector<int16_t> temp(context.buffer_size * 2, 0);
    for_ofdm.reserve(context.buffer_size * 2);
    std::vector<std::complex<float>> zadoff_chu = ofdm_zadoff_chu_symbol(data);
    static float cfo = 0.0f;

    while (!has_flag(context.flags, Flags::EXIT))
    {
        while (data.gui.stopped)
            std::this_thread::sleep_for(std::chrono::milliseconds(50));

        if (data.dsp_buff.read(temp) == 0)
        {
            int16_t *in = temp.data();
            std::complex<float> *out = raw.data();
            size_t n = context.buffer_size;

            for (size_t i = 0; i < n; ++i)
            {
                float I = in[0];
                float Q = in[1];

                out[i] = { I, Q };
                in += 2;
            }
        }
        else
            continue;

        std::atomic_signal_fence(std::memory_order_seq_cst);
        start = std::chrono::steady_clock::now();
        std::atomic_signal_fence(std::memory_order_seq_cst);
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
                    x = data.dsp.scale_coef * x / max_val;
            }
            data.mod.sync = costas_loop(data.mod.conv, data.dsp.costas_band);
            data.mod.demodul = gardner(data.mod.sync, data.dsp.gardner_band, 2);
        }
        else if (data.mod.ModulationType == 4) //OFDM
        {
            int next = 0;
            for_ofdm = raw;
            if (data.ofdm_cfg.pss)
            {
                data.dsp.max_index = ofdm_zc_corr(for_ofdm, zadoff_chu, data.gui.plato);
                if (data.ofdm_cfg.cfo)
                    for_ofdm = cfo_est(for_ofdm, data, context);
            }
            if (data.ofdm_cfg.symbol_sync)
                ofdm_cp_sync(for_ofdm, data.ofdm_cfg.n_subcarriers, data.ofdm_cfg.n_cp, data.gui.plato);
            data.mod.ofdm.clear();
            data.mod.ofdm.resize(data.ofdm_cfg.n_subcarriers * 12);
            if (static_cast<int>(for_ofdm.size()) > data.dsp.max_index + data.ofdm_cfg.n_subcarriers * 2)
                next += data.dsp.max_index + (data.ofdm_cfg.symbol_sync ? 0 : data.ofdm_cfg.n_subcarriers) + data.dsp.offset;
            int last = -data.ofdm_cfg.n_subcarriers;
            for (size_t n = 0; n < 10; ++n)
            {
                if (static_cast<int>(for_ofdm.size()) - next < data.ofdm_cfg.n_subcarriers + data.ofdm_cfg.n_cp)
                    break;
                next += data.ofdm_cfg.n_cp;
                for (size_t i = 0; i < static_cast<size_t>(data.ofdm_cfg.n_subcarriers); ++i)
                {
                    fft.in[i][0] = std::real(for_ofdm[next + i]);
                    fft.in[i][1] = std::imag(for_ofdm[next + i]);
                }
                if (data.ofdm_cfg.fft)
                {
                    fftwf_execute(fft.plan);

                    for (size_t i = 0; i < static_cast<size_t>(data.ofdm_cfg.n_subcarriers); ++i)
                        data.mod.ofdm[i + n * static_cast<size_t>(data.ofdm_cfg.n_subcarriers)] = std::complex<float>(fft.out[i][0], fft.out[i][1]);
                }

                next += data.ofdm_cfg.n_subcarriers;
                last = next;
            }
            // data.mod.ofdm.erase(data.mod.ofdm.begin() + last, data.mod.ofdm.end());
            if (data.ofdm_cfg.eq)
                ofdm_equalize(data.mod.ofdm, data.ofdm_cfg.n_subcarriers, data.ofdm_cfg.pilot_spacing);
            data.gui_buff.write(data.mod.ofdm);

        }
        std::atomic_signal_fence(std::memory_order_seq_cst);
        end = std::chrono::steady_clock::now();
        std::atomic_signal_fence(std::memory_order_seq_cst);
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        data.history.sdrtime.push_back(data.gui.timed / 1e3);
        if (data.history.sdrtime.size() > 4000)
            data.history.sdrtime.erase(data.history.sdrtime.begin());
        data.history.dsptime.push_back(duration.count() / 1e3);
        if (data.history.dsptime.size() > 4000)
            data.history.dsptime.erase(data.history.dsptime.begin());

        if (!data.gui.stopped)
        {
            data.history.receive.insert(data.history.receive.end(), raw.begin(), raw.end());
            if (static_cast<int>(data.history.receive.size()) > context.buffer_size * 10)
                data.history.receive.erase(data.history.receive.begin(), data.history.receive.begin() + 1920);
            data.gui.metrics.push_back(data.dsp.max_index);
            if (static_cast<int>(data.gui.metrics.size()) > context.buffer_size)
                data.gui.metrics.erase(data.gui.metrics.begin());
            if (data.gui.plato[(data.dsp.max_index < 0 ? 0 : data.dsp.max_index)] > data.dsp.threshold)
                data.gui.stopped = data.gui.can_be_stopped ? true : false;
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
    fftwf_make_planner_thread_safe();
    (void)argc;
    (void)argv;
    sdr_config_t sdr(
        "", 1920,
        1.92e6,
        300e6, 300e6,
        89.0, 25.0,
        true, true);
    sdr.modulation_type = 4;
    sdr.flags |= Flags::APPLY_BANDWIDTH;
    int subcarrier_count = static_cast<int>(sdr.sample_rate / 15e3);
    SharedData_t data(sdr.buffer_size, subcarrier_count, 32, 56, 4);

    std::thread gui_thread(run_gui, std::ref(sdr), std::ref(data));
    std::thread sdr_thread(run_sdr, std::ref(sdr), std::ref(data));
    std::thread dsp_thread(run_dsp, std::ref(sdr), std::ref(data));

    gui_thread.join();
    sdr_thread.join();
    dsp_thread.join();

    return 0;
}
