#include "pluto_lib.h"

int main(int argc, char *argv[])
{
    (void)argc;
    double shift = 0e3;
    double carrier = 734750e3; // real = +-734747e3
    sdr_config_t sdr1(
        argv[1], 1920,
        // tx, rx
        1e6, 1e6,
        carrier+shift, carrier+shift,
        -10.0, 55.0);

    if (init(&sdr1) != 0)
    {
        printf("Initialization error\n");
        return -1;
    }

    int N = 1920;
    vector<int> bits(N);
    vector<int16_t> qpsk_tx_buffer(N * 5);
    vector<int16_t> rx_buffer(1920 * 2);

    gen_bits(N, bits);
    qpsk_3gpp(bits, qpsk_tx_buffer, true);

    implement_barker(qpsk_tx_buffer);

    void *tx_buffs[] = {qpsk_tx_buffer.data()}; // Buffer for transmitting samples
    void *rx_buffs[] = {rx_buffer.data()};      // Buffer for transmitting samples
    FILE *file_rx;
    file_rx = fopen("/home/plutoSDR/sdr/pluto/dev/rxtest.pcm", "wb");
    if (strcmp(argv[2], "rx") == 0)
        file_rx = fopen("/home/plutoSDR/sdr/pluto/dev/rx.pcm", "wb");
    FILE *file_tx = fopen("/home/plutoSDR/sdr/pluto/dev/tx1.pcm", "wb");

    int flags = SOAPY_SDR_HAS_TIME;
    long long timeNs;
    long timeoutUs = 400000;
    int sr = 0;
    int st = 0;

    for (int k = 0; k < 100000; k++)
    {
        sr = SoapySDRDevice_readStream(sdr1.sdr, sdr1.rxStream, rx_buffs, sdr1.buffer_size, &flags, &timeNs, timeoutUs);
        long long tx_time = timeNs + (4 * 1000 * 1000); // Schedule TX 4ms ahead
        if (strcmp(argv[2], "rx") != 0)
        {
            st = SoapySDRDevice_writeStream(sdr1.sdr, sdr1.txStream, (const void *const *)tx_buffs, sdr1.buffer_size, &flags, tx_time, timeoutUs);
            (void)st;
        }
        fwrite(rx_buffs[0], 2 * sr * sizeof(int16_t), 1, file_rx);
        fwrite(tx_buffs[0], 2 * sdr1.buffer_size * sizeof(int16_t), 1, file_tx);
    }
    fclose(file_rx);
    fclose(file_tx);
    deinit(&sdr1);
    return 0;
}