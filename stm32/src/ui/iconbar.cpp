
#include <ui/iconbar.h>
#include <ui/ui.hpp>
#include "hint.hpp"
#include "menu.hpp"
#include "fileselect.hpp"
#include "videosettings.hpp"

#include <fpga/sprite.hpp>
#include <fpga/fpga_comm.h>
#include <fpga/layout.h>
#include <fpga/fpga_uploader.hpp>
#include <fdc/fdc.h>
#include <timer.h>
#include <deferredwork.hpp>

#include <sstream>

/* iconbar.
   9 icons with action menu
   action menu can be triggerd by mouse click. if the iconbar has focus,
   tab/arrow keys/joystick dpad etc change selection. selection is depicted by
   a small arrow above the icon. space, enter, any joystick button activates
   the menu.

   focus is gained by hitting the special button of an assigned joystick,
   or any button/key of unassigned devices.
 */

/*
     0            1      2     3      4           5           6             7            8            9            10         11         12          13          14          15
#0a:              black, gray, white, black,      gray,       black,        black,       gray,        black,       gray,      black,     white                                            used by: disk bl
#0b:              black, gray, white, orange,     orange,     orange,       black,       gray,        black,       gray,      black,     white                                            used by: disk bl(four frame animation)
#0c:              black, gray, white, orange,     orange,     black,        orange,      orange,      black,       gray,      black,     white                                            used by: disk bl
#0d:              black, gray, white, orange,     orange,     black,        black,       gray,        orange,      orange,    black,     white                                            used by: disk bl
#0e:              black, gray, white, orange,     orange,     black,        black,       gray,        black,       gray,      orange,    orange                                           used by: disk bl
#1:  transparent, black, gray, white, dark blue,  bright red, gray,         gray,        transparent, bright red,  gray,      white,     bright red, gray,       black,     bright green  used by: disk tr, joystick tr, lpen tr(tile rewrite), mouse tr(tile rewrite)
#2:  transparent, black, gray, white, dark blue,  gray,       bright green, transparent, gray,        transparent, white,     gray,      white,      black,      gray                     used by: disk tr, joystick tr

#8:  transparent, black, gray, white, yellow,     bright red, gray,         yellow,      yellow,      yellow,      gray,      yellow                                                      used by: disk br, joystick tl, bl, br, settings, mouse tl, bl, br
#9:  transparent, black, gray, white, yellow,     bright red, yellow,       yellow,      gray,        yellow,      yellow,    gray                                                        used by: disk br, joystick br, lpen tr(tile rewrite), mouse tr(tile rewrite)
#10: transparent, black, gray, white, yellow,     dark blue,  gray,         gray,        gray,        gray,        yellow,    gray,      white,      gray,       dark blue                used by: disk br, tl, lpen tl, bl, br
#12: transparent, black, gray, white, yellow,     dark blue,  yellow,       gray,        yellow,      yellow,      yellow,    gray,      bright red, bright red, bright red               used by: disk br, tl
 */

#define PAL_TRANS       0
#define PAL_BLACK       1
#define PAL_BBLUE       2
#define PAL_BGREEN      3
#define PAL_BRED        4
#define PAL_BCYAN       5
#define PAL_BYELLOW     6
#define PAL_BMAGENTA    7
#define PAL_WHITE       8
#define PAL_GRAY        9
#define PAL_DBLUE       10
#define PAL_DGREEN      11
#define PAL_DRED        12
#define PAL_DCYAN       13
#define PAL_DYELLOW     14
#define PAL_DMAGENTA    15

#define PAL_GREENBLUE   16
#define PAL_BLUEGREEN   17
#define PAL_REDGREEN    18
#define PAL_ORANGE      19
#define PAL_BLUERED     20
#define PAL_REDBLUE     21
#define PAL_BLUEGRAY    22
#define PAL_GREENGRAY   23
#define PAL_REDGRAY     24
#define PAL_CYANGRAY    25
#define PAL_YELLOWGRAY  26
#define PAL_MAGENTAGRAY 27
#define PAL_BLACK2      28
#define PAL_BBLUE2      29
#define PAL_BGREEN2     30
#define PAL_BRED2       31


/* palette animation for disk bl tile.  #0 is static (unanimated), rest get rotated */
static uint8_t const palette0[][16] = {
  {0, PAL_BLACK, PAL_GRAY, PAL_WHITE, PAL_BLACK,  PAL_GRAY,   PAL_BLACK,  PAL_BLACK,  PAL_GRAY,   PAL_BLACK,  PAL_GRAY,   PAL_BLACK,  PAL_WHITE,  0, 0, 0},
  {0, PAL_BLACK, PAL_GRAY, PAL_WHITE, PAL_ORANGE, PAL_ORANGE, PAL_ORANGE, PAL_BLACK,  PAL_GRAY,   PAL_BLACK,  PAL_GRAY,   PAL_BLACK,  PAL_WHITE,  0, 0, 0},
  {0, PAL_BLACK, PAL_GRAY, PAL_WHITE, PAL_ORANGE, PAL_ORANGE, PAL_BLACK,  PAL_ORANGE, PAL_ORANGE, PAL_BLACK,  PAL_GRAY,   PAL_BLACK,  PAL_WHITE,  0, 0, 0},
  {0, PAL_BLACK, PAL_GRAY, PAL_WHITE, PAL_ORANGE, PAL_ORANGE, PAL_BLACK,  PAL_BLACK,  PAL_GRAY,   PAL_ORANGE, PAL_ORANGE, PAL_BLACK,  PAL_WHITE,  0, 0, 0},
  {0, PAL_BLACK, PAL_GRAY, PAL_WHITE, PAL_ORANGE, PAL_ORANGE, PAL_BLACK,  PAL_BLACK,  PAL_GRAY,   PAL_BLACK,  PAL_GRAY,   PAL_ORANGE, PAL_ORANGE, 0, 0, 0},
};

/* palettes for disk tr, joystick tr.
   lpen tr, mouse tr use the first palette here(#1) as their first palette and switch using tile rewrite(need to change a few pixels as well). */
static uint8_t const palette1_2[2][16] = {
  {PAL_TRANS, PAL_BLACK, PAL_GRAY, PAL_WHITE, PAL_DBLUE, PAL_BRED, PAL_GRAY,   PAL_GRAY,  PAL_TRANS, PAL_BRED,  PAL_GRAY,  PAL_WHITE, PAL_BRED,  PAL_GRAY,  PAL_BLACK, PAL_BGREEN},
  {PAL_TRANS, PAL_BLACK, PAL_GRAY, PAL_WHITE, PAL_DBLUE, PAL_GRAY, PAL_BGREEN, PAL_TRANS, PAL_GRAY,  PAL_TRANS, PAL_WHITE, PAL_GRAY,  PAL_WHITE, PAL_BLACK, PAL_GRAY,  0},
};

/* palettes for disk br.
   joystick br uses the first two palettes.
   joystick tl, bl, mouse tl, bl, br and settings use the first palette.
   lpen tr and mouse tr use the second palette here(#9) as their second palette and switch using tile rewrite(need to change a few pixels as well).
   lpen tl, bl, br use the third palette.
   disk tl uses the third and fourth palette.
 */
static uint8_t const palette8[16] =
  {PAL_TRANS, PAL_BLACK, PAL_GRAY, PAL_WHITE, PAL_BYELLOW, PAL_BRED,  PAL_GRAY,    PAL_BYELLOW, PAL_BYELLOW, PAL_BYELLOW, PAL_GRAY,    PAL_BYELLOW, PAL_GRAY,  PAL_TRANS, PAL_WHITE, PAL_TRANS};
