#ifndef __INES_OSD_H__
#define __INES_OSD_H__


#include "../comm/idef.h"


#ifdef __cplusplus
extern "C"
{
#endif


// standard ascii 5x7 font, 纵向取模, 覆盖 0x20~0x7F 共 96 个字符
extern ines_byte_t  code_ascii_5x7[];

// 把文本直接绘制到 8bit 索引色的画面缓冲上(等价于 win32 前端的同名函数)。
// bits 为自底向上的 DIB 布局, (x, y) 以左上角为原点, clText 为字符颜色索引。
void DrawTextToBitmap(ines_byte_t* bits, ines_int_t iWidth, ines_int_t iHeight,
					  ines_cstr_t szText, ines_int_t x, ines_int_t y, ines_byte_t clText);


#ifdef __cplusplus
};
#endif


#endif
