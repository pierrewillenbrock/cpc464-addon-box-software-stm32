
#include "iconbar_control.hpp"

#include <fpga/sprite.hpp>
#include <fpga/fpga_comm.h>
#include <fpga/layout.h>
#include <fpga/fpga_uploader.hpp>
#include <timer.h>

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

static void iconbar_upload_tile(unsigned no, uint32_t const *data) {
  FPGAComm_CopyToFPGA(FPGA_GRPH_SPRITES_RAM+no*0x10*4, data, 0x10*4);
}

void IconBar_Control::init() {
  //allocate and upload the default tiles
  sprite_set_palette(0, palette0[0]);
  sprite_set_palette(1, palette1_2[0]);
  sprite_set_palette(2, palette1_2[1]);
  sprite_set_palette(8, palette8);
  sprite_set_palette(9, palette9);
  sprite_set_palette(10, palette10);
  sprite_set_palette(12, palette12);
  sprite_upload_palette();

  tile_disktlno = sprite_alloc_vmem(0x10, 0x10, ~0U) / 0x10;
  tile_disktrno = sprite_alloc_vmem(0x10, 0x10, ~0U) / 0x10;
  tile_diskblno = sprite_alloc_vmem(0x10, 0x10, ~0U) / 0x10;
  tile_diskbrno = sprite_alloc_vmem(0x10, 0x10, ~0U) / 0x10;
  tile_joytlno = sprite_alloc_vmem(0x10, 0x10, ~0U) / 0x10;
  tile_joytrno = sprite_alloc_vmem(0x10, 0x10, ~0U) / 0x10;
  tile_joyblno = sprite_alloc_vmem(0x10, 0x10, ~0U) / 0x10;
  tile_joybrno = sprite_alloc_vmem(0x10, 0x10, ~0U) / 0x10;
  tile_mousetlno = sprite_alloc_vmem(0x10, 0x10, ~0U) / 0x10;
  tile_mousetrno = sprite_alloc_vmem(0x10, 0x10, ~0U) / 0x10;
  tile_mouseblno = sprite_alloc_vmem(0x10, 0x10, ~0U) / 0x10;
  tile_mousebrno = sprite_alloc_vmem(0x10, 0x10, ~0U) / 0x10;
  tile_lpentlno = sprite_alloc_vmem(0x10, 0x10, ~0U) / 0x10;
  tile_lpentrno = sprite_alloc_vmem(0x10, 0x10, ~0U) / 0x10;
  tile_lpenblno = sprite_alloc_vmem(0x10, 0x10, ~0U) / 0x10;
  tile_lpenbrno = sprite_alloc_vmem(0x10, 0x10, ~0U) / 0x10;
  tile_settingstlno = sprite_alloc_vmem(0x10, 0x10, ~0U) / 0x10;
  tile_settingstr_blno = sprite_alloc_vmem(0x10, 0x10, ~0U) / 0x10;
  tile_settingsbrno = sprite_alloc_vmem(0x10, 0x10, ~0U) / 0x10;

  iconbar_upload_tile(tile_disktlno, disktl);
  iconbar_upload_tile(tile_disktrno, disktr);
  iconbar_upload_tile(tile_diskblno, diskbl);
  iconbar_upload_tile(tile_diskbrno, diskbr);
  iconbar_upload_tile(tile_joytlno, joytl);
  iconbar_upload_tile(tile_joytrno, joytr);
  iconbar_upload_tile(tile_joyblno, joybl);
  iconbar_upload_tile(tile_joybrno, joybr);
  iconbar_upload_tile(tile_mousetlno, mousetl);
  iconbar_upload_tile(tile_mousetrno, mousetr[0]);
  iconbar_upload_tile(tile_mouseblno, mousebl);
  iconbar_upload_tile(tile_mousebrno, mousebr);
  iconbar_upload_tile(tile_lpentlno, lpentl);
  iconbar_upload_tile(tile_lpentrno, lpentr[0]);
  iconbar_upload_tile(tile_lpenblno, lpenbl);
  iconbar_upload_tile(tile_lpenbrno, lpenbr);
  iconbar_upload_tile(tile_settingstlno, settingstl);
  iconbar_upload_tile(tile_settingstr_blno, settingstr_bl);
  iconbar_upload_tile(tile_settingsbrno, settingsbr);

  m_sprite.setPriority(10);
  m_sprite.setZOrder(20);
  m_sprite.setDoubleSize(false);
  m_sprite.setPosition(ui::screen.rect().x + ui::screen.rect().width - 18*8,
           ui::screen.rect().y + ui::screen.rect().height - 2*8);
  m_sprite.setSize(18,2);

  map(0, 0) = MAP(tile_joytlno, 8);
  map(1, 0) = MAP(tile_joytrno, 1);
  map(2, 0) = MAP(tile_joytlno, 8);
  map(3, 0) = MAP(tile_joytrno, 1);
  map(4, 0) = MAP(tile_mousetlno, 8);
  map(5, 0) = MAP(tile_mousetrno, 1); //the tile gets updated depending on mouse.assigned
  map(6, 0) = MAP(tile_lpentlno, 10);
  map(7, 0) = MAP(tile_lpentrno, 1); //the tile gets updated depending on mouse.assigned
  map(8, 0) = MAP(tile_disktlno, 10);
  map(9, 0) = MAP(tile_disktrno, 1);
  map(10, 0) = MAP(tile_disktlno, 10);
  map(11, 0) = MAP(tile_disktrno, 1);
  map(12, 0) = MAP(tile_disktlno, 10);
  map(13, 0) = MAP(tile_disktrno, 1);
  map(14, 0) = MAP(tile_disktlno, 10);
  map(15, 0) = MAP(tile_disktrno, 1);
  map(16, 0) = MAP(tile_settingstlno, 8);
  map(17, 0) = MAP(tile_settingstr_blno, 8);
  map(0, 1) = MAP(tile_joyblno, 8);
  map(1, 1) = MAP(tile_joybrno, 8);
  map(2, 1) = MAP(tile_joyblno, 8);
  map(3, 1) = MAP(tile_joybrno, 9);
  map(4, 1) = MAP(tile_mouseblno, 8);
  map(5, 1) = MAP(tile_mousebrno, 8);
  map(6, 1) = MAP(tile_lpenblno, 10);
  map(7, 1) = MAP(tile_lpenbrno, 10);
  map(8, 1) = MAP(tile_diskblno, 0); //state and animation by rewriting palette 0
  map(9, 1) = MAP(tile_diskbrno, 8);
  map(10, 1) = MAP(tile_diskblno, 0); //state and animation by rewriting palette 0
  map(11, 1) = MAP(tile_diskbrno, 9);
  map(12, 1) = MAP(tile_diskblno, 0); //state and animation by rewriting palette 0
  map(13, 1) = MAP(tile_diskbrno, 10);
  map(14, 1) = MAP(tile_diskblno, 0); //state and animation by rewriting palette 0
  map(15, 1) = MAP(tile_diskbrno, 12);
  map(16, 1) = MAP(tile_settingstr_blno, 9);
  map(17, 1) = MAP(tile_settingsbrno, 8);

  m_sprite.updateDone();

  m_sprite.setVisible(true);
  ui::screen.onRectChange().connect
  (sigc::mem_fun(this, &IconBar_Control::screenRectChange));
}

