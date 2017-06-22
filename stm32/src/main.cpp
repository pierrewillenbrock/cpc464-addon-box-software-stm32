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
#include <bsp/misc.h>
#include <fpga/fpga_comm.hpp>
#include <fpga/layout.h>
#include <timer.hpp>
#include <block/sdcard.h>
#include <fs/fat.h>
#include <fs/vfs.hpp>
#include <fpga/font.h>
#include <fdc/fdc.h>
#include <fpga/sprite.hpp>
#include <usb/usb.hpp>
#include <input/usbhid.h>
#include <mouse.hpp>
#include <joystick.hpp>
#include <keyboard.hpp>
#include <ui/iconbar.h>
#include <ui/ui.hpp>
#include <hw/led.h>
#include <deferredwork.hpp>
#include <ui/notify.hpp>
#include <usbdevicenotify.h>
#include <joyport.hpp>

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
//the cpc and fpga take some time to deliver a good clock, so just wait.
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
static void cpcResetCompletion(int /*result*/) {
	if (cpcResetState & 1) {
		cpcResetState &= ~1;

		FPGAComm_ReadWriteCommand(&cpcResetFPGACommand);
	}
}
static void cpcResetTimer(void */*unused*/) {
	cpcResetState = 0x7;
	cpcResetFPGACommand.address = FPGA_CPC_CTL;
	cpcResetFPGACommand.length = 1;
	cpcResetFPGACommand.read_data = NULL;
	cpcResetFPGACommand.write_data = &cpcResetState;
	cpcResetFPGACommand.slot = sigc::ptr_fun(&cpcResetCompletion);
	FPGAComm_ReadWriteCommand(&cpcResetFPGACommand);
}

static FPGAComm_Command graphicsCheckFPGACommand;
static struct { uint16_t vposmax; uint16_t hposmax; } graphicsCheckData,
       lastGraphicsCheckData;
static FpgaGraphicsSettings fpga_graphics_settings = {
  290,300,310,25, 9, 72, 934, 145
};

static void graphicsCheckCompletion(int result) {
	if (result != 0)
		return;
	if (graphicsCheckData.vposmax < 200 ||
	    graphicsCheckData.hposmax < 800)
		return;//does not seem valid.
	//adjust {v,h}{sync,blank} according to current display mode
	//and tell screen about the current screen size. screen then
	//passes this information on.

	//filter small one count jitter
	if (graphicsCheckData.hposmax <= lastGraphicsCheckData.hposmax+1 &&
		graphicsCheckData.hposmax+1 >= lastGraphicsCheckData.hposmax &&
		graphicsCheckData.vposmax <= lastGraphicsCheckData.vposmax+1 &&
		graphicsCheckData.vposmax+1 >= lastGraphicsCheckData.vposmax)
		graphicsCheckData = lastGraphicsCheckData;
	else
		lastGraphicsCheckData = graphicsCheckData;

	unsigned w = graphicsCheckData.hposmax+1;
	unsigned h = graphicsCheckData.vposmax;
	FpgaGraphicsSettings new_settings;
	ui::Screen::Options const &opts = ui::screen.options();
#define ADJ_PARAMS(v,m) (((v)<0)?((m)+(v)):(v))
	memcpy(&new_settings,&fpga_graphics_settings,sizeof(new_settings));
	new_settings.vsync_strt  = ADJ_PARAMS(opts.vsync_start, h);
	new_settings.vsync_end   = ADJ_PARAMS(opts.vsync_end,   h);
	new_settings.vblank_strt = ADJ_PARAMS(opts.vblank_start,h);
	new_settings.vblank_end  = ADJ_PARAMS(opts.vblank_end,  h);
	new_settings.hsync_strt  = ADJ_PARAMS(opts.hsync_start, w);
	new_settings.hsync_end   = ADJ_PARAMS(opts.hsync_end,   w);
	new_settings.hblank_strt = ADJ_PARAMS(opts.hblank_start,w);
	new_settings.hblank_end  = ADJ_PARAMS(opts.hblank_end,  w);
	if (memcmp(&new_settings.vsync_strt, &fpga_graphics_settings,
		   sizeof(FpgaGraphicsSettings)) != 0) {
		fpga_graphics_settings = new_settings;
		ui::Rect r;
		r.x = opts.hblank_end - 15;
		r.y = opts.vblank_end * 2;
		r.width = new_settings.hblank_strt - new_settings.hblank_end;
		r.height = (new_settings.vblank_strt - new_settings.vblank_end) * 2;
		ui::screen.setRect(r);
		//for now, just upload the current config struct.
		graphicsCheckFPGACommand.address = FPGA_GRPH_VSYNC_STRT;
		graphicsCheckFPGACommand.length = sizeof(fpga_graphics_settings);
		graphicsCheckFPGACommand.read_data = NULL;
		graphicsCheckFPGACommand.write_data = &fpga_graphics_settings;
		graphicsCheckFPGACommand.slot = sigc::slot<void(int)>();
		FPGAComm_ReadWriteCommand(&graphicsCheckFPGACommand);
	}
}

