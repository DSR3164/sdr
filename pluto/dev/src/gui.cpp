#include <GL/glew.h>
#include <SDL2/SDL.h>

#include <iostream>
#include <thread>

#include "backends/imgui_impl_opengl3.h"
#include "backends/imgui_impl_sdl2.h"
#include "imgui.h"
#include "implot.h"
#include "implot3d.h"
#include "gui.h"
#include <SoapySDR/Device.hpp>
#include <SoapySDR/Formats.hpp>
#include <vector>

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
    static int current_rx_bandhwidth = 10;
    static int current_tx_bandhwidth = 10;
    static std::vector<float> values = {0.2e6, 1e6, 2e6, 3e6, 4e6, 5e6, 6e6, 7e6, 8e6, 9e6, 10e6};
    static SoapySDR::KwargsList list;
    static bool is_scanning = false;
    std::vector<std::string> modulations = {"BPSK", "QPSK", "QAM16", "QAM16 RRC"};
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

    if (ImGui::InputDouble("Sample Rate", &context.sample_rate, 0.5e6, 2e6, "%e"))
        context.flags |= Flags::APPLY_SAMPLE_RATE;
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
    ImGui::SameLine();
    bool send_enabled = (context.flags & Flags::SEND) != Flags::None;

    if (ImGui::Checkbox("Send", &send_enabled))
    {
        if (send_enabled)
            context.flags |= Flags::SEND;
        else
            context.flags &= ~Flags::SEND;
    }

    if (ImGui::Button("Reinit"))
        context.flags |= Flags::REINIT;

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
            std::vector<int16_t> temp_i(1920);
            std::vector<int16_t> temp_q(1920);
            for (int i = 0; i < 1920; ++i)
            {
                temp_i[i] = (context.buffer[2 * i]);
                temp_q[i] = (context.buffer[2 * i + 1]);
            }
            ImGui::Begin("Constellation", nullptr, ImGuiWindowFlags_MenuBar);
            if (ImGui::BeginMenuBar())
            {
                if (ImGui::BeginMenu("Config"))
                {
                    context_edit_window(context);
                    ImGui::EndMenu();
                }
                ImGui::EndMenuBar();
            }
            ImGui::Text("FPS: %.1f (%.3f ms)", io.Framerate, 1000.0f / io.Framerate);
            ImVec2 av = ImGui::GetContentRegionAvail();

            if (ImPlot::BeginPlot("just", ImVec2(av.x, av.y -= 200)))
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
                ImPlot::PlotScatter("Const", temp_i.data(), temp_q.data(), 1920);
                ImPlot::EndPlot();
            }
            if (ImPlot::BeginPlot("I:Q", ImGui::GetContentRegionAvail()))
            {
                ImPlot::SetupAxesLimits(0, 1920, -50, 50, ImPlotCond_Once);
                static bool reset_view = false;
                if (reset_view)
                {
                    ImPlot::SetupAxesLimits(0, 1920, -4096, 4096, ImPlotCond_Always);
                    reset_view = false;
                }

                if (ImPlot::BeginLegendPopup("I") or ImPlot::BeginLegendPopup("Q"))
                {
                    if (ImGui::Button("Reset view"))
                        reset_view = true;
                    ImPlot::EndLegendPopup();
                }
                ImPlot::PlotLine("I", temp_i.data(), 1920);
                ImPlot::PlotLine("Q", temp_q.data(), 1920);
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

    void *rx_buffs[] = {context.buffer.data()};
    void *tx_buffs[] = {tx_buffer.data()};

    int flags = SOAPY_SDR_HAS_TIME;
    long long timeNs;
    long timeoutUs = 400000;
    int k = 0;
    float buff_count = (tx_buffer.size() / (1920 * 2));

    while (!has_flag(context.flags, Flags::EXIT))
    {

        if (has_flag(context.flags, Flags::REINIT))
            reinit(context);
        if (has_flag(context.flags, apply))
            apply_runtime(context);
        if (has_flag(context.flags, Flags::REMODULATION))
        {
            gui::change_modulation(&context, tx_buffer);
            k = 0;
            buff_count = (float)(tx_buffer.size() / (context.buffer_size * 2));
        }
        context.sdr->readStream(context.rxStream, rx_buffs, context.buffer_size, flags, timeNs, timeoutUs);
        tx_buffs[0] = static_cast<void *>(tx_buffer.data() + 1920 * 2 * (k < buff_count ? k : 0));

        if (has_flag(context.flags, Flags::SEND))
            context.sdr->writeStream(context.txStream, (const void *const *)tx_buffs, context.buffer_size, flags, timeNs + (4 * 1000 * 1000), timeoutUs);
        k += 1;
    }
    deinit(&context);
    return 0;
}

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;
    sdr_config_t sdr(
        "", 1920,
        2e6,
        9.4702e8, 9.4702e8,
        50.0, 25.0);

    std::thread gui_thread(run_gui, std::ref(sdr));
    std::thread sdr_thread(sdr_backend, std::ref(sdr));

    gui_thread.join();
    sdr_thread.join();

    return 0;
}