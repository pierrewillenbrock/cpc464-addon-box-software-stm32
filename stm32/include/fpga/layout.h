
#pragma once

#define FPGA_GRPH_BASE     (0x000000)
#define FPGA_CPC_BASE      (0x100000)
#define FPGA_JOYSTICK_BASE (0xfd0000)
#define FPGA_DBG_BASE      (0xfe0000)
#define FPGA_INT_BASE      (0xff0000)

#define FPGA_GRPH_SPRITES_BASE FPGA_GRPH_BASE
#define FPGA_GRPH_REG_BASE     (FPGA_GRPH_BASE+0xf0000)

#define FPGA_GRPH_SPRITES_RAM      FPGA_GRPH_SPRITES_BASE
#define FPGA_GRPH_SPRITES_PALETTE  (FPGA_GRPH_SPRITES_BASE+0x1f00)
#define FPGA_GRPH_SPRITE_BASE(num) (FPGA_GRPH_SPRITES_BASE+0xe0000 + (num)*16)

#define FPGA_GRPH_SPRITE_HPOS(num)    (FPGA_GRPH_SPRITE_BASE(num)+0)
#define FPGA_GRPH_SPRITE_VPOS(num)    (FPGA_GRPH_SPRITE_BASE(num)+2)
#define FPGA_GRPH_SPRITE_RAMBASE(num) (FPGA_GRPH_SPRITE_BASE(num)+4)
#define FPGA_GRPH_SPRITE_HSIZE(num)   (FPGA_GRPH_SPRITE_BASE(num)+6)
#define FPGA_GRPH_SPRITE_VSIZE(num)   (FPGA_GRPH_SPRITE_BASE(num)+7)
#define FPGA_GRPH_SPRITE_HPITCH(num)  (FPGA_GRPH_SPRITE_BASE(num)+8)
#define FPGA_GRPH_SPRITE_FLAGS(num)   (FPGA_GRPH_SPRITE_BASE(num)+9)

#define FPGA_GRPH_VSYNC_STRT   (FPGA_GRPH_REG_BASE+0)
#define FPGA_GRPH_VSYNC_END    (FPGA_GRPH_REG_BASE+2)
#define FPGA_GRPH_VBLANK_STRT  (FPGA_GRPH_REG_BASE+4)
#define FPGA_GRPH_VBLANK_END   (FPGA_GRPH_REG_BASE+6)
#define FPGA_GRPH_HSYNC_STRT   (FPGA_GRPH_REG_BASE+8)
#define FPGA_GRPH_HSYNC_END    (FPGA_GRPH_REG_BASE+10)
#define FPGA_GRPH_HBLANK_STRT  (FPGA_GRPH_REG_BASE+12)
#define FPGA_GRPH_HBLANK_END   (FPGA_GRPH_REG_BASE+14)
#define FPGA_GRPH_LPEN_VPOS    (FPGA_GRPH_REG_BASE+16)
#define FPGA_GRPH_LPEN_HPOS    (FPGA_GRPH_REG_BASE+18)
#define FPGA_GRPH_CURSOR_FRCHI (FPGA_GRPH_REG_BASE+20)
#define FPGA_GRPH_CURSOR_FRCLO (FPGA_GRPH_REG_BASE+21)
#define FPGA_GRPH_VPOSMAX      (FPGA_GRPH_REG_BASE+32)
#define FPGA_GRPH_HPOSMAX      (FPGA_GRPH_REG_BASE+34)

#define FPGA_CPC_ROM      FPGA_CPC_BASE
#define FPGA_CPC_FDC_BASE (FPGA_CPC_BASE+0x100000)
#define FPGA_CPC_CTL      (FPGA_CPC_BASE+0x200000)

