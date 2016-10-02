#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <string>
#include <sstream>
#include <unordered_map>

#include "stm32f4xx_gpio.h"
#include "stm32f4xx_rcc.h"
#include "stm32f4xx_flash.h"
#include "fpga_comm.h"
#include "timer.h"
#include "sdcard.h"
#include "fat.h"
#include "vfs.hpp"

#define LEDR_PIN GPIO_Pin_3
#define LEDG_PIN GPIO_Pin_2
#define LEDB_PIN GPIO_Pin_1
#define LED_GPIO GPIOA
#define LED_RCC_FUNC RCC_AHB1PeriphClockCmd
#define LED_RCC RCC_AHB1Periph_GPIOA

static void LED_Setup() {
	LED_RCC_FUNC(LED_RCC, ENABLE);
	GPIO_InitTypeDef gpio_init;
	GPIO_StructInit(&gpio_init);
	gpio_init.GPIO_Pin = LEDR_PIN | LEDG_PIN | LEDB_PIN;
	gpio_init.GPIO_Speed = GPIO_Speed_2MHz;
	gpio_init.GPIO_Mode = GPIO_Mode_OUT;
	GPIO_Init(LED_GPIO, &gpio_init);
	GPIO_SetBits(LED_GPIO, LEDR_PIN | LEDG_PIN | LEDB_PIN);
}

#undef HSE_STARTUP_TIMEOUT
#define HSE_STARTUP_TIMEOUT 1000000
#define PLL_STARTUP_TIMEOUT 1000
#define SYSCLK_CHANGE_TIMEOUT 1000

static ErrorStatus WaitForHSEStartUp(void)
{
	uint32_t startupcounter = 0;
	ErrorStatus status = ERROR;
	FlagStatus pllstatus = RESET;
	do
	{
		pllstatus = RCC_GetFlagStatus(RCC_FLAG_HSERDY);
		startupcounter++;
	} while((startupcounter != HSE_STARTUP_TIMEOUT) && (pllstatus == RESET));

	if (RCC_GetFlagStatus(RCC_FLAG_HSERDY) != RESET)
		status = SUCCESS;
	else
		status = ERROR;
	return (status);
}

static ErrorStatus WaitForPLLStartUp(void)
{
	uint32_t startupcounter = 0;
	ErrorStatus status = ERROR;
	FlagStatus pllstatus = RESET;
	do
	{
		pllstatus = RCC_GetFlagStatus(RCC_FLAG_PLLRDY);
		startupcounter++;
	} while((startupcounter != PLL_STARTUP_TIMEOUT) && (pllstatus == RESET));

	if (RCC_GetFlagStatus(RCC_FLAG_PLLRDY) != RESET)
		status = SUCCESS;
	else
		status = ERROR;
	return (status);
}

static ErrorStatus WaitForSYSCLKChange(uint8_t source)
{
	uint32_t startupcounter = 0;
	ErrorStatus status = ERROR;
	uint8_t sysclksource;
	do
	{
		sysclksource = RCC_GetSYSCLKSource() >> 2;
		startupcounter++;
	} while((startupcounter != SYSCLK_CHANGE_TIMEOUT) && (sysclksource != source));

	if ((RCC_GetSYSCLKSource() >> 2) == source)
		status = SUCCESS;
	else
		status = ERROR;
	return (status);
}

void SysClk_Setup() {
	RCC_HSEConfig(RCC_HSE_Bypass);
	if (WaitForHSEStartUp() != SUCCESS) {
		GPIO_ResetBits(LED_GPIO, LEDB_PIN | LEDG_PIN);
		GPIO_SetBits(LED_GPIO, LEDR_PIN);
		while(1) {}
	}

	//clock: /M => PLL_INPUT, PLL_INPUT*N => VCO_OUT,
	//       VCO_OUT/P => SYSCLK, VCO_OUT/Q => PLL48CLK
	//PLL_INPUT: 0.95 - 2.1 MHz => 16MHz / 16 = 1MHz
	//VCO_OUT: 100-432 MHz
	//SYSCLK: max 168 MHz
	//PLL48CLK: exactly 48MHz
	//VCO_OUT/Q = 48MHz
	//VCO_OUT/P = 168MHz
	//VCO_OUT = 336
	//N: 192 - 432 => 336
	//P: 2,4,6,8   => 2 => 2SYSCLK = 168
	//Q: 4-16      => 7 => PLL48CLK = 48
	RCC_PLLConfig(RCC_PLLSource_HSE, 16, 336, 2, 7);
	RCC_PLLCmd(ENABLE);
	if (WaitForPLLStartUp() != SUCCESS) {
		GPIO_ResetBits(LED_GPIO, LEDB_PIN | LEDG_PIN);
		GPIO_SetBits(LED_GPIO, LEDR_PIN);
		while(1) {}
	}

	//need 5 WaitStates
	FLASH_SetLatency(FLASH_Latency_5);
	FLASH_PrefetchBufferCmd(ENABLE);
	FLASH_InstructionCacheCmd(ENABLE);
	FLASH_DataCacheCmd(ENABLE);

	RCC_SYSCLKConfig(RCC_SYSCLKSource_PLLCLK);
	if (WaitForSYSCLKChange(RCC_SYSCLKSource_PLLCLK) != SUCCESS) {
		GPIO_ResetBits(LED_GPIO, LEDB_PIN | LEDG_PIN);
		GPIO_SetBits(LED_GPIO, LEDR_PIN);
		while(1) {}
	}

	RCC_HSICmd(DISABLE);
}