static uint8_t const palette9[16] =
  {PAL_TRANS, PAL_BLACK, PAL_GRAY, PAL_WHITE, PAL_BYELLOW, PAL_BRED,  PAL_BYELLOW, PAL_BYELLOW, PAL_GRAY,    PAL_BYELLOW, PAL_BYELLOW, PAL_GRAY,    PAL_TRANS, PAL_GRAY,  PAL_TRANS, PAL_WHITE};
static uint8_t const palette10[16] =
  {PAL_TRANS, PAL_BLACK, PAL_GRAY, PAL_WHITE, PAL_BYELLOW, PAL_DBLUE, PAL_GRAY,    PAL_GRAY,    PAL_GRAY,    PAL_GRAY,    PAL_BYELLOW, PAL_GRAY ,   PAL_WHITE, PAL_GRAY,  PAL_DBLUE, 0};
static uint8_t const palette12[16] =
  {PAL_TRANS, PAL_BLACK, PAL_GRAY, PAL_WHITE, PAL_BYELLOW, PAL_DBLUE, PAL_BYELLOW, PAL_GRAY,    PAL_BYELLOW, PAL_BYELLOW, PAL_BYELLOW, PAL_GRAY,    PAL_BRED,  PAL_BRED,  PAL_BRED,  0};

#define TILE_LINE(c1, c2, c3, c4, c5, c6, c7, c8, p12, p34, p56, p78)	\
	(((p12) << 8) | ((c2) << 4) | (c1) |				\
	 ((p34) << 24) | ((c4) << 20) | ((c3) << 16)),			\
	(((p56) << 8) | ((c6) << 4) | (c5) |				\
	 ((p78) << 24) | ((c8) << 20) | ((c7 << 16)))

static uint32_t const disktl[] = { //uses palettes #10, #12
#define TT 0
#define BB 1
#define GG 2
#define WW 3
#define DD 5
#define WR 12
#define GR 13
#define DR 14
    TILE_LINE(GG, GG, GG, GG, GG, GG, GG, GG,  0,0,0,0),
    TILE_LINE(GG, WW, WR, WR, WW, WW, WW, WW,  0,0,0,0),
    TILE_LINE(GG, WR, DR, DR, DR, WW, DD, DD,  0,0,0,0),
    TILE_LINE(GG, WR, GR, GR, GR, GG, GG, GG,  0,0,0,0),
    TILE_LINE(GG, WW, WR, WR, WW, WW, WW, WW,  0,0,0,0),
    TILE_LINE(GG, BB, BB, BB, BB, BB, BB, BB,  0,0,0,0),
    TILE_LINE(GG, BB, BB, BB, BB, BB, BB, BB,  0,0,0,0),
    TILE_LINE(GG, BB, BB, BB, BB, BB, WW, BB,  0,0,0,0),
#undef DD
#undef WR
#undef GR
#undef DR
      };

static uint32_t const disktr[] = { //uses palettes #1, #2
#define DD 4
#define RG 5
#define Gg 6
#define GT 7
#define TG 8
#define RT 9
#define GW 10
#define WG 11
#define RW 12
#define GB 13
#define BG 14
    TILE_LINE(GG, GG, GG, GG, GG, TT, GT, TT,  0,0,0,0),
    TILE_LINE(WW, GW, RW, GW, GG, GT, RG, GT,  0,0,0,0),
    TILE_LINE(DD, DD, GW, RW, GG, RG, Gg, TG,  0,0,0,0),
    TILE_LINE(GG, GG, GG, GW, RG, Gg, TG, TT,  0,0,0,0),
    TILE_LINE(WW, WG, Gg, RG, Gg, RG, GT, TT,  0,0,0,0),
    TILE_LINE(BB, GB, RG, Gg, GG, GT, RT, GT,  0,0,0,0),
    TILE_LINE(BB, BB, GB, BG, GG, TT, GT, TT,  0,0,0,0),
    TILE_LINE(BB, BB, BB, BB, GG, TT, TT, TT,  0,0,0,0),
#undef TT
#undef BB
#undef GG
#undef WW
#undef DD
#undef RG
#undef Gg
#undef GT
#undef TG
#undef RT
#undef GW
#undef WG
#undef RW
#undef GB
#undef BG
};

static uint32_t const diskbl[] = { //uses palettes #0
#define BBBBB 1
#define GGGGG 2
#define WWWWW 3
#define BOOOO 4
#define GOOOO 5
#define BOBBB 6
#define BBOBB 7
#define GGOGG 8
#define BBBOB 9
#define GGGOG 10
#define BBBBO 11
#define WWWWO 12
    TILE_LINE(GGGGG, BOBBB, BBBBB, BOOOO, BOOOO, WWWWW, WWWWW, WWWWW,  0,0,0,0),
    TILE_LINE(GGGGG, BOBBB, BOOOO, BBBBB, BBBBB, BOOOO, WWWWO, BBBBO,  0,0,0,0),
    TILE_LINE(GGGGG, BOOOO, BOBBB, BOBBB, BBBBB, BBBBO, BOOOO, BBBBB,  0,0,0,0),
    TILE_LINE(GOOOO, BBBBB, BBBBB, BBBBB, BBBBB, BBBBO, GGGGG, BOOOO,  0,0,0,0),
    TILE_LINE(GOOOO, BBBBB, BBOBB, BBBBB, BBBBB, GGGGG, GGGGG, GOOOO,  0,0,0,0),
    TILE_LINE(GGGGG, BOOOO, BBOBB, BBBBB, BBBOB, GGGOG, GOOOO, GGGGG,  0,0,0,0),
    TILE_LINE(GGOGG, BBOBB, BOOOO, BBBBB, BBBBB, BOOOO, GGGOG, BBBBB,  0,0,0,0),
    TILE_LINE(GGGGG, GGGGG, GGGGG, GOOOO, GOOOO, GGGGG, GGGOG, GGGGG,  0,0,0,0),
#undef BBBBB
#undef GGGGG
#undef WWWWW
#undef BOOOO
#undef GOOOO
#undef BOBBB
#undef BBOBB
#undef GGOGG
#undef BBBOB
#undef GGGOG
#undef BBBBO
#undef WWWWO
};

static uint32_t const diskbr[] = { //uses palettes #8-#10,#12
#define TTTT 0
#define BBBB 1
#define GGGG 2
#define WWWW 3
#define YYYY 4
#define GYGY 6
#define YYGG 7
#define YGGY 8
#define YYGY 9
#define GYYY 10
#define YGGG 11
    TILE_LINE(BBBB, BBBB, BBBB, BBBB, GGGG, TTTT, TTTT, TTTT,  0,0,0,0),
    TILE_LINE(BBBB, BBBB, BBBB, BBBB, GGGG, TTTT, TTTT, TTTT,  0,0,0,0),
    TILE_LINE(BBBB, BBBB, BBBB, BBBB, GGGG, GGGG, GGGG, TTTT,  0,0,0,0),
    TILE_LINE(BBBB, BBBB, BBBB, BBBB, GYGY, YYYY, GGGG, GGGG,  0,0,0,0),
    TILE_LINE(BBBB, BBBB, BBBB, BBBB, YYYY, GGGG, YYYY, GGGG,  0,0,0,0),
    TILE_LINE(BBBB, BBBB, BBBB, BBBB, YYYY, YYGG, YGGY, GGGG,  0,0,0,0),
    TILE_LINE(BBBB, BBBB, BBBB, BBBB, YYYY, GGGG, YYYY, GGGG,  0,0,0,0),
    TILE_LINE(GGGG, GGGG, GGGG, GGGG, YYGY, GYYY, YGGG, GGGG,  0,0,0,0),
#undef TTTT
#undef BBBB
#undef GGGG
#undef WWWW
#undef YYYY
#undef GYGY
#undef YYGG
#undef YGGY
#undef YYGY
#undef GYYY
#undef YGGG
};


