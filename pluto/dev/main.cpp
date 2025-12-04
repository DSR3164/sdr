#include "pluto_lib.h"

int main(int argc, char *argv[])
{

    auto [sdr, rxStream, txStream, rx_mtu, tx_mtu] = init(argv[1]);
    if (!sdr)
    {
        printf("Initialization error\n");
        return -1;
    }

    int N = 1920;
    vector<int> bits(N);

    for(int i = 0; i < N; ++i)
    {
        bits[i] = rand() % 2;
    }
    vector<int16_t> qpsk_tx_buffer(1920*2);
    vector<int16_t> rx_buffer(1920*2);

    qpsk(bits, qpsk_tx_buffer);

    for(size_t i = 0; i < 2; i++)
    {
        qpsk_tx_buffer[0 + i] = 0xffff;
        // 8 x timestamp words
        qpsk_tx_buffer[10 + i] = 0xffff;
    }

    void *tx_buffs[] = {qpsk_tx_buffer.data()}; // Buffer for transmitting samples
    void *rx_buffs[] = {rx_buffer.data()}; // Buffer for transmitting samples
    FILE *file_rx;
    if (strcmp(argv[1], "usb:1.17.5")){
        file_rx = fopen("/home/plutoSDR/sdr/pluto/dev/rx1.pcm", "wb");
    }
    else
    {
        file_rx = fopen("/home/plutoSDR/sdr/pluto/dev/rx.pcm", "wb");
    }
    FILE *file_tx = fopen("/home/plutoSDR/sdr/pluto/dev/tx1.pcm", "wb");

    int flags;
    long long timeNs;
    long long last_time = 0;
    long timeoutUs = 400000;
    flags = SOAPY_SDR_HAS_TIME;

    fwrite(tx_buffs[0], 2 * rx_mtu * sizeof(int16_t), 1, file_tx);

    int k = 150;

    while (k > 1)
    {
        
        long long tx_time = timeNs + (4 * 1000 * 1000); // Schedule TX 4ms ahead
        
        if (strcmp(argv[1], "usb:1.17.5")){
            int sr = SoapySDRDevice_readStream(sdr, rxStream, rx_buffs, rx_mtu, &flags, &timeNs, timeoutUs);
            fwrite(rx_buffs[0], 2 * rx_mtu * sizeof(int16_t), 1, file_rx);
        }
        else{
            if (k > 147){
                int st = SoapySDRDevice_writeStream(sdr, txStream, (const void * const*)tx_buffs, tx_mtu, &flags, tx_time, timeoutUs);
            }
        }

        printf("Flags: %i, Time: %lli, TimeDiff: %lli\n", flags, timeNs, (timeNs - last_time) * (last_time > 0));
        last_time = tx_time;
        k-=1;
    }

    fclose(file_rx);
    fclose(file_tx);
    deinit(sdr, rxStream, txStream);
    return 0;
}