int initROM() {
	//first, try to find the ROM file
	DIR *dir = opendir("/media");
	if (!dir)
		return -1;
	struct dirent *dent;
	int fd = -1;
	while(fd == -1 && (dent = readdir(dir))) {
		if((dent->d_name[0] == '.' && dent->d_name[1] == '\0') ||
		   (dent->d_name[0] == '.' && dent->d_name[1] == '.' && dent->d_name[2] == '\0'))
			continue;
		std::string path("/media/");
		path += dent->d_name;
		path += "/amsdos.rom";
		fd = open(path.c_str(), O_RDONLY);
	}
	closedir(dir);
	if (fd == -1)
		return -1;
	char buf1[1024];
	char buf2[1024];
	uint16_t addr = 0x0000; //base address of the ROM
	while(addr < 0x4000) {
		size_t n = sizeof(buf1);
		if (addr + n > 0x4000)
			n = 0x4000 - addr;
		int res = read(fd, buf1, n);
		if (res < 0) {
			GPIO_ResetBits(LED_GPIO, LEDB_PIN | LEDG_PIN);
			GPIO_SetBits(LED_GPIO, LEDR_PIN);
			while(1) {}
		}
		if (res == 0)
			break;
		FPGAComm_CopyToFPGA(addr, buf1, res);
		addr += res;
	}
	lseek(fd, 0, SEEK_SET);
	addr = 0x0000; //base address of the ROM
	while(addr < 0x4000) {
		size_t n = sizeof(buf1);
		if (addr + n > 0x4000)
			n = 0x4000 - addr;
		int res = read(fd, buf1, n);
		if (res < 0) {
			GPIO_ResetBits(LED_GPIO, LEDB_PIN | LEDG_PIN);
			GPIO_SetBits(LED_GPIO, LEDR_PIN);
			while(1) {}
		}
		if (res == 0)
			break;
		FPGAComm_CopyFromFPGA(buf2, addr, res);
		assert(memcmp(buf1, buf2, res) == 0);
		addr += res;
	}
	close(fd);
	//enable here or communicate to main so it can also "fail" there?

	return 0;
}

int main()
{
	LED_Setup();
	SysClk_Setup();
	Timer_Setup();
	VFS_Setup();

	FPGAComm_Setup();
	FAT_Setup();
	SDcard_Setup();

	char name[6]; //name and revision
	FPGAComm_CopyFromFPGA((void*)&name, 0x7ff0, 6);
	if (memcmp(name,"CPCA",4) != 0 || name[4] != 1 || name[5] != 1) {
		GPIO_ResetBits(LED_GPIO, LEDG_PIN);
		GPIO_SetBits(LED_GPIO, LEDR_PIN | LEDB_PIN);

		while(1) {}
	}

	uint8_t b;
	b = 0x09; //issue bus reset, keep everything disabled and f!exp high
	FPGAComm_CopyToFPGA(0x4c00, (void*)&b, 1);



	usleep(10000);
	b = 0x08; //release bus reset, keep everything disabled and f!exp high
	FPGAComm_CopyToFPGA(0x4c00, (void*)&b, 1);

	GPIO_ResetBits(LED_GPIO, LEDR_PIN | LEDG_PIN);
	GPIO_SetBits(LED_GPIO, LEDB_PIN);

	enum {
	  Initial,
	  RomLoaded
	} state = Initial;


	while(1) {
	  switch(state) {
	  case Initial:
	    if (initROM() != 0) {
	      usleep(100000);
	    } else {
	      state = RomLoaded;
	      uint8_t b;
	      b = 0x09; //issue bus reset, keep everything disabled and f!exp high
	      FPGAComm_CopyToFPGA(0x4c00, (void*)&b, 1);
	      usleep(10000);
	      /* in theory, this is all. but in practice, something is amiss.*/
	      //b = 0x06; //release bus reset, enable f!exp, rom and fdc
	      b = 0x0c; //release bus reset, disable f!exp and enable fdc
	      FPGAComm_CopyToFPGA(0x4c00, (void*)&b, 1);
	      GPIO_ResetBits(LED_GPIO, LEDR_PIN | LEDB_PIN);
	      GPIO_SetBits(LED_GPIO, LEDG_PIN);
	    }
	    break;
	  case RomLoaded:
	    __WFI();
	    break;
	  }
	}

	return 0;
}

static std::unordered_map<RefPtr<Inode>, std::string> filesystems;

void VFS_RegisterFilesystem(char const *type, RefPtr<Inode> ino) {
	//find a new name in /media
	int num = 0;
	std::stringstream ss;
	while(num < 20) {
		ss.str("");
		ss << "/media/" << type << num;
		if(mkdir(ss.str().c_str(), S_IRWXU | S_IRWXG | S_IRWXO) != -1)
			break;
		num++;
	}
	if (num >= 20)
		return;

	std::string name = ss.str();

	filesystems.insert(std::make_pair(ino,name));
	VFS_Mount(name.c_str(),ino);
}

void VFS_UnregisterFilesystem(RefPtr<Inode> ino) {
	VFS_Unmount(filesystems[ino].c_str());
	filesystems.erase(ino);
}


