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
        carrier + shift, carrier + shift,
        -10.0, 45.0);

    if (init(&sdr1) != 0)
    {
        printf("Initialization error\n");
        return -1;
    }

    int N = 192000;
    vector<int> bits(N);
    vector<int16_t> tx_buffer;
    vector<int16_t> rx_buffer(1920 * 2);

    // file_to_bits("", bits);
    gen_bits(N, bits);
    qam16_3gpp(bits, tx_buffer, true);

    // implement_barker(tx_buffer);

    void *tx_buffs[] = {tx_buffer.data()}; // Buffer for transmitting samples
    void *rx_buffs[] = {rx_buffer.data()};      // Buffer for transmitting samples
    FILE *file_rx;
    file_rx = fopen("../pcm/rxtest.pcm", "wb");
    if (strcmp(argv[2], "rx") == 0)
        file_rx = fopen("../pcm/rx.pcm", "wb");
    FILE *file_tx = fopen("../pcm/tx1.pcm", "wb");

    int flags = SOAPY_SDR_HAS_TIME;
    long long timeNs;
    long timeoutUs = 400000;
    int sr = 0;
    int st = 0;
    int count = 0;
    float buff_count = (tx_buffer.size() / (1920 * 2));
    cout << "Buffs: " << buff_count << endl;
    fwrite(tx_buffs[0], 2 * (int)buff_count*1920 * sizeof(int16_t), 1, file_tx);

    for (int k = 0; k < 15000000; k++)
    {
        tx_buffs[0] = static_cast<void *>(tx_buffer.data() + 1920 * 2 * (k < buff_count ? k : 0));
        sr = SoapySDRDevice_readStream(sdr1.sdr, sdr1.rxStream, rx_buffs, sdr1.buffer_size, &flags, &timeNs, timeoutUs);
        long long tx_time = timeNs + (4 * 1000 * 1000); // Schedule TX 4ms ahead
        if (strcmp(argv[2], "rx") != 0)
        {
            st = SoapySDRDevice_writeStream(sdr1.sdr, sdr1.txStream, (const void *const *)tx_buffs, sdr1.buffer_size, &flags, tx_time, timeoutUs);
            (void)st;
        }
        fwrite(rx_buffs[0], 2 * sr * sizeof(int16_t), 1, file_rx);
        fwrite(tx_buffs[0], 2 * sdr1.buffer_size * sizeof(int16_t), 1, file_tx);
        if (k % 520 == 0 && k != 0)
        {
            count += 1;
            cout << count << " second\t" << k << " buffs" << endl;
        }
    }
    fclose(file_rx);
    fclose(file_tx);
    deinit(&sdr1);
    return 0;
}