static uint32_t const joytl[] = { //uses palettes #8
#define T 0
#define B 1
#define G 2
#define R 5
  TILE_LINE(T, T, T, T, G, G, G, G,  0,0,0,0),
  TILE_LINE(T, T, T, G, B, B, B, B,  0,0,0,0),
  TILE_LINE(T, T, G, B, B, R, R, B,  0,0,0,0),
  TILE_LINE(T, T, G, B, B, R, R, B,  0,0,0,0),
  TILE_LINE(T, T, G, B, B, R, R, B,  0,0,0,0),
  TILE_LINE(T, T, G, B, B, B, B, B,  0,0,0,0),
  TILE_LINE(T, T, T, G, B, B, B, B,  0,0,0,0),
  TILE_LINE(T, T, T, G, B, B, B, B,  0,0,0,0),
#undef T
#undef B
#undef G
#undef R
};

static uint32_t const joytr[] = { //uses palettes #1, #2
#define TT 0
#define BB 1
#define GG 2
#define WW 3
#define DD 4
#define RG 5
#define Gg 6
#define GT 7
#define TG 8
#define RT 9
#define GW 10
#define WG 11
#define RW 12
#define GB 13
#define BG 14
    TILE_LINE(TT, TT, GT, TT, TT, TT, GT, TT,  0,0,0,0),
    TILE_LINE(GG, GT, RT, GT, TT, GT, RG, GT,  0,0,0,0),
    TILE_LINE(BB, GG, GT, RT, GT, RG, Gg, TG,  0,0,0,0),
    TILE_LINE(BB, GG, TG, GT, RG, Gg, TG, TT,  0,0,0,0),
    TILE_LINE(BB, GG, Gg, RG, Gg, RG, GT, TT,  0,0,0,0),
    TILE_LINE(BB, GG, RG, Gg, TG, GT, RT, GT,  0,0,0,0),
    TILE_LINE(GG, GT, GT, TG, TT, TT, GT, TT,  0,0,0,0),
    TILE_LINE(GG, TT, TT, TT, TT, TT, TT, TT,  0,0,0,0),
#undef TT
#undef BB
#undef GG
#undef WW
#undef DD
#undef RG
#undef Gg
#undef GT
#undef TG
#undef RT
#undef GW
#undef WG
#undef RW
#undef GB
#undef BG
};

static uint32_t const joybl[] = { //uses palettes #8
#define T 0
#define B 1
#define G 2
#define R 5
  TILE_LINE(T, T, T, G, B, B, B, B,  0,0,0,0),
  TILE_LINE(T, T, T, G, B, B, B, B,  0,0,0,0),
  TILE_LINE(T, T, G, G, B, B, B, B,  0,0,0,0),
  TILE_LINE(T, G, R, R, R, R, R, R,  0,0,0,0),
  TILE_LINE(G, R, R, R, R, R, R, R,  0,0,0,0),
  TILE_LINE(G, R, R, R, R, R, R, R,  0,0,0,0),
  TILE_LINE(G, R, R, R, R, R, R, R,  0,0,0,0),
  TILE_LINE(G, G, G, G, G, G, G, G,  0,0,0,0),
#undef T
#undef B
#undef G
#undef R
};

static uint32_t const joybr[] = { //uses palettes #8, #9
#define TT 0
#define BB 1
#define GG 2
#define WW 3
#define YY 4
#define RR 5
#define GY 6
#define YG 8
    TILE_LINE(GG, TT, TT, TT, TT, TT, TT, TT,  0,0,0,0),
    TILE_LINE(GG, TT, TT, TT, TT, TT, TT, TT,  0,0,0,0),
    TILE_LINE(GG, GG, TT, TT, GG, GG, GG, TT,  0,0,0,0),
    TILE_LINE(RR, RR, GG, GG, GY, YY, GG, GG,  0,0,0,0),
    TILE_LINE(RR, RR, RR, GG, YG, YG, GY, GG,  0,0,0,0),
    TILE_LINE(RR, RR, RR, GG, GG, YY, GG, GG,  0,0,0,0),
    TILE_LINE(RR, RR, RR, GG, GG, YG, GG, GG,  0,0,0,0),
    TILE_LINE(GG, GG, GG, GG, YY, YY, YY, GG,  0,0,0,0),
#undef TT
#undef BB
#undef GG
#undef WW
#undef YY
#undef RR
#undef GY
#undef YG
};

static uint32_t const mousetl[] = { //uses palettes #8
#define T 0
#define G 2
#define W 3
#define R 5
  TILE_LINE(T, T, T, T, T, G, W, G,  0,0,0,0),
  TILE_LINE(G, G, G, G, G, G, W, G,  0,0,0,0),
  TILE_LINE(G, W, W, W, W, W, W, W,  0,0,0,0),
  TILE_LINE(G, R, R, R, W, R, R, R,  0,0,0,0),
  TILE_LINE(G, R, R, R, W, R, R, R,  0,0,0,0),
  TILE_LINE(G, R, R, R, W, R, R, R,  0,0,0,0),
  TILE_LINE(G, R, R, R, W, R, R, R,  0,0,0,0),
  TILE_LINE(G, R, R, R, W, R, R, R,  0,0,0,0),
#undef T
#undef G
#undef W
#undef R
};

static uint32_t const mousetr[][0x10] = {
  { //uses palettes #1
#define T 0
#define G 2
#define W 3
#define R 5
#define g 15
  TILE_LINE(T, T, G, T, T, T, G, T,  0,0,0,0),
  TILE_LINE(G, G, R, G, G, G, R, G,  0,0,0,0),
  TILE_LINE(W, W, G, R, G, R, G, T,  0,0,0,0),
  TILE_LINE(W, R, R, G, R, G, T, T,  0,0,0,0),
  TILE_LINE(W, G, G, R, G, R, G, T,  0,0,0,0),
  TILE_LINE(W, G, R, R, G, G, R, G,  0,0,0,0),
  TILE_LINE(W, G, G, G, G, T, G, T,  0,0,0,0),
  TILE_LINE(W, R, R, R, G, T, T, T,  0,0,0,0),
  },
  { //uses palette #1
  TILE_LINE(T, T, T, T, T, T, T, T,  0,0,0,0),
  TILE_LINE(G, G, G, G, G, T, G, T,  0,0,0,0),
  TILE_LINE(W, W, G, R, G, G, g, G,  0,0,0,0),
  TILE_LINE(W, R, G, G, G, g, G, T,  0,0,0,0),
  TILE_LINE(W, G, g, G, g, G, T, T,  0,0,0,0),
  TILE_LINE(W, R, G, g, G, T, T, T,  0,0,0,0),
  TILE_LINE(W, R, R, G, G, T, T, T,  0,0,0,0),
  TILE_LINE(W, R, R, R, G, T, T, T,  0,0,0,0),
#undef T
#undef G
#undef W
#undef R
#undef g
  }
};

static uint32_t const mousebl[] = { //uses palettes #8
#define T 0
#define G 2
#define W 3
  TILE_LINE(G, W, W, W, W, W, W, W,  0,0,0,0),
  TILE_LINE(G, G, W, G, W, G, W, G,  0,0,0,0),
  TILE_LINE(G, W, W, W, W, W, W, W,  0,0,0,0),
  TILE_LINE(G, W, W, W, W, W, W, W,  0,0,0,0),
  TILE_LINE(G, W, W, W, W, W, W, W,  0,0,0,0),
  TILE_LINE(G, W, W, W, W, W, W, W,  0,0,0,0),
  TILE_LINE(G, W, W, W, W, W, W, W,  0,0,0,0),
  TILE_LINE(G, G, G, G, G, G, G, G,  0,0,0,0),
};

