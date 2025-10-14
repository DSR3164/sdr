#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <cstring>
#include <chrono>
#include <iostream>
#include <csignal>

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

int main(void){
    char filename[512];
    snprintf(filename, sizeof(filename), "%s/sdr/pluto/dev/1.pcm", getenv("HOME"));
    signal(SIGSEGV, [](int){ fprintf(stderr, "Segmentation fault caught\n"); exit(1); });
    
    auto start = std::chrono::high_resolution_clock::now();
    
    size_t sample_count = 0;
    int16_t *samples = read_pcm(filename, &sample_count);
    if (!samples) return 1;
    int buffs_size = 1920;
    int buffs_count = (sample_count/buffs_size);
    int c = 0;
    int remainder = sample_count - buffs_count * buffs_size;
    printf("Количество сэмплов: %ld\nКоличество буферов: ld == %d по %d + %d\n", sample_count, (int)(buffs_count + (int)(remainder > 0)), buffs_count, buffs_size, remainder);
    int16_t **tx_buffs = (int16_t **)malloc(sizeof(int16_t*) * (int)(buffs_count + (int)(remainder > 0)));
    FILE* file = fopen("../../2.pcm", "wb");
    for (int i = 0; i < (buffs_count + (int)(bool)remainder); i++)
    {
        c++;
        size_t current_size = (i == (buffs_count + (int)(bool)remainder) - 1 && remainder > 0) ? remainder : buffs_size;
        tx_buffs[i] = (int16_t *)malloc(sizeof(int16_t) * current_size);
        memcpy(tx_buffs[i], samples + i * buffs_size, sizeof(int16_t)*current_size);
        fwrite(tx_buffs[i], sizeof(int16_t) * current_size, 1, file);
    }
    printf("Остаток в последнем буфере: %d\n", remainder);
    printf("Количество циклов: %d\n", c);

    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> duration = end - start;
    std::cout << "Время выполнения: " << duration.count() << " мс" << std::endl;
    
    fclose(file);
    
    return 0;
}