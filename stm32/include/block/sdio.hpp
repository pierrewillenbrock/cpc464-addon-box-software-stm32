
#pragma once

#include <stdint.h>
#include <sigc++/sigc++.h>

#define SDIO_OK                0
#define SDIO_SystemTimeout     1
#define SDIO_CommandCRC        2
#define SDIO_CommandTimeout    3
#define SDIO_CommandMismatch   4
#define SDIO_DataRxOverrun     5
#define SDIO_DataCRC           6
#define SDIO_DataTimeout       7
#define SDIO_DataStartBitError 8
#define SDIO_CSError           9
#define SDIO_DMAError          10
#define SDIO_UnknownError      11

#define SDIO_DataBlockSize_1b               ((uint32_t)0x00000000)
#define SDIO_DataBlockSize_2b               ((uint32_t)0x00000010)
#define SDIO_DataBlockSize_4b               ((uint32_t)0x00000020)
#define SDIO_DataBlockSize_8b               ((uint32_t)0x00000030)
#define SDIO_DataBlockSize_16b              ((uint32_t)0x00000040)
#define SDIO_DataBlockSize_32b              ((uint32_t)0x00000050)
#define SDIO_DataBlockSize_64b              ((uint32_t)0x00000060)
#define SDIO_DataBlockSize_128b             ((uint32_t)0x00000070)
#define SDIO_DataBlockSize_256b             ((uint32_t)0x00000080)
#define SDIO_DataBlockSize_512b             ((uint32_t)0x00000090)
#define SDIO_DataBlockSize_1024b            ((uint32_t)0x000000A0)
#define SDIO_DataBlockSize_2048b            ((uint32_t)0x000000B0)
#define SDIO_DataBlockSize_4096b            ((uint32_t)0x000000C0)
#define SDIO_DataBlockSize_8192b            ((uint32_t)0x000000D0)
#define SDIO_DataBlockSize_16384b           ((uint32_t)0x000000E0)

#define SDIO_APPCMD 0x80

enum SDResponseType {
	NoResponse,
	ResponseShort,
	ResponseLong,
	Response1, //like short, but sdio knows how to detect errors
	Response3, //like short, but sdio ignores crc errors
};

enum SDDataType {
	NoData,
	DataToSDIO,
	DataToCard
};

struct SDCommand {
	uint32_t argument;
	uint8_t command; //can be ored with SDIO_APPCMD to automatically send Cmd55
	uint16_t rca;//used with SDIO_APPCMD
	enum SDResponseType responseType;
	enum SDDataType dataType;
	uint8_t retryCounter;
	uint32_t response[4];
	void *data;
	uint32_t datalength;
	uint32_t datablocksize;
	sigc::slot<void(int)> slot;
	uint8_t state;
	uint32_t datapos;
};

void SDIO_Setup(void);
void SDIO_PowerUp(void);
void SDIO_ClockEnable(void);
void SDIO_ConfigureBus(int widebus, int maxkhz);
void SDIO_PowerDown(void);
void SDIO_Command(struct SDCommand *command);