static uint32_t const mousebr[] = { //uses palettes #8
  TILE_LINE(W, W, W, W, G, T, T, T,  0,0,0,0),
  TILE_LINE(W, G, W, G, G, T, T, T,  0,0,0,0),
  TILE_LINE(W, W, W, W, G, T, T, T,  0,0,0,0),
  TILE_LINE(W, W, W, W, G, T, T, T,  0,0,0,0),
  TILE_LINE(W, W, W, W, G, T, T, T,  0,0,0,0),
  TILE_LINE(W, W, W, W, G, T, T, T,  0,0,0,0),
  TILE_LINE(W, W, W, W, G, T, T, T,  0,0,0,0),
  TILE_LINE(G, G, G, G, G, T, T, T,  0,0,0,0),
#undef T
#undef G
#undef W
};


static uint32_t const lpentl[] = { //uses palettes #10
#define B 1
#define G 2
#define Y 4
#define D 5
  TILE_LINE(G, G, G, G, G, G, G, G,  0,0,0,0),
  TILE_LINE(B, B, B, B, B, B, B, B,  0,0,0,0),
  TILE_LINE(B, B, B, B, B, B, B, B,  0,0,0,0),
  TILE_LINE(D, D, D, D, D, D, G, D,  0,0,0,0),
  TILE_LINE(D, D, D, D, D, G, B, G,  0,0,0,0),
  TILE_LINE(D, Y, Y, Y, Y, G, B, G,  0,0,0,0),
  TILE_LINE(D, D, D, D, G, B, B, B,  0,0,0,0),
  TILE_LINE(Y, Y, D, Y, G, B, G, B,  0,0,0,0),
#undef B
#undef G
#undef Y
#undef D
};

static uint32_t const lpentr[][0x10] = { //uses palettes #1
#define T 0
#define B 1
#define G 2
#define D 4
#define R 5
#define g 15
  {
  TILE_LINE(G, G, G, G, G, G, G, T,  0,0,0,0),
  TILE_LINE(B, G, R, G, B, G, R, G,  0,0,0,0),
  TILE_LINE(B, B, G, R, G, R, G, T,  0,0,0,0),
  TILE_LINE(D, D, D, G, R, G, T, T,  0,0,0,0),
  TILE_LINE(D, D, G, R, G, R, G, T,  0,0,0,0),
  TILE_LINE(D, G, R, G, B, G, R, G,  0,0,0,0),
  TILE_LINE(G, D, G, B, B, G, G, T,  0,0,0,0),
  TILE_LINE(G, D, D, B, B, G, T, T,  0,0,0,0),
  },
  {
  TILE_LINE(G, G, G, G, G, G, T, T,  0,0,0,0),
  TILE_LINE(B, B, B, B, B, G, G, T,  0,0,0,0),
  TILE_LINE(B, B, B, B, B, G, g, G,  0,0,0,0),
  TILE_LINE(D, D, G, B, G, g, G, T,  0,0,0,0),
  TILE_LINE(D, G, g, G, g, G, T, T,  0,0,0,0),
  TILE_LINE(D, G, G, g, G, G, T, T,  0,0,0,0),
  TILE_LINE(G, D, D, G, B, G, T, T,  0,0,0,0),
  TILE_LINE(G, D, D, B, B, G, T, T,  0,0,0,0),
  }
#undef T
#undef B
#undef G
#undef D
#undef R
#undef g
};

static uint32_t const lpenbl[] = { //uses palettes #10
#define T 0
#define B 1
#define G 2
#define Y 4
#define D 5
  TILE_LINE(D, D, D, D, G, B, G, B,  0,0,0,0),
  TILE_LINE(Y, Y, Y, Y, G, B, B, B,  0,0,0,0),
  TILE_LINE(D, D, D, D, G, B, B, B,  0,0,0,0),
  TILE_LINE(D, D, D, D, G, B, B, B,  0,0,0,0),
  TILE_LINE(D, D, D, D, G, B, B, B,  0,0,0,0),
  TILE_LINE(Y, Y, Y, D, G, B, B, B,  0,0,0,0),
  TILE_LINE(D, D, D, D, G, B, B, B,  0,0,0,0),
  TILE_LINE(D, D, D, D, G, B, B, B,  0,0,0,0),
};

static uint32_t const lpenbr[] = { //uses palettes #10
  TILE_LINE(G, D, D, B, B, G, T, T,  0,0,0,0),
  TILE_LINE(G, D, D, B, B, G, T, T,  0,0,0,0),
  TILE_LINE(G, D, D, B, B, G, T, T,  0,0,0,0),
  TILE_LINE(G, D, D, B, B, G, T, T,  0,0,0,0),
  TILE_LINE(G, D, D, B, B, G, T, T,  0,0,0,0),
  TILE_LINE(G, D, D, B, B, G, T, T,  0,0,0,0),
  TILE_LINE(G, D, D, B, B, G, T, T,  0,0,0,0),
  TILE_LINE(G, D, D, B, B, G, T, T,  0,0,0,0),
#undef T
#undef B
#undef G
#undef Y
#undef D
};

static uint32_t const settingstl[] = { //uses palettes #8
#define T 0
#define G 2
#define W 3
  TILE_LINE(T, T, T, G, G, T, T, T,  0,0,0,0),
  TILE_LINE(T, T, G, W, W, G, T, T,  0,0,0,0),
  TILE_LINE(T, G, G, G, W, W, G, T,  0,0,0,0),
  TILE_LINE(G, W, G, G, G, W, G, T,  0,0,0,0),
  TILE_LINE(G, W, W, G, W, W, G, T,  0,0,0,0),
  TILE_LINE(T, G, W, W, W, W, W, G,  0,0,0,0),
  TILE_LINE(T, T, G, G, G, W, W, W,  0,0,0,0),
  TILE_LINE(T, T, T, T, T, G, W, W,  0,0,0,0),
#undef T
#undef G
#undef W
};

static uint32_t const settingstr_bl[] = { //tr uses palettes #8, bl uses #9
#define TT 0
#define GT 12
#define TG 13
#define WT 14
#define TW 15
  TILE_LINE(TT, TT, TT, TT, TT, TT, TG, TW,  0,0,0,0),
  TILE_LINE(TT, TT, TT, TT, TT, TT, TT, TG,  0,0,0,0),
  TILE_LINE(TT, TT, TT, TT, TT, TT, TT, TT,  0,0,0,0),
  TILE_LINE(TT, TT, TT, TT, TT, TT, TT, TT,  0,0,0,0),
  TILE_LINE(TT, TT, TT, TT, TT, TT, TT, TT,  0,0,0,0),
  TILE_LINE(TT, TT, TT, TT, TT, TT, TT, TT,  0,0,0,0),
  TILE_LINE(GT, TT, TT, TT, TT, TT, TT, TT,  0,0,0,0),
  TILE_LINE(WT, GT, TT, TT, TT, TT, TT, TT,  0,0,0,0),
#undef TT
#undef GT
#undef WT
#undef TG
#undef TW
};

