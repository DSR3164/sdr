#include <SoapySDR/Device.h>   // Инициализация устройства
#include <SoapySDR/Formats.h>  // Типы данных, используемых для записи сэмплов
#include <stdio.h>             //printf
#include <stdlib.h>            //free
#include <stdint.h>
#include <complex.h>
#include <cstring>
#include <string.h>
#include <random>
#include <iostream>

using namespace std;
using cp = complex<double>;
using ci = complex<int16_t>;

void mapper_b(const vector<int> &bits, vector<cp> &symbols)
{
    for (size_t i = 0; i < bits.size(); ++i)
        symbols[i] = cp(bits[i] * -2.0 + 1.0, 0.0);
}

void upsample(const vector<cp> &symbols, vector<cp> &upsampled, int up = 10)
{
    if(upsampled.size() < symbols.size() * up)
    {
        printf("Ошибка: недостаточный размер вектора для апсемплинга!\n");
        return;
    }
    fill(upsampled.begin(), upsampled.end(), cp(0, 0));

    for (size_t i = 0; i < symbols.size(); ++i)
        upsampled[i * up] = symbols[i];
}

void filter(const vector<cp> &a, const vector<double> &b, vector<int> &y)
{
    const int nb = b.size();
    const int na = a.size();

    y.assign(na, 0);

    for (int n = 0; n < na; ++n)
    {
        int acc = 0;
        for (int m = 0; m < nb; ++m)
        {
            if (n - m >= 0)
                acc += a[n - m].real() * b[m];
        }
        y[n] = acc;
    }
}

int16_t *read_pcm(const char *filename, size_t *sample_count)
{
    FILE *file = fopen(filename, "rb");
    if(!file) {
        printf("Ошибка чтения файла\n");
        return NULL;
    }
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    printf("file_size = %ld\n", file_size);
    
    *sample_count = file_size / sizeof(int16_t);
    int16_t *samples = (int16_t *)malloc(file_size);
    size_t sf = fread(samples, sizeof(int16_t), *sample_count, file);
    
    if (sf == 0){
        printf("file %s empty!", filename);
    }
    
    fclose(file);
    
    return samples;
}

tuple<SoapySDRDevice*, SoapySDRStream*, SoapySDRStream*, size_t, size_t> init(int sample_rate = 1e6, int carrier_freq = 800e6, bool usb_or_ip = 1)
{
    SoapySDRKwargs args = {};
    SoapySDRKwargs_set(&args, "driver", "plutosdr");        // Говорим какой Тип устройства 
    if (usb_or_ip) {
        SoapySDRKwargs_set(&args, "uri", "usb:");           // Способ обмена сэмплами (USB)
    } else {
        SoapySDRKwargs_set(&args, "uri", "ip:192.168.2.1"); // Или по IP-адресу
    }
    SoapySDRKwargs_set(&args, "direct", "1");               // 
    SoapySDRKwargs_set(&args, "timestamp_every", "1920");   // Размер буфера + временные метки
    SoapySDRKwargs_set(&args, "loopback", "0");             // Используем антенны или нет
    SoapySDRDevice *sdr = SoapySDRDevice_make(&args);       // Инициализация
    SoapySDRKwargs_clear(&args);

    // Параметры RX части
    SoapySDRDevice_setSampleRate(sdr, SOAPY_SDR_RX, 0, sample_rate);
    SoapySDRDevice_setFrequency(sdr, SOAPY_SDR_RX, 0, carrier_freq , NULL);
    
    // Параметры TX части
    SoapySDRDevice_setSampleRate(sdr, SOAPY_SDR_TX, 0, sample_rate);
    SoapySDRDevice_setFrequency(sdr, SOAPY_SDR_TX, 0, carrier_freq , NULL);
    
    // Инициализация количества каналов RX\\TX (в AdalmPluto он один, нулевой)
    size_t channel = 0;

    // Настройки усилителей на RX\\TX
    SoapySDRDevice_setGain(sdr, SOAPY_SDR_RX, channel, 40.0); // Чувствительность приемника
    SoapySDRDevice_setGain(sdr, SOAPY_SDR_TX, channel, -7.0);// Усиление передатчика

    size_t numchun = 0;
    size_t channels[] = {0};
    // Формирование потоков для передачи и приема сэмплов
    SoapySDRStream *rxStream = SoapySDRDevice_setupStream(sdr, SOAPY_SDR_RX, SOAPY_SDR_CS16, channels, numchun, NULL);
    SoapySDRStream *txStream = SoapySDRDevice_setupStream(sdr, SOAPY_SDR_TX, SOAPY_SDR_CS16, channels, numchun, NULL);

    SoapySDRDevice_activateStream(sdr, rxStream, 0, 0, 0); //start streaming
    SoapySDRDevice_activateStream(sdr, txStream, 0, 0, 0); //start streaming

    // Получение MTU (Maximum Transmission Unit), в нашем случае - размер буферов. 
    size_t rx_mtu = SoapySDRDevice_getStreamMTU(sdr, rxStream);
    size_t tx_mtu = SoapySDRDevice_getStreamMTU(sdr, txStream);

    return make_tuple(sdr, rxStream, txStream, rx_mtu, tx_mtu);
}

