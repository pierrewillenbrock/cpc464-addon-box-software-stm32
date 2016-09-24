
#pragma once
#include <stdlib.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void FPGAComm_Setup();
void FPGAComm_CopyToFPGA(uint16_t dest, void const *src, size_t n);
void FPGAComm_CopyFromFPGA(void *dest, uint16_t src, size_t n);
void FPGAComm_CopyFromToFPGA(void *dest, uint16_t fpga, void const *src, size_t n);

#ifdef __cplusplus
}
#endif
