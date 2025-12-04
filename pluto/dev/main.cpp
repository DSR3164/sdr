#include "pluto_lib.h"

int main(int argc, char *argv[])
{

    sdr_config_t sdr1(
        argv[1], 1920,
        1e6, 1e6,
        800e6, 800e6,
        50.0, -7.0);

    if (init(&sdr1) != 0)
    {
        printf("Initialization error\n");
        return -1;
    }

    int N = 1920;
    vector<int> bits(N);

    for (int i = 0; i < N; ++i)
    {
        bits[i] = rand() % 2;
    }
    vector<int16_t> qpsk_tx_buffer(1920 * 2);
    vector<int16_t> rx_buffer(1920 * 2);

    qpsk(bits, qpsk_tx_buffer);

    void *tx_buffs[] = {qpsk_tx_buffer.data()}; // Buffer for transmitting samples
    void *rx_buffs[] = {rx_buffer.data()};      // Buffer for transmitting samples
    FILE *file_rx;
    if (strcmp(argv[1], "usb:1.17.5"))
    {
        file_rx = fopen("/home/plutoSDR/sdr/pluto/dev/rx1.pcm", "wb");
    }
    else
    {
        file_rx = fopen("/home/plutoSDR/sdr/pluto/dev/rx.pcm", "wb");
    }
    FILE *file_tx = fopen("/home/plutoSDR/sdr/pluto/dev/tx1.pcm", "wb");

    int flags = SOAPY_SDR_HAS_TIME;
    long long timeNs;
    long long last_time = 0;
    long timeoutUs = 400000;
    int sr = 0;
    int st = 0;

    fwrite(tx_buffs[0], 2 * sdr1.buffer_size * sizeof(int16_t), 1, file_tx);

    int k = 150;

    while (k > 1)
    {

        long long tx_time = timeNs + (4 * 1000 * 1000); // Schedule TX 4ms ahead

        if (strcmp(argv[1], "usb:1.17.5"))
        {
            sr = SoapySDRDevice_readStream(sdr1.sdr, sdr1.rxStream, rx_buffs, sdr1.buffer_size, &flags, &timeNs, timeoutUs);
            fwrite(rx_buffs[0], 2 * sr * sizeof(int16_t), 1, file_rx);
        }
        else
        {
            if (k > 147)
            {
                st = SoapySDRDevice_writeStream(sdr1.sdr, sdr1.txStream, (const void *const *)tx_buffs, sdr1.buffer_size, &flags, tx_time, timeoutUs);
                (void)st;
            }
        }

        printf("Buffer: %u - Samples: %i, Flags: %i, Time: %lli, TimeDiff: %lli\n", k, sr, flags, timeNs, (timeNs - last_time) * (last_time > 0));
        last_time = tx_time;
        k -= 1;
    }

    fclose(file_rx);
    fclose(file_tx);
    deinit(&sdr1);
    return 0;
}
