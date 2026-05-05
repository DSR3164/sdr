#include "dsp.h"
#include "gui.h"

#include <thread>

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
        2e9, 2e9,
        89.0, 25.0,
        true, true
    );
    sdr.modulation_type = 4;
    sdr.flags |= Flags::APPLY_BANDWIDTH;
    int subcarrier_count = static_cast<int>(sdr.sample_rate / 15e3);
    SharedData_t data(sdr.buffer_size, subcarrier_count, 32, 25, 4);

    std::thread gui_thread(run_gui, std::ref(sdr), std::ref(data));
    std::thread sdr_thread(run_sdr, std::ref(sdr), std::ref(data));
    std::thread dsp_thread(run_dsp, std::ref(sdr), std::ref(data));

    gui_thread.join();
    sdr_thread.join();
    dsp_thread.join();

    return 0;
}