void IconBar_Control::mouseDown(uint8_t button, ui::MouseState mousestate) {
  ui::Rect r = getGlobalRect();
  hint.setVisible(false);
  hintforicon = -1;
  if(mousestate.x < r.x ||
      mousestate.y < r.y ||
      mousestate.x >= r.x+r.width ||
      mousestate.y >= r.y+r.height)
    return;
  if(button == 0 && mousestate.buttons == 1)
    mouse_press_iconno = (mousestate.x - r.x)/16;
}

void IconBar_Control::mouseUp(uint8_t button, ui::MouseState mousestate) {
  ui::Rect r = getGlobalRect();
  hint.setVisible(false);
  hintforicon = -1;
  if(mousestate.x < r.x ||
      mousestate.y < r.y ||
      mousestate.x >= r.x+r.width ||
      mousestate.y >= r.y+r.height)
    return;
  if(button == 0 && mousestate.buttons == 0) {
    int iconno = (mousestate.x - r.x)/16;
    if(iconno == mouse_press_iconno) {
      if(iconno >= 0 && iconno <= 1) {
	joystickSelected(iconno - 0, ui::Point(mousestate.x,mousestate.y));
      }
      if(iconno == 2) {
	mouseSelected(ui::Point(mousestate.x,mousestate.y));
      }
      if(iconno == 3) {
	lpenSelected(ui::Point(mousestate.x,mousestate.y));
      }
      if(iconno >= 4 && iconno < 8) {
	diskSelected(iconno - 4, ui::Point(mousestate.x,mousestate.y));
      }
      if(iconno == 8) {
	settingsSelected(ui::Point(mousestate.x,mousestate.y));
      }
      // do action
    }
  }
}

void IconBar_Control::mouseMove(int16_t /*dx*/, int16_t /*dy*/, ui::MouseState mousestate) {
  ui::Rect r = getGlobalRect();
  if(mousestate.x < r.x ||
      mousestate.y < r.y ||
      mousestate.x >= r.x+r.width ||
      mousestate.y >= r.y+r.height ||
      mousestate.buttons) {
    hint.setVisible(false);
    keyjoy_sel_iconno = -1;
    hintforicon = -1;
    return;
  }
  int iconno = (mousestate.x - r.x)/16;
  showHint(iconno, mousestate.x);
}

void IconBar_Control::joyTrgDown(ui::JoyTrg trg, ui::JoyState /*state*/) {
  switch(trg) {
  case ui::JoyTrg::Left: {
    //move selection left
    //the selected item gets its hint shown and should at some point gain another indicator, possibly in the map? \todo
    if (keyjoy_sel_iconno == -1 || keyjoy_sel_iconno == 0)
      keyjoy_sel_iconno = 8;
    else
      keyjoy_sel_iconno--;
    ui::Rect r = getGlobalRect();
    showHint(keyjoy_sel_iconno, r.x + 16 * keyjoy_sel_iconno);
    break;
  }
  case ui::JoyTrg::Right: {
    //move selection right
    if (keyjoy_sel_iconno == -1 || keyjoy_sel_iconno == 8)
      keyjoy_sel_iconno = 0;
    else
      keyjoy_sel_iconno++;
    ui::Rect r = getGlobalRect();
    showHint(keyjoy_sel_iconno, r.x + 16 * keyjoy_sel_iconno);
    break;
  }
  default:
    break;
  }
}

