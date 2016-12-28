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
#include <deque>
#include <malloc.h>

#include <bsp/stm32f4xx_gpio.h>
#include <bsp/stm32f4xx_rcc.h>
#include <bsp/stm32f4xx_flash.h>
#include <bsp/stm32f4xx_exti.h>
#include <bsp/stm32f4xx_syscfg.h>
#include <fpga/fpga_comm.h>
#include <fpga/layout.h>
#include <timer.h>
#include <block/sdcard.h>
#include <fs/fat.h>
#include <fs/vfs.hpp>
#include <fpga/font.h>
#include <fdc/fdc.h>
#include <fpga/sprite.hpp>
#include <usb/usb.hpp>
#include <input/usbhid.h>
#include <mouse.h>
#include <ui/iconbar.h>
#include <ui/ui.hpp>
#include <deferredwork.hpp>

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
	//P: 2,4,6,8   => 2 => SYSCLK = 168
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
   0x4000 - 0x47ff : FDC Data Exchange RAM
   0x4800          : Soft FDD output information
                     bit 1:0: drive unit
                     bit 2: physical head
		     bit 5:3: command
                      0: NONE
		      1: READID
		      2: FORMATTRACK
		      3: READDATA
		      4: READDELETEDDATA
		      5: WRITEDATA
		      6: WRITEDELETEDDATA
		      7: READTRACK
                     bit 6: fm(0) or mfm(1)
                     bit 7: soft fdd info valid
   0x4801          : Soft FDD output PCN (physical cylinder number)
   0x4802          : Soft FDD output C (logical cylinder number)
   0x4803          : Soft FDD output H (logical head number)
   0x4804          : Soft FDD output R (logical sector size)
   0x4805          : Soft FDD output N (logical sector number)
   0x4806          : Soft FDD output sector size Power of Two
   0x4807          : Soft FDD output sectors per track
   0x4808          : Soft FDD output gap length
   0x4809          : Soft FDD output filler byte
   0x480a          : Soft FDD input access error codes
                     bit 1: control mark
		     bit 2: bad cylinder
		     bit 3: wrong cylinder
		     bit 4: missing address mark
		     bit 5: no data
		     bit 6: data error
		     bit 7: valid (to be set once all fields have been updated)
   0x480b          : motor on
   0x480c          : Soft FDD output debug
   0x4810          : Soft FDD input Drive #0 status
                     bit 4: Two sided indication
		     bit 5: Fault
		     bit 6: Write protected
		     bit 7: Ready
   0x4811          : Soft FDD input Drive #1 status
   0x4812          : Soft FDD input Drive #2 status
   0x4813          : Soft FDD input Drive #3 status
   0x4814          : Soft FDD input Drive #0 next cylinder number
   0x4815          : Soft FDD input Drive #1 next cylinder number
   0x4816          : Soft FDD input Drive #2 next cylinder number
   0x4817          : Soft FDD input Drive #3 next cylinder number
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
                     Tile data from the palette area is replaced with constant
                      palette index 0-3.
   0x6f00 - 0x6fff : The highest range is used for palette data, but can be
                     used for map data as well. If used for tile addresses,
		     instead of the memory contents there are 4 uniform colored
		     tiles of the first 4 palette colors.
		     The palette data forms
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

   0x7000 - 0x7009 : spite0
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
   0x7010 - 0x7019 : spite1
   0x7020 - 0x7029 : spite2
   0x7030 - 0x7039 : spite3
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
   Bits below not accessible using parallel access
   0x7ff0 - 0x7ff3 : ID-Code: "CPCA"
   0x7ff4          : Major version
   0x7ff5          : Minor version
   0x7ff8 - 0x7ff9 : Parallel access address, msb is write indicator
   0x7ffd          : FPGA reset (does not need any data, but must be a write)
   0x7ffe          : Irq status
   0x7fff          : Irq mask


   interrupt layout:
   0: Soft FDD output is valid while Soft FDD input is not or transfer finished.
   1-7: unused
 */

static std::string findMediaFile(char const *name) {
	DIR *dir = opendir("/media");
	std::string result = "";
	if (!dir)
		return result;
	struct dirent *dent;
	while((dent = readdir(dir))) {
		if((dent->d_name[0] == '.' && dent->d_name[1] == '\0') ||
		   (dent->d_name[0] == '.' && dent->d_name[1] == '.' && dent->d_name[2] == '\0'))
			continue;
		std::string path("/media/");
		path += dent->d_name;
		path += "/";
		path += name;
		if (access(path.c_str(), R_OK) == 0) {
			result = path;
			break;
		}
	}
	closedir(dir);
	return result;
}