static void graphicsCheckTimer() {
	graphicsCheckFPGACommand.address = FPGA_GRPH_VPOSMAX;
	graphicsCheckFPGACommand.length = 4;
	graphicsCheckFPGACommand.read_data = &graphicsCheckData;
	graphicsCheckFPGACommand.write_data = NULL;
	graphicsCheckFPGACommand.slot = sigc::ptr_fun(&graphicsCheckCompletion);
	FPGAComm_ReadWriteCommand(&graphicsCheckFPGACommand);
}

static std::deque<sigc::slot<void> > deferred_work;

bool doDeferredWork() {
	sigc::slot<void> work;
	{
		ISR_Guard g;
		if(deferred_work.empty())
			return false;
		work = deferred_work.front();
		deferred_work.pop_front();
	}
	work();
	return true;
}

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

	vfs::Setup();

	FPGAComm_Setup();
	Sprite_Setup();

	while(1) {
		char name[6] = {0xff, 0}; //name and revision
		FPGAComm_CopyFromFPGA((void*)&name, FPGA_INT_ID, 6);
		if (memcmp(name,"CPCA",4) == 0 && name[4] == 1 && name[5] == 1)
			break;
	}

	uint8_t b;
	b = 0x09; //issue bus reset, keep everything disabled and f!exp high
	FPGAComm_CopyToFPGA(FPGA_CPC_CTL, (void*)&b, 1);

	//center the image horizontally, vertical is already good.
	FPGAComm_CopyToFPGA(FPGA_GRPH_REG_BASE, &fpga_graphics_settings, sizeof(fpga_graphics_settings));

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

	Timer_Repeating(500000, sigc::ptr_fun(&graphicsCheckTimer));

	Mouse_Setup();
	Joystick_Setup();
	Keyboard_Setup();

	joyport::setup();

	IconBar_Setup();

	FAT_Setup();
	SDcard_Setup();

	ui::Notification_Setup();
	USBDeviceNotify_Setup(); // must be first usb driver
	USBHID_Setup();
	usb::Setup();

	enum {
		Initial,
		RomLoaded
	} state = Initial;

	MappedSprite logo_sprite;
	logo_sprite.setPriority(0);
	logo_sprite.setZOrder(0);
	logo_sprite.setSize(13,1);
	logo_sprite.setPosition(ui::screen.rect().x+50, ui::screen.rect().y+30);
	logo_sprite.setDoubleSize(true);
	logo_sprite.at(0,0) = font_get_tile('C', 15, 0);
	logo_sprite.at(1,0) = font_get_tile('P', 15, 0);
	logo_sprite.at(2,0) = font_get_tile('C', 15, 0);
	logo_sprite.at(3,0) = font_get_tile(' ', 15, 0);
	logo_sprite.at(4,0) = font_get_tile('A', 15, 0);
	logo_sprite.at(5,0) = font_get_tile('d', 15, 0);
	logo_sprite.at(6,0) = font_get_tile('d', 15, 0);
	logo_sprite.at(7,0) = font_get_tile('o', 15, 0);
	logo_sprite.at(8,0) = font_get_tile('n', 15, 0);
	logo_sprite.at(9,0) = font_get_tile('-', 15, 0);
	logo_sprite.at(10,0) = font_get_tile('B', 15, 0);
	logo_sprite.at(11,0) = font_get_tile('o', 15, 0);
	logo_sprite.at(12,0) = font_get_tile('x', 15, 0);
	logo_sprite.updateDone();
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
			break;
		}
		sched_yield();
	}

	return 0;
}

void addDeferredWork(sigc::slot<void> const &work) {
	ISR_Guard g;
	deferred_work.push_back(work);
}

static std::unordered_map<RefPtr<vfs::Inode>, std::string> filesystems;


namespace vfs {
	void RegisterFilesystem(char const *type, RefPtr<Inode> ino) {
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
		Mount(name.c_str(),ino);
	}

	void UnregisterFilesystem(RefPtr<Inode> ino) {
		Unmount(filesystems[ino].c_str());
		filesystems.erase(ino);
	}
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