void IconBar_Control::joyTrgUp(ui::JoyTrg trg, ui::JoyState /*state*/) {
  switch(trg) {
  case ui::JoyTrg::Btn1: {
    unsigned iconno = keyjoy_sel_iconno;
    hint.setVisible(false);
    keyjoy_sel_iconno = -1;
    hintforicon = -1;
    ui::Rect r = getGlobalRect();
    ui::Point p = {r.x + 16 * iconno, r.y};
    //select the current icon
    if(iconno >= 0 && iconno <= 1) {
      joystickSelected(iconno - 0, p);
    }
    if(iconno == 2) {
      mouseSelected(p);
    }
    if(iconno == 3) {
      lpenSelected(p);
    }
    if(iconno >= 4 && iconno < 8) {
      diskSelected(iconno - 4, p);
    }
    if(iconno == 8) {
      settingsSelected(p);
    }
    break;
  }
  case ui::JoyTrg::Btn2:
    //deselect selection
    hint.setVisible(false);
    keyjoy_sel_iconno = -1;
    hintforicon = -1;
    break;
  default:
    break;
  }
}

void IconBar_Control::showHint(int iconno, unsigned int x) {
  if(hintforicon != iconno) {
    hintforicon = iconno;
    std::stringstream ss;
    if(iconno < 2) {
      ss << joystickHint[iconno];
    } else if(iconno < 3) {
      ss << mouseHint;
    } else if(iconno < 4) {
      ss << lpenHint;
    } else if(iconno < 8) {
      ss << diskHint[iconno-4];
    } else {
      ss << "Settings";
    }
    std::string text = ss.str();
    hint.setText(text);
    hint.setPosition(ui::Point
         (x - text.size()*8,
          ui::screen.rect().y + ui::screen.rect().height - 3*8));
    hint.setVisible(true);
  }
}

void IconBar_Control::setDiskAssigned(unsigned no, bool assigned) {
  if (no >= 4)
    return;

  map(9+no*2, 0) = MAP(tile_disktrno, assigned?2:1);
  updateDone();
}

void IconBar_Control::setDiskActivity(unsigned no, bool activity) {
  if (no >= 4)
    return;

  map(8+no*2,0) = MAP(tile_disktlno, activity?12:10);
  updateDone();
}

void IconBar_Control::diskMotorTimeout(void *data) {
  IconBar_Control *_this = (IconBar_Control*)data;
  sprite_set_palette(0, palette0[_this->disk_motor_anim+1]);
  sprite_upload_palette();

  _this->disk_motor_anim++;
  if (_this->disk_motor_anim >= 4)
    _this->disk_motor_anim = 0;
}

void IconBar_Control::setDiskMotor(bool on) {
  ISR_Guard g;
  if (on) {
    if(diskMotor_timer)
      return;
    diskMotorTimeout(NULL);

    diskMotor_timer = Timer_Repeating(100000,
           IconBar_Control::diskMotorTimeout,
           this);
  } else {
    if(!diskMotor_timer)
      return;
    Timer_Cancel(diskMotor_timer);
    diskMotor_timer = 0;

    sprite_set_palette(0, palette0[0]);
    sprite_upload_palette();
  }
}

void IconBar_Control::setJoystickAssigned(unsigned no, bool assigned) {
  if (no >= 2)
    return;
  map(1+no*2, 0) = MAP(tile_joytrno, assigned?2:1);
  updateDone();
}

void IconBar_Control::setMouseAssigned(bool assigned) {
  mousetr_uploader.setDest(FPGA_GRPH_SPRITES_RAM +
				   tile_mousetrno * 64);
  mousetr_uploader.setSrc(mousetr[assigned?1:0]);
  mousetr_uploader.setSize(64);
  mousetr_uploader.triggerUpload();
}

void IconBar_Control::setLpenAssigned(bool assigned) {
  lpentr_uploader.setDest(FPGA_GRPH_SPRITES_RAM +
          tile_lpentrno * 64);
  lpentr_uploader.setSrc(lpentr[assigned?1:0]);
  lpentr_uploader.setSize(64);
  lpentr_uploader.triggerUpload();
}

void IconBar_Control::setDiskHint(unsigned no, std::string const &hint) {
  if (no >= 4)
    return;

  diskHint[no] = hint;
}

void IconBar_Control::setJoystickHint(unsigned no, std::string const &hint) {
  if (no >= 2)
    return;

  joystickHint[no] = hint;
}

void IconBar_Control::setMouseHint(std::string const &hint) {
  mouseHint = hint;
}

void IconBar_Control::setLpenHint(std::string const &hint) {
  lpenHint = hint;
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