static int initROM() {
	//first, try to find the ROM file
	std::string path = findMediaFile("amsdos.rom");
	if (path.empty())
		return -1;
	int fd = open(path.c_str(), O_RDONLY);
	if (fd == -1)
		return -1;
	char buf1[1024];
	char buf2[1024];
	uint32_t addr = FPGA_CPC_ROM; //base address of the ROM
	while(addr < FPGA_CPC_ROM+0x4000) {
		size_t n = sizeof(buf1);
		if (addr + n > FPGA_CPC_ROM+0x4000)
			n = FPGA_CPC_ROM+0x4000 - addr;
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
	if (addr < FPGA_CPC_ROM+0x4000) {
		close(fd);
		return -1;
	}
	lseek(fd, 0, SEEK_SET);
	addr = FPGA_CPC_ROM; //base address of the ROM
	while(addr < FPGA_CPC_ROM+0x4000) {
		size_t n = sizeof(buf1);
		if (addr + n > FPGA_CPC_ROM+0x4000)
			n = FPGA_CPC_ROM+0x4000 - addr;
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

struct {
  uint32_t samples[1024];
} CPCLog;
void captureCPCLog() {
	uint8_t b = 0x00;
	FPGAComm_CopyToFPGA(FPGA_DBG_BASE, &b, 1);
	FPGAComm_CopyFromFPGA(&CPCLog, FPGA_DBG_BASE, sizeof(CPCLog));
	b = 0x80;
	FPGAComm_CopyToFPGA(FPGA_DBG_BASE, &b, 1);
}

static FPGAComm_Command cpcResetFPGACommand;
static uint8_t cpcResetState = 0x6;
static void cpcResetCompletion(int result, struct FPGAComm_Command *unused) {
	if (cpcResetState & 1) {
		cpcResetState &= ~1;

		FPGAComm_ReadWriteCommand(&cpcResetFPGACommand);
	}
}
static void cpcResetTimer(void *unused) {
	cpcResetState = 0x7;
	cpcResetFPGACommand.address = FPGA_CPC_CTL;
	cpcResetFPGACommand.length = 1;
	cpcResetFPGACommand.read_data = NULL;
	cpcResetFPGACommand.write_data = &cpcResetState;
	cpcResetFPGACommand.completion = cpcResetCompletion;
	FPGAComm_ReadWriteCommand(&cpcResetFPGACommand);
}

static std::deque<sigc::slot<void> > deferred_work;

int main()
{
	NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);
	LED_Setup();
	SysClk_Setup();
	//__WFI() is guaranteed to return after this point.(Whenever SYSTICK
	//gets emitted)
	Timer_Setup();
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_SYSCFG, ENABLE);
	EXTI_DeInit();
	SYSCFG_CompensationCellCmd(ENABLE);
	while(SYSCFG_GetCompensationCellStatus() == RESET) {
		__WFI();
	}

	VFS_Setup();

	FPGAComm_Setup();
	Sprite_Setup();

	Mouse_Setup();

	IconBar_Setup();

	FAT_Setup();
	SDcard_Setup();

	USBHID_Setup();
	USB_Setup();

	while(1) {
		char name[6] = {0xff, 0}; //name and revision
		FPGAComm_CopyFromFPGA((void*)&name, FPGA_INT_ID, 6);
		if (memcmp(name,"CPCA",4) == 0 && name[4] == 1 && name[5] == 1)
			break;
	}

	uint8_t b;
	uint16_t w;
	b = 0x09; //issue bus reset, keep everything disabled and f!exp high
	FPGAComm_CopyToFPGA(FPGA_CPC_CTL, (void*)&b, 1);

	//center the image horizontally, vertical is already good.
	w = 9;
	FPGAComm_CopyToFPGA(FPGA_GRPH_HSYNC_STRT, (void*)&w, 2);
	w = 60;
	FPGAComm_CopyToFPGA(FPGA_GRPH_HSYNC_END, (void*)&w, 2);

	usleep(10000);
	b = 0x08; //release bus reset, keep everything disabled and f!exp high
	FPGAComm_CopyToFPGA(FPGA_CPC_CTL, (void*)&b, 1);

	GPIO_ResetBits(LED_GPIO, LEDR_PIN | LEDG_PIN);
	GPIO_SetBits(LED_GPIO, LEDB_PIN);

	font_upload();

	{
	  uint8_t font_palette11[16] = {6, 10,0,0,//dark blue on yellow
					8, 1, 0,0,//black on white
					1, 8, 0,0,//white on black
	  };
	  uint8_t font_palette15[16] = {10, 6,0,0,//yellow on dark blue
					1, 8, 0,0,//white on black
					8, 1, 0,0,//black on white
	  };
	  sprite_set_palette(11, font_palette11);
	  sprite_set_palette(15, font_palette15);
	  sprite_upload_palette();
	}

	enum {
		Initial,
		RomLoaded
	} state = Initial;

	uint32_t logo_map[] = {
		font_get_tile('C', 15, 0),
		font_get_tile('P', 15, 0),
		font_get_tile('C', 15, 0),
		font_get_tile(' ', 15, 0),
		font_get_tile('A', 15, 0),
		font_get_tile('d', 15, 0),
		font_get_tile('d', 15, 0),
		font_get_tile('o', 15, 0),
		font_get_tile('n', 15, 0),
		font_get_tile('-', 15, 0),
		font_get_tile('B', 15, 0),
		font_get_tile('o', 15, 0),
		font_get_tile('x', 15, 0),
	};
	uint16_t logo_map_base = sprite_alloc_vmem(13,1,~0U);
	FPGAComm_CopyToFPGA(FPGA_GRPH_SPRITES_RAM +
			    logo_map_base*4, logo_map, sizeof(logo_map));
	Sprite logo_sprite;
	sprite_info logo_sprite_info = {
		.hpos = (uint16_t)(ui::screenRect().x+50),
		.vpos = (uint16_t)(ui::screenRect().y+30),
		.map_addr = logo_map_base,
		.hsize = 13,
		.vsize = 1,
		.hpitch = 13,
		.doublesize = 1,
	};
	logo_sprite.setSpriteInfo(logo_sprite_info);
	logo_sprite.setPriority(0);
	logo_sprite.setZOrder(0);
	logo_sprite.setVisible(true);

	captureCPCLog(); //do it once so the function is not optimized out
	while(1) {
		switch(state) {
		case Initial:
			if (initROM() != 0) {
				usleep(100000);
			} else {
				state = RomLoaded;
				uint8_t b;
				b = 0x09; //issue bus reset, keep everything disabled and f!exp high
				FPGAComm_CopyToFPGA(FPGA_CPC_CTL, (void*)&b, 1);
				usleep(10000);
				/* in theory, this is all. but in practice, something is amiss.*/
				b = 0x06; //release bus reset, enable f!exp, rom and fdc
				//b = 0x04; //release bus reset, enable f!exp and enable fdc

				FPGAComm_CopyToFPGA(FPGA_CPC_CTL, (void*)&b, 1);
				GPIO_ResetBits(LED_GPIO, LEDR_PIN | LEDB_PIN);
				GPIO_SetBits(LED_GPIO, LEDG_PIN);

				FDC_Setup();
				//	      Timer_Repeating(5000000, cpcResetTimer, NULL);
				std::string path = findMediaFile("disk.dsk");
				if (!path.empty()) {
					FDC_InsertDisk(0,path.c_str());
				}
				malloc_stats();

			}
			break;
		case RomLoaded:
			__WFI();
			break;
		}
		uint32_t irq_level;
		ISR_Disable(irq_level);
		while(!deferred_work.empty()) {
			sigc::slot<void> work = deferred_work.front();
			deferred_work.pop_front();
			ISR_Enable(irq_level);
			work();
			ISR_Disable(irq_level);
		}
		ISR_Enable(irq_level);
	}

	return 0;
}

void addDeferredWork(sigc::slot<void> const &work) {
	ISR_Guard g;
	deferred_work.push_back(work);
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

void FDC_MotorOn() {
	IconBar_disk_motor_on();
}

void FDC_MotorOff() {
	IconBar_disk_motor_off();
}

void FDC_Activity(int drive, int activity) {
	IconBar_disk_activity(drive, activity);
}

