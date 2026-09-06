#ifndef __PPU_H__
#define __PPU_H__


#include "../comm/idef.h"


#ifdef __cplusplus
extern "C"
{
#endif

#define  MIRROR_SINGLE_SCREEN     0
#define  MIRROR_VERT              1
#define  MIRROR_HORZ              2
#define  MIRROR_FOUR_SCREEN       3
 
// PPU control register 1
#define PPU_NAME_MEM_MASK    0x03
#define PPU_RW_VERT          0x04
#define PPU_SPR_MEM_MASK     0x08
#define PPU_BG_MEM_MASK      0x10
#define PPU_BIG_SPR          0x20
#define PPU_VBLANK_ENABLED   0x80


// PPU control register 2 
#define PPU_SINGCOLOR        0x01
#define PPU_MASK_BGLEFT8     0x02
#define PPU_MASK_SPRLEFT8    0x04
#define PPU_ENABLE_BG        0x08
#define PPU_ENABLE_SPR       0x10


// PPU status register
#define PPU_STATUS_VRAM_WRITE_IGNORED   0x10
#define PPU_STATUS_SPR_NUM_OVER         0x20
#define PPU_STATUS_SPR_0_SCAN           0x40
#define PPU_STATUS_ENTER_VBLANK         0x80


#define BG_WHITE_MASK   1
#define SPR_WHITE_MASK  2

// up to 32KB
#define NES_MAX_VRAM_SIZE  0x8000
// UP to 32 banks
#define NES_VRAM_1K_MASK     0x1f
#define NES_MAX_VRAM_BANKS   0x20
#define NES_MAX_PTMEM_BANKS  0x8
#define NES_MAX_NTRAM_BANKS  0x4

#define NES_MAX_NTMEM_SIZE      0x1000
#define NES_MAX_SPMEM_SIZE      0x100
#define NES_MAX_PALMEM_SIZE     0x10
#define NES_MAX_VMEM_BANKS      12

struct _ines_ppu_;

typedef struct _ines_ppu_ ines_ppu_t;

struct _ines_ppu_
{
	ines_byte_t   pattern_table[NES_MAX_VRAM_SIZE]; // 1000 0000 0000 0000   some MMC used 32K VRAM for example mapper6
	ines_byte_t   pattern_table_used[NES_MAX_VRAM_BANKS]; // used 32K VRAM for example mapper6
	ines_byte_t   pattern_type[NES_MAX_PTMEM_BANKS];
	ines_byte_t   name_table[NES_MAX_NTMEM_SIZE];
	ines_byte_t   bg_pal[NES_MAX_PALMEM_SIZE];
	ines_byte_t   sp_pal[NES_MAX_PALMEM_SIZE];
	ines_byte_t   sp_RAM[NES_MAX_SPMEM_SIZE];
	ines_byte_t*  mem_bank[NES_MAX_VMEM_BANKS];
	ines_byte_t   reg_ctrl_1;   /* PPU控制寄存器1， $2000, RW,  */
	ines_byte_t   reg_ctrl_2;   /* PPU控制寄存器2， $2001, RW,  */
	ines_byte_t   reg_status;   /* PPU状态寄存器,   $2002, RO,  */
	ines_byte_t   reg_spr_addr; /* sprite memory access start addr $2003, WO,   */
	/* ines_byte_t   reg_spr_data; */ /* sprite memory read write data  $2004, RW,   */
	//ines_byte_t   reg_screen_offset_x; /* screen offset x  $2005, second write. WO */
	//ines_byte_t   reg_screen_offset_y; /* screen offset x  $2005, first write. WO */
	// ines_word_t   reg_VRAM_addr;  /* VRAM access start first high 6 bits, second low 8 bits, $2006, WO */
	/* ines_byte_t   reg_VRAM_data; */ /* VRAM read/write data $2007  WR */	
	ines_byte_t   toggle_2005_2006;
	ines_byte_t   read_2007_buffer;
	//ines_byte_t   used_vrom;
	ines_byte_t   in_vblank;
	ines_word_t   index_t;
	ines_word_t   index_v;
	ines_word_t   index_x;
	ines_int_t    current_line;
};

// 初始化
void ines_ppu_init(ines_ppu_t* p_ppu);
// 删除
void ines_ppu_free(ines_ppu_t* p_ppu);
// 软件复位
void ines_ppu_reset(ines_ppu_t* p_ppu);

void ines_ppu_set_mirror(ines_ppu_t* p_ppu, ines_byte_t  n0, ines_byte_t  n1, ines_byte_t  n2, ines_byte_t  n3);
void ines_ppu_set_mirror_type(ines_ppu_t* p_ppu, ines_byte_t mt);

ines_byte_t  ines_ppu_readlow(ines_ppu_t* p_ppu, ines_word_t addr);


void  ines_ppu_writelow(ines_ppu_t* p_ppu, ines_word_t addr, ines_byte_t val);


void ines_ppu_start_frame(ines_ppu_t* p_ppu);
void ines_ppu_end_frame(ines_ppu_t* p_ppu);

void ines_ppu_start_vblank(ines_ppu_t* p_ppu);

void ines_ppu_end_vblank(ines_ppu_t* p_ppu);

// case p_line is NULL indicated don't draw really
void ines_ppu_render_line(ines_ppu_t* p_ppu, ines_byte_t* p_line);


ines_int_t ines_ppu_save_state(ines_ppu_t* p_ppu, FILE* fSave);
ines_int_t ines_ppu_load_state(ines_ppu_t* p_ppu, FILE* fSave);

#ifdef __cplusplus
};
#endif

#endif
