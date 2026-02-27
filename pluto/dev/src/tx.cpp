#include <GL/glew.h>
#include <SDL2/SDL.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <thread>

#include "backends/imgui_impl_opengl3.h"
#include "backends/imgui_impl_sdl2.h"
#include "imgui.h"
#include "implot.h"
#include "gui.h"
#include <dsp_module.h>
#include <SoapySDR/Device.hpp>
#include <vector>

static bool flag_barker = false;
static bool changed = false;
std::vector<int> metrics;

int context_edit_window(sdr_config_t &context)
{
    int current_uri = -1;
    int current_mod = 1;
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
    int current_rx_bandhwidth = 10;
    int current_tx_bandhwidth = 1;
    static std::vector<float> values = { 0.2e6, 1e6, 2e6, 3e6, 4e6, 5e6, 6e6, 7e6, 8e6, 9e6, 10e6 };
    static SoapySDR::KwargsList list;
    static bool is_scanning = false;
    std::vector<std::string> modulations = { "BPSK", "QPSK", "QAM16", "QAM16 RRC", "OFDM" };
    bool changed_tx_g = ImGui::SliderFloat("TX Gain", &context.tx_gain, 0.0f, 89.0f, "%.3f");
    bool changed_rx_g = ImGui::SliderFloat("RX Gain", &context.rx_gain, 0.0f, 73.0f, "%.3f");
    bool changed_tx_f = ImGui::InputDouble("TX Frequency", &context.tx_carrier_freq, 10e3, 10e5, "%e");
    bool changed_rx_f = ImGui::InputDouble("RX Frequency", &context.rx_carrier_freq, 10e3, 10e5, "%e");
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
        if (ImGui::SliderInt("OFDM subcarriers", &context.n, 4, std::round(context.sample_rate / 15e3)))
            context.flags |= Flags::REMODULATION;
        if (ImGui::SliderInt("OFDM CP len", &context.ncp, 4, 64))
            context.flags |= Flags::REMODULATION;
        if (ImGui::SliderInt("OFDM Pilot Spacing", &context.ps, 2, std::round(context.sample_rate / 15e3) - 3))
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

void run_gui(sdr_config_t &context, int &send)
{
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER);
    SDL_Window *window = SDL_CreateWindow(
        "ImGUI RF", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        1520, 720, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    SDL_GLContext gl_context = SDL_GL_CreateContext(window);
    ImGui::CreateContext();
    ImPlot::CreateContext();
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
            std::vector<int16_t> rx_copy = context.rxbuffer;
            std::vector<int16_t> tx_copy = context.txbuffer;
            std::vector<float> txraw_q(tx_copy.size() / 2, 1);
            std::vector<float> txraw_i(tx_copy.size() / 2, 1);
            if (!tx_copy.empty())
            {
                for (size_t i = 0; i < (tx_copy.size() / 2); ++i)
                {
                    txraw_i[i] = (tx_copy[2 * i]);
                    txraw_q[i] = (tx_copy[2 * i + 1]);
                }
            }


            ImGui::Begin("Constellation", nullptr, ImGuiWindowFlags_MenuBar);
            ImGui::Text("Sending %d samples)", send);
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

            if (ImPlot::BeginPlot("I:Q", ImGui::GetContentRegionAvail()))
            {
                ImPlot::SetupAxes("N", "Magnitude", ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
                ImPlot::SetupAxesLimits(0, context.buffer_size, -50, 50, ImPlotCond_Once);
                ImPlot::PlotLine("I", txraw_i.data(), txraw_i.size());
                ImPlot::PlotLine("Q", txraw_q.data(), txraw_q.size());
                ImPlot::EndPlot();
            }
            ImGui::End();
            ImGui::Begin("Time domain sync");
            if (ImPlot::BeginPlot("Send History", ImGui::GetContentRegionAvail()))
            {
                ImPlot::SetupAxesLimits(0, 1920 * 4, -10, 4000, ImPlotCond_Once);
                ImPlot::PlotLine("Met", metrics.data(), metrics.size());
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

int sdr_tx(sdr_config_t &context, int &send)
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
    int N = 256 * 4 * 2;
    std::vector<int> bits(N);
    std::vector<int16_t> tx_buffer;
    gen_bits(N, bits);
    ofdm(bits, tx_buffer, context.n, context.ncp, context.ps);

    int flags = 0;
    long long timeNs = 0;
    long timeoutUs = 0;

    while (!has_flag(context.flags, Flags::EXIT))
    {

        if (has_flag(context.flags, Flags::REINIT))
            reinit(context);
        if (has_flag(context.flags, apply))
            apply_runtime(context);
        if (has_flag(context.flags, Flags::REMODULATION))
        {
            gui::change_modulation(context, tx_buffer, bits);
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
        context.txbuffer = tx_buffer;

        if (has_flag(context.flags, Flags::SEND))
        {
            size_t total_samples = tx_buffer.size() / 2;
            size_t total_sent = 0;

            while (total_sent < total_samples)
            {
                void *buffs[] = { tx_buffer.data() + total_sent * 2 };

                send = context.sdr->writeStream(
                    context.txStream,
                    buffs,
                    total_samples - total_sent,
                    flags,
                    timeNs,
                    timeoutUs
                );

                metrics.push_back(send);
                if (metrics.size() > 1920 * 4)
                    metrics.erase(metrics.begin());

                if (send > 0)
                    total_sent += send;
                else
                    break;
            }

        }
    }
    deinit(&context);
    return 0;
}


int main()
{
    sdr_config_t sdr(
        "", 1920,
        3.84e6,
        826e6, 826e6,
        89.0, 25.0,
        true, false);
    int send = 0;
    sdr.n = 256;
    sdr.ncp = 16;
    sdr.ps = 253;
    // std::thread gui_thread(run_gui, std::ref(sdr), std::ref(send));

    std::thread sdr_thread(sdr_tx, std::ref(sdr), std::ref(send));
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    std::cout << " skdfsdf";
    run_gui(sdr, send);

    // GUI в main потоке

    // gui_thread.join();
    sdr_thread.join();

    return 0;
}
