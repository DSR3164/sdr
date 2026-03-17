#include <GL/glew.h>
#include <SDL2/SDL.h>

#include <SoapySDR/Device.hpp>
#include <SoapySDR/Formats.hpp>
#include "pluto_lib.h"
#include <cstring>
#include <fftw3.h>
#include <vector>
#include "gui.h"
#include "dsp_module.h"
#include "imgui.h"
#include <thread>

namespace gui
{
    void change_modulation(sdr_config_t &sdr_config, std::vector<int16_t> &tx_buffer, std::vector<int> &bits, SharedData_t &data, int ofdm_mod)
    {
        tx_buffer.clear();
        tx_buffer.reserve(1920 * 4);

        switch (sdr_config.modulation_type)
        {
        case 0:
            bpsk_3gpp(bits, tx_buffer);
            break;
        case 1:
            qpsk_3gpp(bits, tx_buffer);
            break;
        case 2:
            qam16_3gpp(bits, tx_buffer);
            break;
        case 3:
            qam16_3gpp_rrc(bits, tx_buffer);
            break;
        case 4:
            ofdm(bits, tx_buffer, data.ofdm_cfg.n_subcarriers, data.ofdm_cfg.n_cp, data.ofdm_cfg.pilot_spacing, ofdm_mod);
            break;
        default:
            break;
        }

        sdr_config.flags &= ~Flags::REMODULATION;
    }

    namespace
    {
        FFTWPlan &fftw_singleton(int size)
        {
            static FFTWPlan p{ size };
            return p;
        }

    }

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
            int idx = (i + maxim / 2) % maxim;
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
        for (int i = 0; i < x_hz.size(); ++i)
        {
            double f = ((double)i / x_hz.size() - 0.5) * Fs;
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
        static std::vector<std::complex<float>> x(gui::NFFT);
        static std::vector<float> mag_db(gui::NFFT);

        for (int n = 0; n < gui::NFFT; n++)
        {
            constexpr float inv = 1.0f / 32768.0f;
            float I = (float)iq_interleaved[2 * n + 0] * inv;
            float Q = (float)iq_interleaved[2 * n + 1] * inv;
            float w = hann(n, gui::NFFT);
            x[n] = std::complex<float>(I * w, Q * w);
        }

        fft_radix2_inplace(x);

        constexpr float eps = 1e-12f;
        for (int k = 0; k < gui::NFFT; k++)
        {
            uint8_t r, g, b;
            int ks = (k + gui::NFFT / 2) & (gui::NFFT - 1);
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
        std::vector<std::string> ofdm_modulations = { "BPSK", "QPSK", "QAM16", "QAM128" };
        static std::string preview_mod = modulations[context.modulation_type];
        static std::string preview_ofdm_mod = "";
        if (data.mod.ModulationType) preview_ofdm_mod = ofdm_modulations[data.ofdm_cfg.mod];
        else preview_ofdm_mod = ofdm_modulations[2];
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
            ImGui::Text("SDR Cycle: %.f", data.gui.timed);
            ImGui::Text("FPS: %.1f (%.3f ms)", io.Framerate, 1000.0f / io.Framerate);
            ImGui::Text("Index %d", data.dsp.max_index);
            ImGui::Checkbox("Debug", &data.gui.debug);
            ImGui::TreePop();
            ImGui::Spacing();
        }

        if (ImGui::TreeNode("SDR"))
        {
            bool changed_tx_g = ImGui::SliderFloat("TX Gain", &context.tx_gain, 0.0f, 89.0f, "%.3f");
            bool changed_rx_g = ImGui::SliderFloat("RX Gain", &context.rx_gain, 0.0f, 73.0f, "%.3f");
            bool changed_tx_f = ImGui::InputDouble("TX Frequency", &context.tx_carrier_freq, 10e3, 10e5, "%e");
            bool changed_rx_f = ImGui::InputDouble("RX Frequency", &context.rx_carrier_freq, 10e3, 10e5, "%e");
            ImGui::SliderFloat("Coefficient", &data.dsp.scale_coef, 0.0f, 2000.0f, "%.3f");
            ImGui::InputDouble("Gardner", &data.dsp.gardner_band, 1e-6, 1, "%e");
            ImGui::InputDouble("Costas", &data.dsp.costas_band, 1e-6, 1, "%e");
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
                ImGui::Checkbox("CP sync", &data.ofdm_cfg.symbol_sync);
                ImGui::SameLine();
                ImGui::Checkbox("PSS", &data.ofdm_cfg.pss);
                ImGui::SameLine();
                ImGui::Checkbox("CFO", &data.ofdm_cfg.cfo);
                ImGui::SameLine();
                ImGui::Checkbox("FFT", &data.ofdm_cfg.fft);
                ImGui::SameLine();
                ImGui::Checkbox("Equa", &data.ofdm_cfg.eq);
                if (ImGui::SliderInt("OFDM subcarriers", &data.ofdm_cfg.n_subcarriers, 4, std::round(context.sample_rate / 15e3)))
                    context.flags |= Flags::REMODULATION;
                if (ImGui::SliderInt("OFDM CP len", &data.ofdm_cfg.n_cp, 4, 64))
                    context.flags |= Flags::REMODULATION;
                if (ImGui::SliderInt("OFDM Pilot Spacing", &data.ofdm_cfg.pilot_spacing, 2, std::round(context.sample_rate / 15e3) - 3))
                    context.flags |= Flags::REMODULATION;
                ImGui::InputInt("OFDM Symbol Offset", &data.dsp.offset, 1, -1);
            }

            ImGui::TreePop();
            ImGui::Spacing();
        }

        return 1;
    }

} // namespace gui