void deinit(SoapySDRDevice *sdr, SoapySDRStream *rxStream, SoapySDRStream *txStream)
{
    //stop streaming
    SoapySDRDevice_deactivateStream(sdr, rxStream, 0, 0);
    SoapySDRDevice_deactivateStream(sdr, txStream, 0, 0);

    //shutdown the stream
    SoapySDRDevice_closeStream(sdr, rxStream);
    SoapySDRDevice_closeStream(sdr, txStream);

    //cleanup device handle
    SoapySDRDevice_unmake(sdr);
}

FILE* output_pcm()
{
    char repo_path[128];
    if (getenv("USER") && strcmp(getenv("USER"), "excalibur") == 0)
        strcpy(repo_path, "/home/excalibur/code");
    else if (getenv("HOME"))
        strcpy(repo_path, getenv("HOME"));
    char filename_out[64];
    printf("Введите имя выходного файла: ");
    scanf("%63s", filename_out);
    char fullpath[1024];
    snprintf(fullpath, sizeof(fullpath), "%s/sdr/pluto/dev/%s.pcm", repo_path, filename_out);
    printf("Output file: %s\n", fullpath);
    FILE* file = fopen(fullpath, "wb");

    return file;
}

int main(void)
{
    
    auto [sdr, rxStream, txStream, rx_mtu, tx_mtu] = init();
    if (!sdr) {
        printf("Ошибка инициализации PlutoSDR!\n");
        return -1;
    }

    const int up = 10;
    vector<int> bits = {1, 0, 0, 1, 0, 1, 0, 0, 1, 0};
    vector<cp> symbols(bits.size());
    vector<cp> upsampled(bits.size() * up);
    vector<int> signal(bits.size() * up);
    vector<double> b(5, 1.0);

    mapper_b(bits, symbols);
    upsample(symbols, upsampled, up);
    filter(upsampled, b, signal);
    
    vector<int16_t> buffer(signal.size()*2);
    vector<int16_t> rx_vec(rx_mtu * 2);

    for (int i = 0; i < (int)buffer.size() ; i+=2)
    {
        buffer[i] = signal[i/2] << 2;
        buffer[i+1] = 0 << 2;
    }

    const void *tx_buffs[] = {buffer.data()}; // Буфер для передачи сэмплов
    void *rx_buffs[] = {rx_vec.data()}; // Буфер для приема сэмплов
    
    FILE* output_file = output_pcm();
    
    int flags;
    long long timeNs;
    long long last_time = 0;
    long timeoutUs = 400000;
    flags = SOAPY_SDR_HAS_TIME;

    for (size_t b = 0; b < 10; b++)
    {
        int sr = SoapySDRDevice_readStream(sdr, rxStream, rx_buffs, rx_mtu, &flags, &timeNs, timeoutUs);
        
        long long tx_time = timeNs + (4 * 1000 * 1000); // на 4 [мс] в будущее
        if (b == 3)
        {
            int st = SoapySDRDevice_writeStream(sdr, txStream, tx_buffs, tx_mtu, &flags, tx_time, timeoutUs);
            
            if (st < 0)
            printf("TX Failed on buffer %zu: %i\n", b, st);
            printf("Buffer: %lu - Samples: %i, Flags: %i, Time: %lli, TimeDiff: %lli\n", b, sr, flags, timeNs, (timeNs - last_time) * (last_time > 0));
        }
        fwrite(rx_vec.data(), 2 * sr * sizeof(int16_t), 1, output_file);
        last_time = tx_time;
    }

    fclose(output_file);
    deinit(sdr, rxStream, txStream);
    return 0;
}
