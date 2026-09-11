#ifndef __INES_PALETTE_H__
#define __INES_PALETTE_H__


#include "../core/nes.h"


#ifdef __cplusplus
extern "C"
{
#endif


// 调色板条目, 内存布局与 win32 的 RGBQUAD 一致: 字节序为 B,G,R,reserved
typedef struct _ines_palette_rgb_
{
	ines_byte_t   b;
	ines_byte_t   g;
	ines_byte_t   r;
	ines_byte_t   reserved;
} ines_palette_rgb_t;


// 与 win32/iNES.c 中的 rgbQuard 完全一致, 保证两个平台颜色相同
extern ines_palette_rgb_t   iNES_palette[MAX_COLORS];

// 索引色查表: 每项为 32bit 的 BGRA(内存字节序为 B,G,R,0xFF),
// 供 CGBitmapContext(kCGBitmapByteOrder32Little | kCGImageAlphaNoneSkipFirst) 使用
extern ines_dword_t         iNES_palette_bgra[MAX_COLORS];


// 生成 iNES_palette_bgra, 进程内调用一次即可
void iNES_palette_init(void);

// 把 width*height 的 8bit 索引色图像转换为 BGRA。
// 注意: 输入是自底向上的 DIB 布局, 输出为自顶向下, 转换时按行翻转。
// pDst 需要有 width*height*4 字节空间。
void iNES_palette_to_bgra(const ines_byte_t* pSrc, ines_byte_t* pDst,
						  ines_int_t width, ines_int_t height);


#ifdef __cplusplus
};
#endif


#endif
