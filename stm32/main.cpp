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

/* FPGA memory map:
   0x0000 - 0x3fff : Expansion ROM source
   0x4000 -        : FDC registers?
   0x4c00          : CPC control:
                     bit 0: bus reset,
                     bit 1: rom enable,
                     bit 2: fdc enable,
                     bit 3: f!exp(inverted,
                            i.E. 1 is f!exp high, i.E. deactivated)
   0x6000 - 0x6fff : graphics memory, uint16le with 9 valid bits each
                     Map bytes(9 bit in this case) are:
                      bit 5:0 are high bits (10:5) of tile address,
                      bit 8:6 are the palette selection bits
                     Tiles are 8x4 bytes:
                      bit 3:0 are the color index of one pixel
                      bit 7:4 are the color index of the other pixel
                      bit 8 selects the alternative palette mode
   0x6f00 - 0x6fff : The highest range is used for palette data, but can be
                     used for tiles or map data as well. The palette data forms
                     8 palettes of 16 full color indices(5 bits) and
                     8 palettes of 16 short color indices(4 bits)
                     The palette bytes are used this way:
                     bits 4:0 contain a full color index, used when
                              tile bit 8 is 0
                     bits 8:4 contain a short color index, used when
                              tile bit 8 is 1

  the primary colors are:
         (colors available to full and short color indexes)
	0:	"000000", -- transparent
	1:	"000000", -- black
	2:	"000011", -- bright blue
	3:	"001100", -- bright green
	4:	"110000", -- bright red
	5:	"001111", -- bright cyan
	6:	"111100", -- bright yellow
	7:	"110011", -- bright magenta
	8:	"111111", -- white
	9:	"010101", -- gray
	10:	"000001", -- dark blue
	11:	"000100", -- dark green
	12:	"010000", -- dark red
	13:	"000101", -- dark cyan
	14:	"010100", -- dark yellow
	15:	"010001", -- dark magenta
         (colors available only to full color indexes)
	16:	"000111", -- greenish blue
	17:	"001110", -- blueish green
	18:	"011100", -- redish green
	19:	"110100", -- greenish red (orange?)
	20:	"110001", -- blueish red
	21:	"010011", -- redish blue
	22:	"010111", -- blueish gray
	23:	"011101", -- greenish gray
	24:	"110101", -- redish gray
	25:	"011111", -- cyanish gray
	26:	"111101", -- yellowish gray
	27:	"110111", -- magentaish gray
	28:	"000000", -- black
	29:	"000011", -- bright blue
	30:	"001100", -- bright green
	31:	"110000" -- bright red

   0x7000 - 0x700f : spite0
   0x7000          : sprite_hpos 0:7
   0x7001          : sprite_hpos 10:8
   0x7002          : sprite_vpos 0:7
   0x7003          : sprite_vpos 10:8
   0x7004          : ram_base 0:7     base of map data mapping to tiles
   0x7005          : ram_base 10:8
   0x7006          : hsize in tiles
   0x7007          : vsize in tiles
   0x7008          : hpitch in tiles
   0x7009          : bit0: double sized pixels
   0x7010 - 0x701f : spite1
   0x7020 - 0x702f : spite2
   0x7030 - 0x703f : spite3
   0x7040          : vsync_start 0:7  default: 300
   0x7041          : vsync_start 8:8
   0x7042          : vsync_end 0:7    default: 311
   0x7043          : vsync_end 8:8
   0x7044          : hsync_start 0:7  default: 900
   0x7045          : hsync_start 9:8
   0x7046          : hsync_end 0:7    default: 1023
   0x7047          : hsync_end 9:8
   0x7048          : lpen_vpos 0:7
   0x7049          : lpen_vpos 8:8 in bit 0:0, bit7: lpen active
   0x704a          : lpen_hpos 0:7
   0x704b          : lpen_hpos 9:8
   0x704c          : cursor_force_high (color bits getting forced high when the
                                        fcursor signal is active)
   0x704d          : cursor_force_low  (color bits getting forced low when the
                                        fcursor signal is active)
   0x7ffx - ?
 */

struct sprite_info {
	uint16_t hpos;
	uint16_t vpos;
	uint16_t map_addr;
	uint8_t hsize;
	uint8_t vsize;
	uint8_t hpitch;
	uint8_t doublesize:1;
	uint8_t reserved:7;
} __attribute__((packed));

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
	uint16_t w;
	b = 0x09; //issue bus reset, keep everything disabled and f!exp high
	FPGAComm_CopyToFPGA(0x4c00, (void*)&b, 1);

	//center the image horizontally, vertical is already good.
	w = 9;
	FPGAComm_CopyToFPGA(0x7044, (void*)&w, 2);
	w = 60;
	FPGAComm_CopyToFPGA(0x7046, (void*)&w, 2);


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