static uint32_t const settingsbr[] = { //uses palettes #8
#define T 0
#define G 2
#define W 3
  TILE_LINE(W, W, G, G, G, T, T, T,  0,0,0,0),
  TILE_LINE(W, W, W, W, W, G, T, T,  0,0,0,0),
  TILE_LINE(G, W, W, G, W, W, G, T,  0,0,0,0),
  TILE_LINE(G, W, G, G, G, W, G, T,  0,0,0,0),
  TILE_LINE(G, W, W, G, G, G, T, T,  0,0,0,0),
  TILE_LINE(T, G, W, W, G, T, T, T,  0,0,0,0),
  TILE_LINE(T, T, G, G, T, T, T, T,  0,0,0,0),
  TILE_LINE(T, T, T, T, T, T, T, T,  0,0,0,0),
#undef T
#undef G
#undef W
};

#undef TILE_LINE

#define MAP(t, p) ((((t) & 0x7f) << 2) | ((p) << 17))
static uint32_t iconbar_map[36];

static uint16_t iconbar_mapbase;
static uint8_t iconbar_tile_disktlno;
static uint8_t iconbar_tile_disktrno;
static uint8_t iconbar_tile_diskblno;
static uint8_t iconbar_tile_diskbrno;
static uint8_t iconbar_tile_joytlno;
static uint8_t iconbar_tile_joytrno;
static uint8_t iconbar_tile_joyblno;
static uint8_t iconbar_tile_joybrno;
static uint8_t iconbar_tile_mousetlno;
static uint8_t iconbar_tile_mousetrno;
static uint8_t iconbar_tile_mouseblno;
static uint8_t iconbar_tile_mousebrno;
static uint8_t iconbar_tile_lpentlno;
static uint8_t iconbar_tile_lpentrno;
static uint8_t iconbar_tile_lpenblno;
static uint8_t iconbar_tile_lpenbrno;
static uint8_t iconbar_tile_settingstlno;
static uint8_t iconbar_tile_settingstr_blno;
static uint8_t iconbar_tile_settingsbrno;

static FPGA_Uploader iconbar_map_uploader;
static FPGA_Uploader iconbar_mousetr_uploader;
static FPGA_Uploader iconbar_lpentr_uploader;

static bool iconbar_disk_assigned[4];
static std::string iconbar_recent_disks[4];
static std::string iconbar_disk_displayname[4];
static std::string iconbar_joystick_displayname[2];
static std::string iconbar_mouse_displayname;
static std::string iconbar_lpen_displayname;

class IconBar_DiskMenu : public ui::Menu {
public:
  unsigned diskno;
  /*
        > insert disk (if none inserted)
        > new disk (if none inserted)
        > eject disk (if inserted)
	> ----
        > last 4 inserted disks  (if none inserted)
   */
  unsigned int getItemCount() {
    if(iconbar_disk_assigned[diskno])
      return 1;
    unsigned int num = 0;
    for(unsigned int i = 0; i < 4; i++) {
      if (!iconbar_recent_disks[i].empty())
	num++;
    }
    return num+2;
  }
  std::string getItemText(unsigned int index) {
    if(iconbar_disk_assigned[diskno])
      return "Eject Disk";
    else
      switch(index) {
      case 0: return "Insert Disk...";
      case 1: return "Create Disk...";
      case 2:
      case 3:
      case 4:
      case 5:
	return iconbar_recent_disks[index-2];
      default: return "";
      }
  }
  sigc::connection fileSelectedCon;
  sigc::connection fileSelectCanceledCon;
  void selectItem(int index);
  void fileSelectedOpen(std::string file);
  void fileSelectedCreate(std::string file);
  void fileSelectCanceled();
};

class IconBar_SettingsMenu : public ui::Menu {
public:
  /*
        > Display settings
   */
  unsigned int getItemCount() {
    return 1;
  }
  std::string getItemText(unsigned int /*index*/) {
    return "Display settings";
  }
  sigc::connection settingsClosedCon;
  void selectItem(int index);
  void settingsClosed();
};

static IconBar_DiskMenu iconbar_diskmenu;
static IconBar_SettingsMenu iconbar_settingsmenu;

class IconBar_Control : public ui::Control {
private:
  Sprite sprite;
  ui::Hint hint;
  int hintforicon;
  int press_iconno;
public:
  IconBar_Control() : hintforicon(-1), press_iconno(-1) {}
  void screenRectChange(ui::Rect const &r) {
    struct sprite_info info = {
      .hpos = (uint16_t)(r.x + r.width - 18*8),
      .vpos = (uint16_t)(r.y + r.height - 2*8),
      .map_addr = iconbar_mapbase,
      .hsize = 18,
      .vsize = 2,
      .hpitch = 18,
      .doublesize = 0,
      .reserved = 0,
    };
    sprite.setSpriteInfo(info);
  }
  void init() {
    struct sprite_info info = {
      .hpos = (uint16_t)(ui::screen.rect().x + ui::screen.rect().width - 18*8),
      .vpos = (uint16_t)(ui::screen.rect().y + ui::screen.rect().height - 2*8),
      .map_addr = iconbar_mapbase,
      .hsize = 18,
      .vsize = 2,
      .hpitch = 18,
      .doublesize = 0,
      .reserved = 0,
    };
    sprite.setSpriteInfo(info);
    sprite.setZOrder(0);
    sprite.setPriority(1);
    sprite.setVisible(true);
    ui::screen.onRectChange().connect
      (sigc::mem_fun(this, &IconBar_Control::screenRectChange));
  }
  virtual ui::Rect getRect() {
    sprite_info const &info = sprite.info();
    ui::Rect r = {
      .x = info.hpos,
      .y = info.vpos,
      .width = (uint16_t)(info.hsize*8),
      .height = (uint16_t)(info.vsize*8)
    };
    return r;
  }
  virtual ui::Rect getGlobalRect() {
    return getRect();
  }
  virtual void mouseDown(uint8_t button, ui::MouseState mousestate) {
    ui::Rect r = getGlobalRect();
    if (mousestate.x < r.x ||
	mousestate.y < r.y ||
	mousestate.x >= r.x+r.width ||
	mousestate.y >= r.y+r.height)
      return;
    if (button == 0 && mousestate.buttons == 1)
      press_iconno = (mousestate.x - r.x)/16;
  }
  virtual void mouseUp(uint8_t button, ui::MouseState mousestate) {
    ui::Rect r = getGlobalRect();
    if (mousestate.x < r.x ||
	mousestate.y < r.y ||
	mousestate.x >= r.x+r.width ||
	mousestate.y >= r.y+r.height)
      return;
    if (button == 0 && mousestate.buttons == 0) {
      int iconno = (mousestate.x - r.x)/16;
      if (iconno == press_iconno) {
	if (iconno >= 4 && iconno < 8) {
	  iconbar_diskmenu.diskno = iconno -4;
	  iconbar_diskmenu.setPosition(ui::Point(mousestate.x,mousestate.y));
	  iconbar_diskmenu.setVisible(true);
	  UI_setTopLevelControl(&iconbar_diskmenu);
	}
	if (iconno == 8) {
	  iconbar_settingsmenu.setPosition(ui::Point(mousestate.x,mousestate.y));
	  iconbar_settingsmenu.setVisible(true);
	  UI_setTopLevelControl(&iconbar_settingsmenu);
	}
	// do action
      }
    }
  }
  virtual void mouseMove(int16_t /*dx*/, int16_t /*dy*/, ui::MouseState mousestate) {
    ui::Rect r = getGlobalRect();
    if (mousestate.x < r.x ||
	mousestate.y < r.y ||
	mousestate.x >= r.x+r.width ||
	mousestate.y >= r.y+r.height ||
	mousestate.buttons) {
      hint.setVisible(false);
      hintforicon = -1;
      return;
    }
    int iconno = (mousestate.x - r.x)/16;
    if (hintforicon != iconno) {
      hintforicon = iconno;
      std::stringstream ss;
      if (iconno < 2) {
	ss << "Joystick " << (iconno - 0) << ": "
	   << iconbar_joystick_displayname[iconno];
      } else if (iconno < 3) {
	ss << "Mouse: " << iconbar_mouse_displayname;
      } else if (iconno < 4) {
	ss << "Light pen: " << iconbar_lpen_displayname;
      } else if (iconno < 8) {
	ss << "Disk drive " << (iconno-4) << ": "
	   << iconbar_disk_displayname[iconno-4];
      } else {
	ss << "Settings";
      }
      std::string text = ss.str();
      hint.setText(text);
      hint.setPosition(ui::Point
		       (ui::screen.rect().x + ui::screen.rect().width - text.size()*8,
			ui::screen.rect().y + ui::screen.rect().height - 3*8));
      hint.setVisible(true);
    }
  }
  //we are the top level, so we get all events. mouseleave never happens.
};

