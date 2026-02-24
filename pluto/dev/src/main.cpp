#include "pluto_lib.h"
#include <iostream>
#include "fstream"

using namespace std;

int main()
{
    int N = 128 * 10 * 2;
    vector<int> bits(N);
    vector<int16_t> tx_buffer;

    gen_bits(N, bits);
    ofdm(bits, tx_buffer, 128, 32, 125);

    std::fstream file("../pcm/ofdm2.pcm", std::ios::out | std::ios::binary);

    file.write(reinterpret_cast<char *>(tx_buffer.data()),
               tx_buffer.size() * sizeof(int16_t));
    file.close();
    return 0;
}