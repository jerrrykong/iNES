#ifndef __CPU_H__
#define __CPU_H__


#include "../comm/idef.h"


#ifdef __cplusplus
extern "C"
{
#endif


/* P (flag) register bitmasks      NVRBDIZC    */
#define  N_FLAG         0x80
#define  V_FLAG         0x40
#define  R_FLAG         0x20  /* Reserved, always 1 */
#define  B_FLAG         0x10
#define  D_FLAG         0x08
#define  I_FLAG         0x04
#define  Z_FLAG         0x02
#define  C_FLAG         0x01

/* Vector addresses */
#define  NMI_VECTOR     0xFFFA
#define  RESET_VECTOR   0xFFFC
#define  IRQ_VECTOR     0xFFFE

/* cycle counts for interrupts */
#define  INT_CYCLES     7
#define  RESET_CYCLES   6

#define  NMI_MASK       0x01
#define  MMC_IRQ_MASK   0x02
#define  APU_IRQ_MASK   0x04

#define  IRQ_MASK       (MMC_IRQ_MASK|APU_IRQ_MASK)

/* Stack is located on 6502 page 1 */
#define  STACK_OFFSET   0x0100



struct _ines_cpu_;

typedef struct _ines_cpu_ ines_cpu_t;

struct _ines_cpu_
{
	ines_byte_t       reg_A;
	ines_byte_t       reg_X;
	ines_byte_t       reg_Y;
	ines_byte_t       reg_P;
	ines_byte_t       reg_SP;
	ines_word_t       reg_PC;
	ines_byte_t*      mem_bank[8];         // 内存分块映射，前3块无效
	ines_byte_t       bank_writeable[8];   // 内存分块是否可写
	ines_byte_t       RAM[0x800]; // 空间 0~2000H， 镜像3次
	ines_int64_t      total_cycles;
	ines_int_t        burn_cycles;
	ines_bool_t       jammed;
	ines_byte_t       INT_pending; 
	ines_int_t        apu_next_irq;
	ines_byte_t       dead_mem[0x2000]; // 初始
};


// 初始化
void ines_cpu_init(ines_cpu_t* p_cpu);
// 删除
void ines_cpu_free(ines_cpu_t* p_cpu);
// 软件复位
void ines_cpu_reset(ines_cpu_t* p_cpu);

ines_int_t  ines_cpu_exec(ines_cpu_t* p_cpu, ines_int_t cycles);

void ines_cpu_IRQ(ines_cpu_t* p_cpu, ines_byte_t irq_mask, ines_bool_t  is_set);

void ines_cpu_NMI(ines_cpu_t* p_cpu);

ines_int_t ines_cpu_save_state(ines_cpu_t* p_cpu, FILE* fSave);
ines_int_t ines_cpu_load_state(ines_cpu_t* p_cpu, FILE* fSave);

#ifdef __cplusplus
};
#endif

#endif
