#ifndef __APU_H__
#define __APU_H__


#include "../comm/idef.h"


#ifdef __cplusplus
extern "C"
{
#endif

// with cpu cycles
#define APU_FRAME_PERIOD_PAL      8314
#define APU_FRAME_PERIOD_NTSC     7458
#define APU_PULSE_PHASE_RANGE        8
#define APU_TRIANGLE_PHASE_RANGE    16

#define APU_STATUS_PULSE1_ENABLED    0x01
#define APU_STATUS_PULSE2_ENABLED    0x02
#define APU_STATUS_TRIANGLE_ENABLED  0x04
#define APU_STATUS_NOISE_ENABLED     0x08
#define APU_STATUS_DMC_ENABLED       0x10
#define APU_STATUS_DMC_IRQ           0x40
#define APU_STATUS_FRAME_IRQ         0x80

#define MAX_SAMPLE_PER_FRAME         (44100 / 50 + 10)

struct _ines_apu_;
struct _ines_apu_pulse_;
struct _ines_apu_triangle_;
struct _ines_apu_noise_;
struct _ines_apu_dmc_;




typedef struct _ines_apu_           ines_apu_t;
typedef struct _ines_apu_pulse_     ines_apu_pulse_t;
typedef struct _ines_apu_triangle_  ines_apu_triangle_t;
typedef struct _ines_apu_noise_     ines_apu_noise_t;
typedef struct _ines_apu_dmc_       ines_apu_dmc_t;



struct _ines_apu_pulse_
{
	ines_byte_t   reg_ctrl[4];     // 控制寄存器s
	ines_byte_t   reg_written[4];  // 控制寄存器写入标记
	ines_byte_t   duty_counter;    // 序列计数器
	ines_byte_t   envelope;        // 
	ines_byte_t   env_delay;
	ines_byte_t   sweep_delay;
	ines_word_t   length_counter;  // 音长计数器
	ines_word_t    period_delay;    // 

	ines_sbyte_t   buffer[MAX_SAMPLE_PER_FRAME];
};

struct _ines_apu_triangle_
{
	ines_byte_t   reg_ctrl[4];     // 控制寄存器
	ines_byte_t   reg_written[4];  // 控制寄存器写入标记
	ines_byte_t   linear_counter;  // 线性计数器
	ines_byte_t   phase_counter;   // 三角阶梯计数器
	ines_word_t   length_counter;  // 音长计数器
	ines_word_t   period_delay;  // 周期计数器
	ines_sbyte_t  buffer[MAX_SAMPLE_PER_FRAME];
};

struct _ines_apu_noise_
{
	ines_byte_t   reg_ctrl[4];     // 控制寄存器
	ines_byte_t   reg_written[4];  // 控制寄存器写入标记
	ines_byte_t   envelope;        // 
	ines_byte_t   env_delay;
	ines_word_t   length_counter;  // 音长计数器
	ines_word_t   shift_register;  // 移位寄存器
	ines_word_t   period_delay;    // 周期计数器
	ines_sbyte_t  buffer[MAX_SAMPLE_PER_FRAME];
};

struct _ines_apu_dmc_
{
	ines_byte_t   reg_ctrl[4];     // 控制寄存器
	ines_byte_t   reg_written[4];  // 控制寄存器写入标记
	ines_word_t   length_counter;  // 音长计数器
	ines_word_t   period;          // 周期
	ines_int_t    period_delay;    // 周期计数器
	ines_byte_t   is_ntsc;
	ines_byte_t   mute;
	ines_byte_t   irq_enable;
	ines_byte_t   irq_flag;
	ines_int_t   next_irq;
	ines_word_t   address;
	ines_byte_t   bit;
	ines_byte_t   bit_buffer;
	ines_byte_t   bit_remain;
	ines_byte_t   bit_empty;
	ines_byte_t   dac;
	ines_sbyte_t  buffer[MAX_SAMPLE_PER_FRAME];
};


struct _ines_apu_
{
	ines_apu_pulse_t    channel_pulse1;
	ines_apu_pulse_t    channel_pulse2;
	ines_apu_triangle_t channel_triangle;
	ines_apu_noise_t    channel_noise;
	ines_apu_dmc_t      channel_dmc;


	ines_byte_t         reg_frame_mode;	
	ines_byte_t         reg_ctrl;
	ines_byte_t         irq_flag;
	ines_int_t          next_irq;
	ines_int_t          cur_frame;
	ines_int_t          frame_period;
	ines_int_t          frame_delay;
	ines_int64_t        frame_start_cpu_cycles;
	ines_int_t          last_cycles;
	

	ines_byte_t*  out_buffer;
	ines_dword_t  out_buffer_len;
	ines_dword_t  out_data_len;
	ines_int_t    out_volumn;

	float          prev_out;     // 用于平滑输出（低通滤波）
	float          low_fliter;   // 直流分量滤除 
};

#define dmc2apu(dmc)   ( (ines_apu_t*)( (char*) (dmc) - offsetof(ines_apu_t, channel_dmc ) ) )

// 初始化
void ines_apu_init(ines_apu_t* p_apu);
// 删除
void ines_apu_free(ines_apu_t* p_apu);
// 软件复位
void ines_apu_reset(ines_apu_t* p_apu);

ines_byte_t ines_apu_read(ines_apu_t* p_apu, ines_word_t addr);
void ines_apu_write(ines_apu_t* p_apu, ines_word_t addr, ines_byte_t  val);

void ines_apu_flush_run(ines_apu_t* p_apu);

void ines_apu_setoutbuffer(ines_apu_t* p_apu, ines_byte_t* p_buffer, ines_dword_t  buffer_len, ines_int_t  volumn);
ines_dword_t ines_apu_getoutlen(ines_apu_t* p_apu);

void ines_apu_start_frame(ines_apu_t* p_apu);
void ines_apu_render_frame(ines_apu_t* p_apu, double end_time);

ines_int_t ines_apu_save_state(ines_apu_t* p_apu, FILE* fSave);
ines_int_t ines_apu_load_state(ines_apu_t* p_apu, FILE* fSave);


#ifdef __cplusplus
};
#endif

#endif
