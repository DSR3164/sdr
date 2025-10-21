#include <SoapySDR/Device.h>   // Инициализация устройства
#include <SoapySDR/Formats.h>  // Типы данных, используемых для записи сэмплов
#include <stdio.h>             //printf
#include <stdlib.h>            //free
#include <stdint.h>
#include <complex.h>
#include <cstring>
#include <string.h>

int16_t *read_pcm(const char *filename, size_t *sample_count)
{
    FILE *file = fopen(filename, "rb");
    
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


int main(void)
{
    SoapySDRKwargs args = {};
    SoapySDRKwargs_set(&args, "driver", "plutosdr");        // Говорим какой Тип устройства 
    if (1) {
        SoapySDRKwargs_set(&args, "uri", "usb:");           // Способ обмена сэмплами (USB)
    } else {
        SoapySDRKwargs_set(&args, "uri", "ip:192.168.2.1"); // Или по IP-адресу
    }
    SoapySDRKwargs_set(&args, "direct", "1");               // 
    SoapySDRKwargs_set(&args, "timestamp_every", "1920");   // Размер буфера + временные метки
    SoapySDRKwargs_set(&args, "loopback", "0");             // Используем антенны или нет
    SoapySDRDevice *sdr = SoapySDRDevice_make(&args);       // Инициализация
    SoapySDRKwargs_clear(&args);
    
    int sample_rate = 1e6;
    int carrier_freq = 800e6;
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

    char filename[512];
    char pwd[] = "/home/plutoSDR";
    snprintf(filename, sizeof(filename), "%s/sdr/pluto/dev/1.pcm", pwd);

    size_t sample_count = 0;
    int16_t *samples = read_pcm(filename, &sample_count);
    if (!samples) return 1;
    int buffs_size = tx_mtu;
    int buffs_count = (sample_count/buffs_size);
    int remainder = sample_count - buffs_count * buffs_size;
    int full_size = buffs_count + (int)(bool)remainder;
    printf("Количество сэмплов: %ld\nКоличество буферов: %ld == %d по %d + %d\n", sample_count, full_size, buffs_count, buffs_size, remainder);

    // Количество итерация чтения из буфера
    size_t iteration_count = 10;
    long long last_time = 0;
    
    FILE* file = fopen("../2.pcm", "wb");
    int16_t *rx_buffer = (int16_t *)malloc((rx_mtu * 2 * sizeof(int16_t)));

    void *rx_buffs[] = {rx_buffer};
    int flags;        // flags set by receive operation
    long long timeNs; //timestamp for receive buffer
    long timeoutUs = 400000;
    
    flags = SOAPY_SDR_HAS_TIME;
    for (size_t b = 0; b < full_size; b++)
    {
        size_t current_size = (b == full_size - 1 && remainder > 0) ? remainder : buffs_size;
        const void *one_buff = samples + b * buffs_size * 2;
        
        int sr = SoapySDRDevice_readStream(sdr, rxStream, rx_buffs, rx_mtu, &flags, &timeNs, timeoutUs);
        
        long long tx_time = timeNs + (4 * 1000 * 1000); // на 4 [мс] в будущее
        
        int st = SoapySDRDevice_writeStream(sdr, txStream, &one_buff, tx_mtu, &flags, tx_time, timeoutUs);
        
        if (st < 0)
        printf("TX Failed on buffer %zu: %i\n", b, st);
        fwrite(rx_buffer, 2 * rx_mtu * sizeof(int16_t), 1, file);
        last_time = tx_time;
        printf("Buffer: %lu - Samples: %i, Flags: %i, Time: %lli, TimeDiff: %lli\n", b, sr, flags, timeNs, (timeNs - last_time) * (last_time > 0));
    }

    fclose(file);

    //stop streaming
    SoapySDRDevice_deactivateStream(sdr, rxStream, 0, 0);
    SoapySDRDevice_deactivateStream(sdr, txStream, 0, 0);

    //shutdown the stream
    SoapySDRDevice_closeStream(sdr, rxStream);
    SoapySDRDevice_closeStream(sdr, txStream);

    //cleanup device handle
    SoapySDRDevice_unmake(sdr);

    free(samples);
    return 0;
}