static IconBar_Control iconbar_control;
static ui::FileSelect iconbar_fileselect;
static ui::VideoSettings iconbar_videosettings;

static void IconBar_DeferredEjectDisk(unsigned drive) {
  FDC_EjectDisk(drive);
  IconBar_disk_unassigned(drive);
}

static void IconBar_DeferredOpenDisk(unsigned drive, std::string file) {
  FDC_EjectDisk(drive);
  FDC_InsertDisk(drive,file.c_str());
  IconBar_disk_assigned(drive, file.c_str());
}

static void IconBar_DeferredCreateDisk(unsigned drive, std::string file) {
  FDC_EjectDisk(drive);
  //todo
  //FDC_InsertDisk(drive,file.c_str());
  IconBar_disk_assigned(drive, file.c_str());
}

void IconBar_DiskMenu::selectItem(int index) {
  if (index == -1) { // menu was dismissed, do nothing
    iconbar_diskmenu.setVisible(false);
    UI_setTopLevelControl(&iconbar_control);
  }
  if (iconbar_disk_assigned[diskno]) {
    addDeferredWork(sigc::bind(sigc::ptr_fun(IconBar_DeferredEjectDisk),diskno));
    UI_setTopLevelControl(&iconbar_control);
    iconbar_diskmenu.setVisible(false);
  } else {
    if (index == 0) { // insert disk.
      iconbar_diskmenu.setVisible(false);
      iconbar_fileselect.setVisible(true);
      iconbar_fileselect.setActionText("Open");
      fileSelectedCon = iconbar_fileselect.onFileSelected().connect(sigc::mem_fun(this, &IconBar_DiskMenu::fileSelectedOpen));
      fileSelectCanceledCon = iconbar_fileselect.onCancel().connect(sigc::mem_fun(this, &IconBar_DiskMenu::fileSelectCanceled));
      UI_setTopLevelControl(&iconbar_fileselect);
    } else if (index == 1) { // new disk. todo: need to add restrictions to file chooser: accept directories(using action), accept existing files, accept only existing files, accept only non-existing files
      iconbar_diskmenu.setVisible(false);
      iconbar_fileselect.setVisible(true);
      iconbar_fileselect.setActionText("Create");
      fileSelectedCon = iconbar_fileselect.onFileSelected().connect(sigc::mem_fun(this, &IconBar_DiskMenu::fileSelectedCreate));
      fileSelectCanceledCon = iconbar_fileselect.onCancel().connect(sigc::mem_fun(this, &IconBar_DiskMenu::fileSelectCanceled));
      UI_setTopLevelControl(&iconbar_fileselect);
    } else if (index > 2+4) {
      iconbar_diskmenu.setVisible(false);
      UI_setTopLevelControl(&iconbar_control);
    } else if (iconbar_recent_disks[index - 2].empty()) {
      iconbar_diskmenu.setVisible(false);
      UI_setTopLevelControl(&iconbar_control);
    } else {
      iconbar_diskmenu.setVisible(false);
      UI_setTopLevelControl(&iconbar_control);
      addDeferredWork(sigc::bind(sigc::bind(sigc::ptr_fun(IconBar_DeferredOpenDisk),iconbar_recent_disks[index - 2]),diskno));
    }
  }
}

void IconBar_DiskMenu::fileSelectedOpen(std::string file) {
  iconbar_fileselect.setVisible(false);
  UI_setTopLevelControl(&iconbar_control);
  addDeferredWork(sigc::bind(sigc::bind(sigc::ptr_fun(IconBar_DeferredOpenDisk),file),diskno));
  fileSelectedCon.disconnect();
  fileSelectCanceledCon.disconnect();
}

void IconBar_DiskMenu::fileSelectedCreate(std::string file) {
  iconbar_fileselect.setVisible(false);
  UI_setTopLevelControl(&iconbar_control);
  addDeferredWork(sigc::bind(sigc::bind(sigc::ptr_fun(IconBar_DeferredCreateDisk),file),diskno));
  fileSelectedCon.disconnect();
  fileSelectCanceledCon.disconnect();
}

void IconBar_DiskMenu::fileSelectCanceled() {
  iconbar_fileselect.setVisible(false);
  UI_setTopLevelControl(&iconbar_control);
  fileSelectedCon.disconnect();
  fileSelectCanceledCon.disconnect();
}

void IconBar_SettingsMenu::selectItem(int index) {
  if (index == -1) { // menu was dismissed, do nothing
    iconbar_settingsmenu.setVisible(false);
    UI_setTopLevelControl(&iconbar_control);
  }
  if (index == 0) {
    iconbar_settingsmenu.setVisible(false);
    iconbar_videosettings.setVisible(true);
    settingsClosedCon = iconbar_videosettings.onClose().connect(sigc::mem_fun(this, &IconBar_SettingsMenu::settingsClosed));
    UI_setTopLevelControl(&iconbar_videosettings);
  }
}

void IconBar_SettingsMenu::settingsClosed() {
  iconbar_videosettings.setVisible(false);
  UI_setTopLevelControl(&iconbar_control);
  settingsClosedCon.disconnect();
}

static void iconbar_upload_tile(unsigned no, uint32_t const *data) {
  FPGAComm_CopyToFPGA(FPGA_GRPH_SPRITES_RAM+no*0x10*4, data, 0x10*4);
}

static void iconbar_queue_map_update() {
  iconbar_map_uploader.setDest(FPGA_GRPH_SPRITES_RAM + iconbar_mapbase*4);
  iconbar_map_uploader.setSrc(iconbar_map);
  iconbar_map_uploader.setSize(sizeof(iconbar_map));
  iconbar_map_uploader.triggerUpload();
}

void IconBar_disk_assigned(unsigned no, char const *displayname) {
  if (no >= 4)
    return;
  iconbar_disk_assigned[no] = true;
  iconbar_disk_displayname[no] = displayname;
  std::string t = displayname;
  for(unsigned int i = 0; i < 4; i++) {
    std::string t2 = iconbar_recent_disks[i];
    iconbar_recent_disks[i] = t;
    t = t2;
    if (t == displayname)
      break;
  }

  iconbar_map[9+no*2] = MAP(iconbar_tile_disktrno, 2);

  iconbar_queue_map_update();
}

void IconBar_disk_unassigned(unsigned no) {
  if (no >= 4)
    return;
  iconbar_disk_assigned[no] = false;
  iconbar_disk_displayname[no] = "not assigned";
  iconbar_map[9+no*2] = MAP(iconbar_tile_disktrno, 1);

  iconbar_queue_map_update();
}

