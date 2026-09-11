#ifndef __NES_H__
#define __NES_H__


#include "../comm/idef.h"
#include "rom.h"
#include "cpu.h"
#include "apu.h"
#include "ppu.h"
#include "mapper.h"
#include "joypad.h"

#ifdef __cplusplus
extern "C"
{
#endif


#define NES_ERR_FILE_FORMAT           10001
#define NES_ERR_UNSUPPORT_MAPPER_ID   10002
#define NES_ERR_ILLEGAL_INSTRUCTION   10003


#define	FETCH_CYCLES	8


typedef   struct  _ines_settng_  {
	int       is_ntsc;
	double    base_clock;          // 
	double    cpu_clock;
	int       total_scan_lines;
	double    scan_line_cycles;
	double    h_draw_cycles;
	double    h_blank_cycles;
	double    scan_end_cycles;
	double    frame_cycles;
	double    frame_rate;

} ines_setting_t;


#define NES_STATUS_OFF          0
#define NES_STATUS_RUNNING      1
#define NES_STATUS_PAUSE        2
#define NES_STATUS_FRAME_STEP   3
//#define NES_STATUS_FRAME_PAUSE  4


// max 64KB
#define NES_MAX_SRAM_SIZE    0x10000
#define NES_MAX_SRAM_BANKS   8
// max 8 banks
#define NES_SRAM_8K_MASK     0x7


#define MAX_ERROR_STR_LEN     1024

#define INES_MAX_TITLE    256

struct _ines_host_;

typedef struct _ines_host_ ines_host_t;

struct _ines_host_
{
	ines_rom_t      rom;
	ines_mapper_t   mapper;
	ines_cpu_t      cpu;
	ines_ppu_t      ppu;
	ines_apu_t      apu;
	ines_joypad_t   joypad;
	ines_setting_t  setting;
	ines_byte_t     SRAM[NES_MAX_SRAM_SIZE]; // UP to 64K SRAM 
	ines_byte_t     SRAM_used[NES_MAX_SRAM_BANKS]; // each 8k if be used set to 1
	ines_byte_t     SRAM_write_flag;	 // 0 : no modified; 1: modified no save; 2: modified and saved
	ines_byte_t     frame_irq_enabled;
	ines_byte_t     frame_irq_disenabled;
	ines_byte_t     DMA_high;
	double          base_cycles;
	ines_int_t      cpu_cycles;
	//ines_char_t     title[INES_MAX_TITLE]; // ROM title
	ines_byte_t     status; // 0 - not install ROM. 1 - running. 2 - pause 3 - frame step
	ines_int64_t    frame_count;

	ines_word_t     prom_8k_num;
	ines_word_t     vrom_1k_num;
	ines_word_t     prom_8k_mask;
	ines_word_t     vrom_1k_mask;
};


#define cpu2host(p)    ((ines_host_t*)( (char*)(p) - offsetof(ines_host_t, cpu) ))
#define ppu2host(p)    ((ines_host_t*)( (char*)(p) - offsetof(ines_host_t, ppu) ))
#define apu2host(p)    ((ines_host_t*)( (char*)(p) - offsetof(ines_host_t, apu) ))
#define mapper2host(p) ((ines_host_t*)( (char*)(p) - offsetof(ines_host_t, mapper) ))



#define MAX_COLORS   64
#define SCREEN_WIDTH  256
#define SCREEN_HEIGHT  240
#define SCREEN_PIXELS   (SCREEN_WIDTH * SCREEN_HEIGHT)
#define PIXEL_BYTES   1
#define PIXEL_BITS    (PIXEL_BYTES * 8)
#define SCREEN_IMAGE_BYTES   (SCREEN_PIXELS * PIXEL_BYTES)

#define NES_AUDIO_SAMPLE_RATE      44100
#define NES_AUDIO_SAMPLE_BITS      8       
#define NES_AUDIO_CHANNEL          1       

#define NES_AUDIO_BYTES_PER_SECOND   (NES_AUDIO_SAMPLE_RATE * (NES_AUDIO_SAMPLE_BITS / 8) * NES_AUDIO_CHANNEL)


#define NES_BANK_WRITE_ABLE        0x01         // 可写的内存块
#define NES_BANK_WRITE_PROTECTED   0x02         // 写入保护的内存块
#define NES_BANK_READ_PROTECTED    0x04         // 禁止读取的内存块

#define NES_BANK_CAN_WRITE(b)     ( ( (b)&(NES_BANK_WRITE_ABLE|NES_BANK_WRITE_PROTECTED) )  == NES_BANK_WRITE_ABLE )
#define NES_BANK_CAN_READ(b)     ( ( (b)&(NES_BANK_READ_PROTECTED) )  == 0 )

//#define NES_SRAM_NO_WRITE      0
//#define NES_SRAM_IS_WRITE      1
//#define NES_SRAM_IS_SAVED      2




// 初始化
void ines_host_init(ines_host_t* p_host, int is_ntsc);
// 删除
void ines_host_free(ines_host_t* p_host);
// 加载ROM文件
ines_bool_t ines_host_load_rom(ines_host_t* p_host, ines_cstr_t strNesFileName, ines_cstr_t strRAMFileName);
// 软件复位
void ines_host_reset(ines_host_t* p_host);


void ines_host_init_setting(ines_host_t* p_host, ines_int_t is_ntsc);

ines_int_t ines_host_doframe(ines_host_t* p_host, ines_byte_t* p_screen);

// 总线读写（2000h以上空间，CPU内部RAM的读写不通过此接口
ines_byte_t  ines_host_read(ines_host_t* p_host, ines_word_t addr);
void ines_host_write(ines_host_t* p_host, ines_word_t addr, ines_byte_t  val);
void ines_host_write_sram_raw(ines_host_t* p_host, ines_word_t addr, ines_byte_t  val);


void ines_host_load_sram(ines_host_t* p_host, ines_cstr_t szRAMFileName);
void ines_host_save_sram(ines_host_t* p_host, ines_cstr_t szRAMFileName);

void ines_set_prom_bank_4(ines_host_t* p_host, ines_word_t b4, ines_word_t b5, ines_word_t b6, ines_word_t b7);
void ines_set_prom_bank_5(ines_host_t* p_host, ines_word_t b3, ines_word_t b4, ines_word_t b5, ines_word_t b6, ines_word_t b7);
void ines_set_prom_bank_n(ines_host_t* p_host, ines_word_t n, ines_word_t bn); // n = 3~7, bn=b3~b7
void ines_set_sram_bank_n(ines_host_t* p_host, ines_word_t n, ines_word_t bn);  // sram bank 

void ines_set_vrom_bank_8(ines_host_t* p_host, ines_word_t b0, ines_word_t b1, ines_word_t b2, ines_word_t b3, 
						  ines_word_t b4, ines_word_t b5, ines_word_t b6, ines_word_t b7);
void ines_set_vrom_bank_n(ines_host_t* p_host, ines_word_t n, ines_word_t bn); // n=0~8, bn = b0~b8
void ines_set_vram_bank_n(ines_host_t* p_host, ines_word_t n, ines_word_t bn);  // sram bank point
void ines_set_ciram_pattern_bank_n(ines_host_t* p_host, ines_word_t n, ines_word_t page); // 内部 NT RAM 当作 CHR 页(mapper 19)
void ines_set_nt_chr_bank_n(ines_host_t* p_host, ines_word_t n, ines_word_t bn); // nametable 窗口指向 CHR 页(n=0-3，mapper 19 ROM nametable)

ines_int_t ines_host_save_state(ines_host_t* p_host, FILE* fSave);
ines_int_t ines_host_load_state(ines_host_t* p_host, FILE* fSave);

ines_int_t  ines_save_state(ines_host_t* p_host, FILE* fSave);
ines_int_t  ines_load_state(ines_host_t* p_host, FILE* fSave);
ines_int_t ines_check_state_time(ines_host_t* p_host, FILE* fSave);

#ifdef __cplusplus
};
#endif

#endif
