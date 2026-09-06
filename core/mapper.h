#ifndef __MAPPER_H__
#define __MAPPER_H__


#include "../comm/idef.h"


#ifdef __cplusplus
extern "C"
{
#endif

struct _ines_mapper_;

typedef struct _ines_mapper_ ines_mapper_t;

struct _ines_mapper_
{
	ines_byte_t*  p_data;
	ines_size_t   data_len;
	ines_int_t    custom_sram;
	void (*fini) (ines_mapper_t*);
	void (*reset) (ines_mapper_t*);
	void (*hsync) (ines_mapper_t*, ines_int_t);
	void (*vsync) (ines_mapper_t*);
	ines_byte_t (*readlow) (ines_mapper_t*, ines_word_t);
	void (*writelow) (ines_mapper_t*, ines_word_t, ines_byte_t);
	void (*writehigh) (ines_mapper_t*, ines_word_t, ines_byte_t);
	ines_byte_t (*PPU_latch)(ines_mapper_t*, ines_word_t addr, ines_int_t mode); // for MMC5
	void (*PPU_latch_FDFE)(ines_mapper_t*, ines_word_t addr); // for MMC2
	int  (*savestate)(ines_mapper_t*, FILE*, ines_bool_t);
};

// 初始化
void ines_mapper_init(ines_mapper_t* p_mapper);
// 删除
void ines_mapper_free(ines_mapper_t* p_mapper);
// 软件复位
void ines_mapper_reset(ines_mapper_t* p_mapper);


ines_int_t ines_mapper_save_state(ines_mapper_t* p_mapper, FILE* fSave);
ines_int_t ines_mapper_load_state(ines_mapper_t* p_mapper, FILE* fSave);

#define ines_mapper_hsync(p_mapper, scanline)   \
	do { if( (p_mapper)->hsync ) { \
	(*(p_mapper)->hsync)((p_mapper), (scanline)); \
	} } while(0)

#define ines_mapper_vsync(p_mapper)   \
	do { if( (p_mapper)->vsync ) { \
	(*(p_mapper)->vsync) (p_mapper); \
	} } while(0)

#define ines_mapper_readlow(p_mapper,  addr)    \
	(  ( (p_mapper)->readlow) ? ( (p_mapper)->readlow( (p_mapper), addr )  ) : ( (addr)>>8 ) )

#define  ines_mapper_writelow(p_mapper, addr, val)     \
	do { if( (p_mapper)->writelow ) { \
	(* (p_mapper)->writelow) ( (p_mapper), (addr), (val) ); \
	} } while(0)

#define  ines_mapper_writehigh(p_mapper, addr, val)    \
	do { if( (p_mapper)->writehigh ) { \
	(*(p_mapper)->writehigh) ( (p_mapper), (addr), (val) ); \
	} } while(0)

#define ines_mapper_PPU_latch(p_mapper, addr, mode)   \
	(  ( (p_mapper)->PPU_latch) ? ( (p_mapper)->PPU_latch( (p_mapper), (addr), (mode) )  ) : ( 0 ) )

#define  ines_mapper_PPU_latch_FDFE(p_mapper, addr)     \
	do { if( (p_mapper)->PPU_latch_FDFE ) { \
	(* (p_mapper)->PPU_latch_FDFE) ( (p_mapper), (addr) ); \
	} } while(0)


ines_word_t  addr_mask(ines_word_t  num);

#define INIT_MAPPER_DATA_ST(mapper, st)     { \
	st* p = ines_alloc_st(st);            \
	memset(p, 0, sizeof(st));            \
	(mapper)->p_data = (ines_byte_t*)p;  \
	(mapper)->data_len = sizeof(st);  } 



#ifdef __cplusplus
};
#endif

#endif