#define FPGA_CPC_FDC_DATA             (FPGA_CPC_FDC_BASE+0x000)
#define FPGA_CPC_FDC_INFOBLK          (FPGA_CPC_FDC_BASE+0x800)
#define FPGA_CPC_FDC_OUTSTS           (FPGA_CPC_FDC_INFOBLK+0)
#define FPGA_CPC_FDC_OUTPCN           (FPGA_CPC_FDC_INFOBLK+1)
#define FPGA_CPC_FDC_OUTC             (FPGA_CPC_FDC_INFOBLK+2)
#define FPGA_CPC_FDC_OUTH             (FPGA_CPC_FDC_INFOBLK+3)
#define FPGA_CPC_FDC_OUTR             (FPGA_CPC_FDC_INFOBLK+4)
#define FPGA_CPC_FDC_OUTN             (FPGA_CPC_FDC_INFOBLK+5)
#define FPGA_CPC_FDC_OUTSZPOT         (FPGA_CPC_FDC_INFOBLK+6)
#define FPGA_CPC_FDC_OUTSECTPTRCK     (FPGA_CPC_FDC_INFOBLK+7)
#define FPGA_CPC_FDC_OUTGPL           (FPGA_CPC_FDC_INFOBLK+8)
#define FPGA_CPC_FDC_OUTFILL          (FPGA_CPC_FDC_INFOBLK+9)
#define FPGA_CPC_FDC_INSTS            (FPGA_CPC_FDC_BASE+0x80a)
#define FPGA_CPC_FDC_MOTOR            (FPGA_CPC_FDC_BASE+0x80b)
#define FPGA_CPC_FDC_DBG              (FPGA_CPC_FDC_BASE+0x80c)
#define FPGA_CPC_FDC_FDD_STS(driveno) (FPGA_CPC_FDC_BASE+0x810+(driveno))
#define FPGA_CPC_FDC_FDD_NCN(driveno) (FPGA_CPC_FDC_BASE+0x814+(driveno))

#define FPGA_JOYSTICK_J1ST      (FPGA_JOYSTICK_BASE+0)
#define FPGA_JOYSTICK_J2ST      (FPGA_JOYSTICK_BASE+1)
#define FPGA_JOYSTICK_MOUSEXCTR (FPGA_JOYSTICK_BASE+2)
#define FPGA_JOYSTICK_MOUSEYCTR (FPGA_JOYSTICK_BASE+3)
#define FPGA_JOYSTICK_MOUSEXINC (FPGA_JOYSTICK_BASE+4)
#define FPGA_JOYSTICK_MOUSEYINC (FPGA_JOYSTICK_BASE+5)

#define FPGA_INT_ID      (FPGA_INT_BASE+0xfff0)
#define FPGA_INT_VERSION (FPGA_INT_BASE+0xfff4)
#define FPGA_INT_PAR     (FPGA_INT_BASE+0xfff8)
#define FPGA_INT_RESET   (FPGA_INT_BASE+0xfffd)
#define FPGA_INT_IRQSTS  (FPGA_INT_BASE+0xfffe)
#define FPGA_INT_IRQMSK  (FPGA_INT_BASE+0xffff)

#define PACKED __attribute__((packed))

struct FpgaGraphicsSettings {
  uint16_t vsync_strt;
  uint16_t vsync_end;
  uint16_t vblank_strt;
  uint16_t vblank_end;
  uint16_t hsync_strt;
  uint16_t hsync_end;
  uint16_t hblank_strt;
  uint16_t hblank_end;
} PACKED;

