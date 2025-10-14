#include <SoapySDR/Device.h>   // Инициализация устройства
#include <SoapySDR/Formats.h>  // Типы данных, используемых для записи сэмплов
#include <stdio.h>             //printf
#include <stdlib.h>            //free
#include <stdint.h>
#include <complex.h>
#include <cstring>
#include <string.h>

int16_t *read_pcm(const char *filename, size_t *sample_count, int16_t *samples)
{
    FILE *file = fopen(filename, "rb");
    
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    printf("file_size = %ld\n", file_size);
    
    *sample_count = file_size / sizeof(int16_t);
    
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
    int carrier_freq = 999e6;
    // Параметры RX части
    SoapySDRDevice_setSampleRate(sdr, SOAPY_SDR_RX, 0, sample_rate);
    SoapySDRDevice_setFrequency(sdr, SOAPY_SDR_RX, 0, carrier_freq , NULL);
    
    // Параметры TX части
    SoapySDRDevice_setSampleRate(sdr, SOAPY_SDR_TX, 0, sample_rate);
    SoapySDRDevice_setFrequency(sdr, SOAPY_SDR_TX, 0, carrier_freq , NULL);
    
    // Инициализация количества каналов RX\\TX (в AdalmPluto он один, нулевой)
    size_t channel = 0;

    // Настройки усилителей на RX\\TX
    SoapySDRDevice_setGain(sdr, SOAPY_SDR_RX, channel, 10.0); // Чувствительность приемника
    SoapySDRDevice_setGain(sdr, SOAPY_SDR_TX, channel, -50.0);// Усиление передатчика

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

    // Выделяем память под буферы RX и TX
    int16_t tx_buff[2*tx_mtu * 10];
    int16_t rx_buffer[2*rx_mtu * 10];

    int buffer_size = 1920;
    long file_size = 3245278;
    int16_t *samples = (int16_t *)malloc(file_size);
    size_t sample_count = 0;
    samples = read_pcm("/home/plutoSDR/sdr/1.pcm", &sample_count, samples);
    printf("\nКоличество сэмплов: %ld\nКоличесвто буфферов: %ld\n", sample_count, sample_count/buffer_size);
    int16_t *tx_buffs[sample_count/buffer_size];
    for (int i = 0; i<((int)sample_count/buffer_size); i++)
    {
        memcpy(tx_buffs, samples + i * buffer_size, sizeof(int16_t)*buffer_size);
    }
    
    printf("Длина буферов: %ld\n", sizeof(tx_buffs)/sizeof(int16_t));

    // Количество итерация чтения из буфера
    size_t iteration_count = 10;
    long long last_time = 0;
    
    // FILE* file = fopen("../rx.pcm", "wb");
    // FILE* file1 = fopen("../tx.pcm", "wb");

    // Начинается работа с получением и отправкой сэмплов
    for (size_t buffers_read = 0; buffers_read < iteration_count; buffers_read++)
    {
        void *rx_buffs[] = {rx_buffer};
        int flags;        // flags set by receive operation
        long long timeNs; //timestamp for receive buffer
        long timeoutUs = 1000000;
        // считали буффер RX, записали его в rx_buffer
        int sr = SoapySDRDevice_readStream(sdr, rxStream, rx_buffs, rx_mtu, &flags, &timeNs, timeoutUs);
        
        // Смотрим на количество считаных сэмплов, времени прихода и разницы во времени с чтением прошлого буфера
        printf("Buffer: %lu - Samples: %i, Flags: %i, Time: %lli, TimeDiff: %lli\n", buffers_read, sr, flags, timeNs, timeNs - last_time);
        last_time = timeNs;

        // Переменная для времени отправки сэмплов относительно текущего приема
        long long tx_time = timeNs + (4 * 1000 * 1000); // на 4 [мс] в будущее

        // // Добавляем время, когда нужно передать блок tx_buff, через tx_time -наносекунд
        // for(size_t i = 0; i < 8; i++)
        // {
        //     uint8_t tx_time_byte = (tx_time >> (i * 8)) & 0xff;
        //     tx_buff[2 + i] = tx_time_byte << 4;
        // }

        // Здесь отправляем наш tx_buff массив
        
        if( (buffers_read==2) ){
            flags = SOAPY_SDR_HAS_TIME;
            int st = SoapySDRDevice_writeStream(sdr, txStream, (const void * const*)tx_buffs, tx_mtu, &flags, tx_time, timeoutUs);
            if ((size_t)st != tx_mtu)
            {
                printf("TX Failed: %i\n", st);
            }
        }
        
        // fwrite(rx_buffer, 2 * rx_mtu * sizeof(int16_t), 1, file);
        // fwrite(tx_buffs, 2 * rx_mtu * sizeof(int16_t), 1, file1);
    }

    // fclose(file);

    //stop streaming
    SoapySDRDevice_deactivateStream(sdr, rxStream, 0, 0);
    SoapySDRDevice_deactivateStream(sdr, txStream, 0, 0);

    //shutdown the stream
    SoapySDRDevice_closeStream(sdr, rxStream);
    SoapySDRDevice_closeStream(sdr, txStream);

    //cleanup device handle
    SoapySDRDevice_unmake(sdr);

    return 0;

    free(samples);
}