void IconBar_disk_activity(unsigned no, bool activity) {
  if (no >= 4)
    return;
  iconbar_map[8+no*2] = MAP(iconbar_tile_disktlno, activity?12:10);

  iconbar_queue_map_update();
}

static unsigned iconbar_disk_motor_anim;
static uint32_t iconbar_palette0_timer = 0;

static void iconbar_disk_motor_timer(void *) {
  sprite_set_palette(0, palette0[iconbar_disk_motor_anim+1]);
  sprite_upload_palette();

  iconbar_disk_motor_anim++;
  if (iconbar_disk_motor_anim >= 4)
    iconbar_disk_motor_anim = 0;
}

void IconBar_disk_motor_on() {
  ISR_Guard g;
  if (iconbar_palette0_timer)
    return;
  iconbar_disk_motor_timer(NULL);

  iconbar_palette0_timer = Timer_Repeating(100000, iconbar_disk_motor_timer,
					   NULL);
}

void IconBar_disk_motor_off() {
  ISR_Guard g;
  if (!iconbar_palette0_timer)
    return;
  Timer_Cancel(iconbar_palette0_timer);
  iconbar_palette0_timer = 0;

  sprite_set_palette(0, palette0[0]);
  sprite_upload_palette();
}

void IconBar_joystick_assigned(unsigned no, char const *displayname) {
  if (no >= 2)
    return;
  iconbar_joystick_displayname[no] = displayname;
  iconbar_map[1+no*2] = MAP(iconbar_tile_joytrno, 2);

  iconbar_queue_map_update();
}

void IconBar_joystick_unassigned(unsigned no) {
  if (no >= 2)
    return;
  iconbar_joystick_displayname[no] = "not assigned";
  iconbar_map[1+no*2] = MAP(iconbar_tile_joytrno, 1);

  iconbar_queue_map_update();
}

void IconBar_mouse_assigned(char const *displayname) {
  iconbar_mouse_displayname = displayname;
  iconbar_mousetr_uploader.setDest(FPGA_GRPH_SPRITES_RAM +
				   iconbar_tile_mousetrno * 64);
  iconbar_mousetr_uploader.setSrc(mousetr[1]);
  iconbar_mousetr_uploader.setSize(64);
  iconbar_mousetr_uploader.triggerUpload();
}

void IconBar_mouse_unassigned() {
  iconbar_mouse_displayname = "not assigned";
  iconbar_mousetr_uploader.setDest(FPGA_GRPH_SPRITES_RAM +
				   iconbar_tile_mousetrno * 64);
  iconbar_mousetr_uploader.setSrc(mousetr[0]);
  iconbar_mousetr_uploader.setSize(64);
  iconbar_mousetr_uploader.triggerUpload();
}

void IconBar_lpen_assigned(char const *displayname) {
  iconbar_lpen_displayname = displayname;
  iconbar_lpentr_uploader.setDest(FPGA_GRPH_SPRITES_RAM +
				  iconbar_tile_lpentrno * 64);
  iconbar_lpentr_uploader.setSrc(lpentr[1]);
  iconbar_lpentr_uploader.setSize(64);
  iconbar_lpentr_uploader.triggerUpload();
}

void IconBar_lpen_unassigned() {
  iconbar_lpen_displayname = "not assigned";
  iconbar_lpentr_uploader.setDest(FPGA_GRPH_SPRITES_RAM +
				  iconbar_tile_lpentrno * 64);
  iconbar_lpentr_uploader.setSrc(lpentr[0]);
  iconbar_lpentr_uploader.setSize(64);
  iconbar_lpentr_uploader.triggerUpload();
}

void IconBar_Setup() {
  for(unsigned i = 0; i < 4; i++)
    iconbar_disk_displayname[i] = "not assigned";
  for(unsigned i = 0; i < 2; i++)
    iconbar_joystick_displayname[i] = "not assigned";
  iconbar_mouse_displayname = "not assigned";
  iconbar_lpen_displayname = "not assigned";

  //allocate and upload the default tiles
  sprite_set_palette(0, palette0[0]);
  sprite_set_palette(1, palette1_2[0]);
  sprite_set_palette(2, palette1_2[1]);
  sprite_set_palette(8, palette8);
  sprite_set_palette(9, palette9);
  sprite_set_palette(10, palette10);
  sprite_set_palette(12, palette12);
  sprite_upload_palette();

  iconbar_mapbase = sprite_alloc_vmem(sizeof(iconbar_map)/4, 1, ~0U);
  iconbar_tile_disktlno = sprite_alloc_vmem(0x10, 0x10, ~0U) / 0x10;
  iconbar_tile_disktrno = sprite_alloc_vmem(0x10, 0x10, ~0U) / 0x10;
  iconbar_tile_diskblno = sprite_alloc_vmem(0x10, 0x10, ~0U) / 0x10;
  iconbar_tile_diskbrno = sprite_alloc_vmem(0x10, 0x10, ~0U) / 0x10;
  iconbar_tile_joytlno = sprite_alloc_vmem(0x10, 0x10, ~0U) / 0x10;
  iconbar_tile_joytrno = sprite_alloc_vmem(0x10, 0x10, ~0U) / 0x10;
  iconbar_tile_joyblno = sprite_alloc_vmem(0x10, 0x10, ~0U) / 0x10;
  iconbar_tile_joybrno = sprite_alloc_vmem(0x10, 0x10, ~0U) / 0x10;
  iconbar_tile_mousetlno = sprite_alloc_vmem(0x10, 0x10, ~0U) / 0x10;
  iconbar_tile_mousetrno = sprite_alloc_vmem(0x10, 0x10, ~0U) / 0x10;
  iconbar_tile_mouseblno = sprite_alloc_vmem(0x10, 0x10, ~0U) / 0x10;
  iconbar_tile_mousebrno = sprite_alloc_vmem(0x10, 0x10, ~0U) / 0x10;
  iconbar_tile_lpentlno = sprite_alloc_vmem(0x10, 0x10, ~0U) / 0x10;
  iconbar_tile_lpentrno = sprite_alloc_vmem(0x10, 0x10, ~0U) / 0x10;
  iconbar_tile_lpenblno = sprite_alloc_vmem(0x10, 0x10, ~0U) / 0x10;
  iconbar_tile_lpenbrno = sprite_alloc_vmem(0x10, 0x10, ~0U) / 0x10;
  iconbar_tile_settingstlno = sprite_alloc_vmem(0x10, 0x10, ~0U) / 0x10;
  iconbar_tile_settingstr_blno = sprite_alloc_vmem(0x10, 0x10, ~0U) / 0x10;
  iconbar_tile_settingsbrno = sprite_alloc_vmem(0x10, 0x10, ~0U) / 0x10;

  iconbar_upload_tile(iconbar_tile_disktlno, disktl);
  iconbar_upload_tile(iconbar_tile_disktrno, disktr);
  iconbar_upload_tile(iconbar_tile_diskblno, diskbl);
  iconbar_upload_tile(iconbar_tile_diskbrno, diskbr);
  iconbar_upload_tile(iconbar_tile_joytlno, joytl);
  iconbar_upload_tile(iconbar_tile_joytrno, joytr);
  iconbar_upload_tile(iconbar_tile_joyblno, joybl);
  iconbar_upload_tile(iconbar_tile_joybrno, joybr);
  iconbar_upload_tile(iconbar_tile_mousetlno, mousetl);
  iconbar_upload_tile(iconbar_tile_mousetrno, mousetr[0]);
  iconbar_upload_tile(iconbar_tile_mouseblno, mousebl);
  iconbar_upload_tile(iconbar_tile_mousebrno, mousebr);
  iconbar_upload_tile(iconbar_tile_lpentlno, lpentl);
  iconbar_upload_tile(iconbar_tile_lpentrno, lpentr[0]);
  iconbar_upload_tile(iconbar_tile_lpenblno, lpenbl);
  iconbar_upload_tile(iconbar_tile_lpenbrno, lpenbr);
  iconbar_upload_tile(iconbar_tile_settingstlno, settingstl);
  iconbar_upload_tile(iconbar_tile_settingstr_blno, settingstr_bl);
  iconbar_upload_tile(iconbar_tile_settingsbrno, settingsbr);

  iconbar_map[0] = MAP(iconbar_tile_joytlno, 8);
  iconbar_map[1] = MAP(iconbar_tile_joytrno, 1);
  iconbar_map[2] = MAP(iconbar_tile_joytlno, 8);
  iconbar_map[3] = MAP(iconbar_tile_joytrno, 1);
  iconbar_map[4] = MAP(iconbar_tile_mousetlno, 8);
  iconbar_map[5] = MAP(iconbar_tile_mousetrno, 1); //the tile gets updated depending on mouse.assigned
  iconbar_map[6] = MAP(iconbar_tile_lpentlno, 10);
  iconbar_map[7] = MAP(iconbar_tile_lpentrno, 1); //the tile gets updated depending on mouse.assigned
  iconbar_map[8] = MAP(iconbar_tile_disktlno, 10);
  iconbar_map[9] = MAP(iconbar_tile_disktrno, 1);
  iconbar_map[10] = MAP(iconbar_tile_disktlno, 10);
  iconbar_map[11] = MAP(iconbar_tile_disktrno, 1);
  iconbar_map[12] = MAP(iconbar_tile_disktlno, 10);
  iconbar_map[13] = MAP(iconbar_tile_disktrno, 1);
  iconbar_map[14] = MAP(iconbar_tile_disktlno, 10);
  iconbar_map[15] = MAP(iconbar_tile_disktrno, 1);
  iconbar_map[16] = MAP(iconbar_tile_settingstlno, 8);
  iconbar_map[17] = MAP(iconbar_tile_settingstr_blno, 8);
  iconbar_map[18] = MAP(iconbar_tile_joyblno, 8);
  iconbar_map[19] = MAP(iconbar_tile_joybrno, 8);
  iconbar_map[20] = MAP(iconbar_tile_joyblno, 8);
  iconbar_map[21] = MAP(iconbar_tile_joybrno, 9);
  iconbar_map[22] = MAP(iconbar_tile_mouseblno, 8);
  iconbar_map[23] = MAP(iconbar_tile_mousebrno, 8);
  iconbar_map[24] = MAP(iconbar_tile_lpenblno, 10);
  iconbar_map[25] = MAP(iconbar_tile_lpenbrno, 10);
  iconbar_map[26] = MAP(iconbar_tile_diskblno, 0); //state and animation by rewriting palette 0
  iconbar_map[27] = MAP(iconbar_tile_diskbrno, 8);
  iconbar_map[28] = MAP(iconbar_tile_diskblno, 0); //state and animation by rewriting palette 0
  iconbar_map[29] = MAP(iconbar_tile_diskbrno, 9);
  iconbar_map[30] = MAP(iconbar_tile_diskblno, 0); //state and animation by rewriting palette 0
  iconbar_map[31] = MAP(iconbar_tile_diskbrno, 10);
  iconbar_map[32] = MAP(iconbar_tile_diskblno, 0); //state and animation by rewriting palette 0
  iconbar_map[33] = MAP(iconbar_tile_diskbrno, 12);
  iconbar_map[34] = MAP(iconbar_tile_settingstr_blno, 9);
  iconbar_map[35] = MAP(iconbar_tile_settingsbrno, 8);

  FPGAComm_CopyToFPGA(FPGA_GRPH_SPRITES_RAM+iconbar_mapbase*4, iconbar_map, sizeof(iconbar_map));

  iconbar_fileselect.setFolder("/media");

  iconbar_control.init();
  UI_setTopLevelControl(&iconbar_control);
}

