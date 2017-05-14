#pragma once

// SD standard defintions

#define SD_R6_GENERAL_UNKNOWN_ERROR     ((uint32_t)0x00002000U)
#define SD_R6_ILLEGAL_CMD               ((uint32_t)0x00004000U)
#define SD_R6_COM_CRC_FAILED            ((uint32_t)0x00008000U)

//voltage window is 3.2-3.3. could try some different values as well.
//3.2-3.4(for +/-5%) is one option, could also try 3.0-3.6 (for +/-10%)
//would be 0x00300000 resp 0x00780000
#define SD_VOLTAGE_WINDOW_SD            ((uint32_t)0x00100000U)
#define SD_HIGH_CAPACITY                ((uint32_t)0x40000000U)

#define SD_CS_ADDR_OUT_OF_RANGE        ((uint32_t)0x80000000U)
#define SD_CS_ADDR_MISALIGNED          ((uint32_t)0x40000000U)
#define SD_CS_BLOCK_LEN_ERR            ((uint32_t)0x20000000U)
#define SD_CS_ERASE_SEQ_ERR            ((uint32_t)0x10000000U)
#define SD_CS_BAD_ERASE_PARAM          ((uint32_t)0x08000000U)
#define SD_CS_WRITE_PROT_VIOLATION     ((uint32_t)0x04000000U)
#define SD_CS_LOCK_UNLOCK_FAILED       ((uint32_t)0x01000000U)
#define SD_CS_COM_CRC_FAILED           ((uint32_t)0x00800000U)
#define SD_CS_ILLEGAL_CMD              ((uint32_t)0x00400000U)
#define SD_CS_CARD_ECC_FAILED          ((uint32_t)0x00200000U)
#define SD_CS_CC_ERROR                 ((uint32_t)0x00100000U)
#define SD_CS_GENERAL_UNKNOWN_ERROR    ((uint32_t)0x00080000U)
#define SD_CS_STREAM_READ_UNDERRUN     ((uint32_t)0x00040000U)
#define SD_CS_STREAM_WRITE_OVERRUN     ((uint32_t)0x00020000U)
#define SD_CS_CID_CSD_OVERWRITE        ((uint32_t)0x00010000U)
#define SD_CS_WP_ERASE_SKIP            ((uint32_t)0x00008000U)
#define SD_CS_CARD_ECC_DISABLED        ((uint32_t)0x00004000U)
#define SD_CS_ERASE_RESET              ((uint32_t)0x00002000U)
#define SD_CS_AKE_SEQ_ERROR            ((uint32_t)0x00000008U)
//#define SD_CS_ERRORBITS                ((uint32_t)0xFDFFE008U)
#define SD_CS_ERRORBITS                ((uint32_t)0xFD7FE008U)


