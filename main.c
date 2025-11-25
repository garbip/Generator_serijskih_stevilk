#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <stdint.h>
#include <inttypes.h>

int main(void) {
    srand((unsigned)time(NULL));
    const char hex_chars[] = "0123456789abcdef";
    char hex32[33];

    for (int i = 0; i < 32; ++i) {
        hex32[i] = hex_chars[rand() & 0xF]; 
    }

    // verzija 4
    hex32[12] = '4';
    // set the variant to 10xx (values 8,9,a,b)  to nism fix, ne najdem nič preveč pametnega. Uglavnem variante 10xx: 0x8 (0b1000), 0x9 (0b1001), 0xA (0b1010), 0xB (0b1011)
    hex32[16] = hex_chars[(rand() & 0x3) | 0x8]; // variant 10xx

    hex32[32] = '\0';

    // dodamo -
    char uuid[37];
    int out = 0;
    for (int i = 0; i < 32; ++i) {
        if (out == 8 || out == 13 || out == 18 || out == 23) {
            uuid[out++] = '-';
        }
        uuid[out++] = hex32[i];
    }
    uuid[out] = '\0';


    printf("%s\n", hex32);
    printf("%s\n", uuid);
    

uint32_t crc32_compute(const char *data) {
    uint32_t crc = 0xFFFFFFFFu;
    while (*data) {
        crc ^= (uint8_t)*data++;
        for (int i = 0; i < 8; ++i) {
            crc = (crc >> 1) ^ (0xEDB88320u & (-(int)(crc & 1)));
        }
    }
    return crc ^ 0xFFFFFFFFu;
}

uint32_t crc = crc32_compute(uuid);
printf("CRC32(uuid)=0x%08" PRIx32 "\n", crc);
printf("%s 0x%8 \n" ,uuid,  crc); 
return 0;





}