/*
random crap:

disk top left tile (variable piece only)
____  _rr_
_ddd  rrrr
_xxx  rrrr
____  _rr_

_r
dr
xr

disk top right tile

xxxxx.x.  xxxxx...
_xrxxxrx  ____x.x.
ddxrxrx.  dd__xxgx
xxxxrx..  xxxWxgx.
__xrxrx.  _xgxgx..
Wxrxxxrx  WWxgx...
WWxWx.x.  WWWxx...
WWWWx...  WWWWx...

xx
..
__
WW
dd
rx
xg
x.
.x
r.
x_
_x
r_
xW
Wx

disk bottom right tile (variable piece only)
_X_   XX_  _X_  XX_
X_X   X_X  X_X  X_X
XXX   XX_  X__  X_X
X_X   X_X  X_X  X_X
X_X   XX_  _X_  XX_

____
XXXX
_X_X
XX__
X__X
XX_X
_XXX
X___

disk bottom left tile
xWWWW___  xoWoo___    xWWoo___
xWWWWW_W  xooWWo_W    xWoWWooo
xWWWWWWW  xoooWWoW    xoWWWooW
xWWWWWxW  oWWWWWxo    oWWWWoxo
xWWWWxxx  oWWWWxxo    oWoWWxxo
xWWWWxxx  xoWWooox    xooWWxox
xWWWWWxW  xWoWWooW    oooWWoxW
xxxxxxxx  xxxooxox    xxxooxxx

xxx
WWW
___
__o
WoW
Woo
WWo      
xoo
xox
xxo

disk top left tile uses black, gray, white, dark blue, 1x(white/bright red), 1x(gray/bright red), 1x (dark blue/bright red) (=>7), in 2 palettes. allocating color index 1,2,4,5, 7-9
                           can be combined with mouse top right tile or disk bottom right tile
disk top right tile uses transparent, black, gray, white, dark blue, 1x(bright red/gray), 1x(gray/bright green), 1x(gray/transparent), 1x(transparent/gray), 
		           1x(bright red/transparent), 1x(gray/white), 1x(white/gray), 1x(bright red/white), 1x(gray/black), 1x(black/gray) (=>15)
                           in 2 palettes. allocating color index 0,1,2,4,5,6-15
disk bottom right tile uses transparent, black, gray, yellow, 6x(yellow/gray)  (=>10), in 4 palettes. allocating color index 0-3, 10-15
                           can be combined with disk top left tile
disk bottom left tile uses black, gray, white, 1x(white/orange), 3x(black/orange), 3x(gray/orange), in 1 palette(animated). allocating color index 1,2,4, 9-15

mouse top right tile

..
xx
rx
x.
__
x_
r_
xg
.x
rr
xr
r.

mouse top left tile uses transparent, gray, white, bright red
mouse top right tile uses transparent, gray, white, bright red, 1x(bright red/gray), 1x(gray/transparent), 1x(gray/white), 1x(bright red/white), 1x(gray/bright green),
                          1x(transparent/gray), 1x(gray/bright red), 1x(bright red/transparent) (=>12), in 2 palettes.
                          can be combined with disk top left tile
mouse bottom left tile uses gray, white
mouse bottom right tile uses transparent, gray, white

joystick top left tile probably can reuse the first disk top right palette
joystick top right tile probably can reuse disk top right palette
joystick bottom left tile probably can reuse the first disk top right palette
joystick bottom right tile uses transparent, black, gray, yellow, bright red, 2x(gray/yellow). can be combined with disk bottom right tile.

lpen can probably use disk bottom right tile+disk top left tile
 */

// kate: indent-width 2; indent-mode cstyle;