/* FPGA memory map:
   0x000000 - 0x001fff : graphics memory, uint32le with 2x9 valid bits each
                         Map bytes(2x9 bit in this case) are:
                          bit 1:0 low plane select bits
                          bit 8:2 are high bits (10:4) of tile address,
			  bit 20:16: palette select (0-7: full palette, 8-15
			             limited color palette)
			  bit 22:21: palette index for bit plane mode 1,2,
			             forming 4 subpalettes of 4 indices
			  bit 23: vflip
			  bit 24: hflip
                          bit 8:6 are the palette selection bits
                         Tiles are 8x4 bytes:
                          bit 3:0 are the color index of one pixel
                          bit 7:4 are the color index of the other pixel
                          bit 8 inverts bit 3 of the palette select
                         Tile data from the palette area is replaced with
			  constant palette index 0-15. plane bits get used as
			  extra color index bits: map bits *****ssss11111iiii
   0x001f00 - 0x001fff : The highest range is used for palette data, but can be
                         used for map data as well. If used for tile addresses,
			 instead of the memory contents there are 4 uniform
			 colored tiles of the first 4 palette colors.
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

   0x0e0000 - 0x0e0009 : spite0
   0x0e0000            : sprite_hpos 0:7
   0x0e0001            : sprite_hpos 10:8
   0x0e0002            : sprite_vpos 0:7
   0x0e0003            : sprite_vpos 10:8
   0x0e0004            : ram_base 0:7     base of map data mapping to tiles
   0x0e0005            : ram_base 10:8
   0x0e0006            : hsize in tiles
   0x0e0007            : vsize in tiles
   0x0e0008            : hpitch in tiles
   0x0e0009            : bit0: double sized pixels
   0x0e0010 - 0x0e0019 : spite1
   0x0e0020 - 0x0e0029 : spite2
   0x0e0030 - 0x0e0039 : spite3
   0x0f0000            : vsync_start 0:7  default: 300
   0x0f0001            : vsync_start 8:8
   0x0f0002            : vsync_end 0:7    default: 311
   0x0f0003            : vsync_end 8:8
   0x0f0004            : vblank_start 0:7  default: 300
   0x0f0005            : vblank_start 8:8
   0x0f0006            : vblank_end 0:7    default: 311
   0x0f0007            : vblank_end 8:8
   0x0f0008            : hsync_start 0:7  default: 900
   0x0f0009            : hsync_start 9:8
   0x0f000a            : hsync_end 0:7    default: 1023
   0x0f000b            : hsync_end 9:8
   0x0f000c            : hblank_start 0:7  default: 900
   0x0f000d            : hblank_start 9:8
   0x0f000e            : hblank_end 0:7    default: 1023
   0x0f000f            : hblank_end 9:8
   0x0f0010            : lpen_vpos 0:7
   0x0f0011            : lpen_vpos 8:8 in bit 0:0, bit7: lpen active
   0x0f0012            : lpen_hpos 0:7
   0x0f0013            : lpen_hpos 9:8
   0x0f0014            : cursor_force_high (color bits getting forced high when
                                            the fcursor signal is active)
   0x0f0015            : cursor_force_low  (color bits getting forced low when
                                            the fcursor signal is active)
   0x0f0020            : vposmax 0:7
   0x0f0021            : vposmax 8:8
   0x0f0022            : hposmax 0:7
   0x0f0023            : hposmax 9:8
   0x100000 - 0x103fff : Expansion ROM source
   0x200000 - 0x2007ff : FDC Data Exchange RAM
   0x200800            : Soft FDD output information
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
   0x200801            : Soft FDD output PCN (physical cylinder number)
   0x200802            : Soft FDD output C (logical cylinder number)
   0x200803            : Soft FDD output H (logical head number)
   0x200804            : Soft FDD output R (logical sector size)
   0x200805            : Soft FDD output N (logical sector number)
   0x200806            : Soft FDD output sector size Power of Two
   0x200807            : Soft FDD output sectors per track
   0x200808            : Soft FDD output gap length
   0x200809            : Soft FDD output filler byte
   0x20080a            : Soft FDD input access error codes
                         bit 1: control mark
		         bit 2: bad cylinder
		         bit 3: wrong cylinder
		         bit 4: missing address mark
		         bit 5: no data
		         bit 6: data error
		         bit 7: valid (to be set once all fields have been updated)
   0x20080b            : motor on
   0x20080c            : Soft FDD output debug
   0x200810            : Soft FDD input Drive #0 status
                         bit 4: Two sided indication
		         bit 5: Fault
		         bit 6: Write protected
		         bit 7: Ready
   0x200811            : Soft FDD input Drive #1 status
   0x200812            : Soft FDD input Drive #2 status
   0x200813            : Soft FDD input Drive #3 status
   0x200814            : Soft FDD input Drive #0 next cylinder number
   0x200815            : Soft FDD input Drive #1 next cylinder number
   0x200816            : Soft FDD input Drive #2 next cylinder number
   0x200817            : Soft FDD input Drive #3 next cylinder number
   0x300c00            : CPC control:
                         bit 0: bus reset,
                         bit 1: rom enable,
                         bit 2: fdc enable,
                         bit 3: f!exp(inverted,
                                i.E. 1 is f!exp high, i.E. deactivated)
   Bits below not accessible using parallel access
   0x7ffff0 - 0x7ff3 : ID-Code: "CPCA"
   0x7ffff4          : Major version
   0x7ffff5          : Minor version
   0x7ffff8 - 0x7ff9 : Parallel access address, msb is write indicator
   0x7ffffd          : FPGA reset (does not need any data, but must be a write)
   0x7ffffe          : Irq status
   0x7fffff          : Irq mask


   interrupt layout:
   0: Soft FDD output is valid while Soft FDD input is not or transfer finished.
   1-7: unused
 */
