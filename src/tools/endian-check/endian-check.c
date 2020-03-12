#include <stdio.h>
#include <stdint.h>
#include <endian.h>

/*
 * This is a temporary tool to test some endianness properties on 
 * different (virtual) systems and architecture
 */

void endian_check() {
    uint16_t assigned = 123;
    uint16_t after_htobe16 = htobe16(assigned);
    uint16_t after_htons = htons(assigned);

    printf("Assigned value:\t%d\n", assigned);
    printf("After htobe16:\t%d\n", after_htobe16);
    printf("After htons:\t%d\n", after_htons);
    printf("\n");
    
    if (assigned != after_htobe16) {
        printf("System is LITTLE-ENDIAN\n");
    } else {
        printf("System is BIG-ENDIAN\n");
    }
}


int main(int argc, char** argv) {
    endian_check();
    return 0;
}