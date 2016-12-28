
#pragma once
#include <stdlib.h>
#include <stdint.h>

struct FPGAComm_Command {
	uint32_t address;
	uint32_t length;
	void *read_data;
	void const *write_data;
	//completion is allowed to be NULL
	void (*completion)(int result, struct FPGAComm_Command *command);
	//private fields
	uint8_t state;
};

#ifdef __cplusplus
extern "C" {
#endif

void FPGAComm_Setup();
void FPGAComm_ReadWriteCommand(struct FPGAComm_Command *command);
void FPGAComm_CopyToFPGA(uint32_t dest, void const *src, size_t n);
void FPGAComm_CopyFromFPGA(void *dest, uint32_t src, size_t n);
void FPGAComm_CopyFromToFPGA(void *dest, uint32_t fpga, void const *src,
			     size_t n);
void FPGAComm_SetIRQHandler(unsigned int num, void (*handler)(void *),
			    void *data);
void FPGAComm_EnableIRQs(unsigned int mask);
void FPGAComm_DisableIRQs(unsigned int mask);
  //these are non-blocking variants. user needs to setup command->completion as needed.
void FPGAComm_EnableIRQs_nb(unsigned int mask,
			    struct FPGAComm_Command *command);
void FPGAComm_DisableIRQs_nb(unsigned int mask,
			     struct FPGAComm_Command *command);

#ifdef __cplusplus
}
#endif
