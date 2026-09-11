
#include "nes.h"
#include "apu.h"
#include "../comm/log.h"


#define CUR_CPU_CYCLES(p_apu)  (apu2host(p_apu)->cpu.total_cycles)

#define CPUCYCLE2SAMPLENUM(cycle)   ines_cpu_cycles_to_samples(cycle)

/* 音长寄存器写入值 -> 音长计数器初值转换表  */
static const ines_byte_t length_table [0x20] = {
	0x0A, 0xFE, 0x14, 0x02, 0x28, 0x04, 0x50, 0x06,
	0xA0, 0x08, 0x3C, 0x0A, 0x0E, 0x0C, 0x1A, 0x0E, 
	0x0C, 0x10, 0x18, 0x12, 0x30, 0x14, 0x60, 0x16,
	0xC0, 0x18, 0x48, 0x1A, 0x10, 0x1C, 0x20, 0x1E
};

/* 噪声发生器的周期 0~0xf对应的timer装入值 */
static const ines_int_t noise_period_table [16] = {
	0x004, 0x008, 0x010, 0x020, 0x040, 0x060, 0x080, 0x0A0,
	0x0CA, 0x0FE, 0x17C, 0x1FC, 0x2FA, 0x3F8, 0x7F2, 0xFE4
};


/* 方波通道的序列模式 */
static const ines_byte_t   pulse_duty[4] = {
	0x02, /*  0 1 0 0 0 0 0 0  duty=0 */
	0x06, /*  0 1 1 0 0 0 0 0  duty=1 */
	0x0E, /*  0 1 1 1 0 0 0 0  duty=2 */
	0xF9, /*  1 0 0 1 1 1 1 1  duty=3 */
}; 


static const ines_sbyte_t   triangle_volume[0x20] = {
	 1, 3, 5, 7, 9, 11, 13, 15, 15, 13, 11, 9, 7, 5, 3, 1,
	-1,-3,-5,-7,-9,-11,-13,-15,-15,-13,-11,-9,-7,-5,-3,-1,
};

/* DMC通道的计数器装入值表 */ 
static const ines_int_t  dmc_period_table[2][16] = {
	{0x1ac, 0x17c, 0x154, 0x140, 0x11e, 0x0fe, 0x0e2, 0x0d6, // NTSC
	0x0be, 0x0a0, 0x08e, 0x080, 0x06a, 0x054, 0x048, 0x036},

	{0x18e, 0x161, 0x13c, 0x129, 0x10a, 0x0ec, 0x0d2, 0x0c7, // PAL (totally untested)
	0x0b1, 0x095, 0x084, 0x077, 0x062, 0x04e, 0x043, 0x032}  // to do: verify PAL periods
};

/* Mix table */
// pulse_table [n] = 95.52 / (8128.0 / n + 100)
static  float mix_pulse_table[31];
// tnd_table [n] = 163.67 / (24329.0 / n + 100)
static  float mix_tnd_table[203];



static void pulse_reset(ines_apu_pulse_t* pulse);
static void triangle_reset(ines_apu_triangle_t* triangle);
static void noise_reset(ines_apu_noise_t* noise);
static void dmc_reset(ines_apu_dmc_t* dmc);


static void pulse_run(ines_apu_pulse_t* pulse, ines_int_t  from, ines_int_t  to);
static void triangle_run(ines_apu_triangle_t* triangle, ines_int_t  from, ines_int_t  to);
static void noise_run(ines_apu_noise_t* noise, ines_int_t  from, ines_int_t  to);
static void dmc_run(ines_apu_dmc_t* dmc, ines_int_t  from, ines_int_t  to);

static void pulse_clock_length(ines_apu_pulse_t* pulse);
static void triangle_clock_length(ines_apu_triangle_t* triangle);
static void noise_clock_length(ines_apu_noise_t* noise);

static void pulse_clock_sweep(ines_apu_pulse_t* pulse, ines_byte_t neg_adj);

static void triangle_clock_linear_counter(ines_apu_triangle_t* triangle);
static void pulse_clock_envelope(ines_apu_pulse_t* pulse);
static void noise_clock_envelope(ines_apu_noise_t* noise);

static void dmc_write(ines_apu_dmc_t* dmc, ines_word_t addr, ines_byte_t  val);
static void dmc_start(ines_apu_dmc_t* dmc);




static void run_until(ines_apu_t* p_apu, ines_int64_t  cur_cpu_cycle);
static void irq_changed(ines_apu_t* p_apu);



// 初始化
void ines_apu_init(ines_apu_t* p_apu)
{
	int n;
	memset(p_apu, 0, sizeof(*p_apu));
	// init mix tables
	mix_pulse_table[0] = mix_tnd_table[0] = 0.0f; 
	for(n = 1; n < 31; n++)
	{
		mix_pulse_table[n] = 95.52f / (8128.0f / n + 100);
	}
	for(n = 1; n < 203; n++)
	{
		mix_tnd_table[n] = 163.67f / (24329.0f / n + 100);
	}
}

// 删除
void ines_apu_free(ines_apu_t* p_apu)
{
	
}

// 软件复位
void ines_apu_reset(ines_apu_t* p_apu)
{
	p_apu->reg_frame_mode = 0;
	p_apu->frame_period = apu2host(p_apu)->setting.is_ntsc ?  APU_FRAME_PERIOD_NTSC : APU_FRAME_PERIOD_PAL; // for NTSC ? 
	p_apu->frame_delay = 1;
	p_apu->cur_frame = 0;

	p_apu->frame_start_cpu_cycles = 0;
	p_apu->last_cycles = 0;
	p_apu->reg_ctrl = 0;
	p_apu->irq_flag = 0;
	p_apu->next_irq = 0;

	p_apu->prev_out = 0.0f;
	p_apu->low_fliter = 0.0f;
	
	pulse_reset(&p_apu->channel_pulse1);
	pulse_reset(&p_apu->channel_pulse2);
	triangle_reset(&p_apu->channel_triangle);
	noise_reset(&p_apu->channel_noise);
	dmc_reset(&p_apu->channel_dmc);

	// 扩展音源引擎复位(已挂接时)
	p_apu->exp.cursor = 0;
	if(p_apu->exp.p_chip && p_apu->exp.reset)
		p_apu->exp.reset(&p_apu->exp);

	ines_apu_write(p_apu, 0x4015, 0);
	ines_apu_write(p_apu, 0x4017, 0);
}

ines_byte_t ines_apu_read(ines_apu_t* p_apu, ines_word_t addr)
{
	ines_int64_t   cur_cycles;
	ines_byte_t    data;

	cur_cycles = apu2host(p_apu)->cpu.total_cycles;

	run_until(p_apu, cur_cycles - 1);

	// read status reg
	if(addr == 0x4015)
	{
		data = 0;
		if(p_apu->irq_flag)                             data |= APU_STATUS_FRAME_IRQ;
		if(p_apu->channel_dmc.irq_flag)                 data |= APU_STATUS_DMC_IRQ;
		if(p_apu->channel_pulse1.length_counter != 0)   data |= APU_STATUS_PULSE1_ENABLED;
		if(p_apu->channel_pulse2.length_counter != 0)   data |= APU_STATUS_PULSE2_ENABLED;
		if(p_apu->channel_triangle.length_counter != 0) data |= APU_STATUS_TRIANGLE_ENABLED;
		if(p_apu->channel_noise.length_counter != 0)    data |= APU_STATUS_NOISE_ENABLED;
		if(p_apu->channel_dmc.length_counter != 0)      data |= APU_STATUS_DMC_ENABLED;

		if(p_apu->irq_flag)
			p_apu->irq_flag = 0;
		irq_changed(p_apu);
	}
	else 
	{
		// case heavy bus load
		data = 0x40;
	}
	// INES_LOG(LOG_DBG, MOD_APU, ISTR("APU_READ($%04X)=#$%02X, CPU_CYCLE=%I64d\n"), addr, data, cur_cycles);

	run_until(p_apu, cur_cycles);
	
	return data;
}

void ines_apu_write(ines_apu_t* p_apu, ines_word_t addr, ines_byte_t  val)
{
	ines_int64_t   cur_cycles;
	ines_int_t     reg;

	cur_cycles = CUR_CPU_CYCLES(p_apu);


	run_until(p_apu, cur_cycles);

	INES_LOG(LOG_DBG, MOD_APU, ISTR("APU_WRITE($%04X)=#$%02X, CPU_CYCLE=%")ISTR(PRI64)ISTR("d\n"), addr, val, cur_cycles);

	switch(addr)
	{
	case 0x4000:
	case 0x4001:
	case 0x4002:
	case 0x4003:
		reg = addr & 0x3;
		p_apu->channel_pulse1.reg_ctrl[reg] = val;
		p_apu->channel_pulse1.reg_written[reg] = 1;
		if(reg == 3)
		{
			if(p_apu->reg_ctrl & APU_STATUS_PULSE1_ENABLED)
			{
				p_apu->channel_pulse1.length_counter = length_table[(val >> 3) & 0x1f];
			}
			p_apu->channel_pulse1.duty_counter = 0;
		}
		break;
	case 0x4004:
	case 0x4005:
	case 0x4006:
	case 0x4007:
		reg = addr & 0x3;
		p_apu->channel_pulse2.reg_ctrl[reg] = val;
		p_apu->channel_pulse2.reg_written[reg] = 1;
		if(reg == 3)
		{
			if(p_apu->reg_ctrl & APU_STATUS_PULSE2_ENABLED)
			{
				p_apu->channel_pulse2.length_counter = length_table[(val >> 3) & 0x1f];
			}
			p_apu->channel_pulse2.duty_counter = 0;
		}
		break;
	case 0x4008:
	case 0x4009:
	case 0x400a:
	case 0x400b:
		reg = addr & 0x3;
		p_apu->channel_triangle.reg_ctrl[reg] = val;
		p_apu->channel_triangle.reg_written[reg] = 1;
		if(reg == 3)
		{
			if(p_apu->reg_ctrl & APU_STATUS_TRIANGLE_ENABLED)
			{
				p_apu->channel_triangle.length_counter = length_table[(val >> 3) & 0x1f];
			}
		}
		break;
	case 0x400c:
	case 0x400d:
	case 0x400e:
	case 0x400f:
		reg = addr & 0x3;
		p_apu->channel_noise.reg_ctrl[reg] = val;
		p_apu->channel_noise.reg_written[reg] = 1;
		if(reg == 3)
		{
			if(p_apu->reg_ctrl & APU_STATUS_NOISE_ENABLED)
			{
				p_apu->channel_noise.length_counter = length_table[(val >> 3) & 0x1f];
			}
		}
		break;
	case 0x4010:
	case 0x4011:
	case 0x4012:
	case 0x4013:
		reg = addr & 0x3;
		p_apu->channel_dmc.reg_ctrl[reg] = val;
		p_apu->channel_dmc.reg_written[reg] = 1;
		dmc_write(&p_apu->channel_dmc, reg, val);
		if(0 == reg)
			irq_changed(p_apu);
		break;
	case 0x4015:
		/////////////////////////////////////////////////////////////////////////////////////////
		if (0 == (val & APU_STATUS_PULSE1_ENABLED))   p_apu->channel_pulse1.length_counter   = 0;
		if (0 == (val & APU_STATUS_PULSE2_ENABLED))   p_apu->channel_pulse2.length_counter   = 0;
		if (0 == (val & APU_STATUS_TRIANGLE_ENABLED)) p_apu->channel_triangle.length_counter = 0;
		if (0 == (val & APU_STATUS_NOISE_ENABLED))    p_apu->channel_noise.length_counter    = 0;
		if (0 == (val & APU_STATUS_DMC_ENABLED))      p_apu->channel_dmc.length_counter      = 0;
		/////////////////////////////////////////////////////////////////////////////////////////
		{
			// ines_int_t  recalc_irq = 0;
			ines_byte_t old_ctrl = p_apu->reg_ctrl;
			p_apu->reg_ctrl = val;

			p_apu->channel_dmc.irq_flag = 0;


			if(!(val&0x10))
			{
				// disable dmc IRQ
				p_apu->channel_dmc.next_irq = 0;
				//recalc_irq = 1;
			}
			else if(!(old_ctrl&0x10))
			{
				// enable dmc
				dmc_start(&p_apu->channel_dmc);
			}

			//if(recalc_irq)
			{
				irq_changed(p_apu);
			}
		}
		break;
	case 0x4017:
		p_apu->reg_frame_mode = val;
		p_apu->frame_delay &= 1;
		p_apu->cur_frame = 0;
		p_apu->next_irq = 0;

		if(val & 0x40)
		{
			// disable frame irq
			p_apu->irq_flag = 0;
		}

		// mode 1?
		if(0 == (val & 0x80))
		{
			p_apu->cur_frame = 1;
			p_apu->frame_delay += p_apu->frame_period;
			// irq  enabled ?
			if(0 == (val & 0x40))
			{
				p_apu->next_irq = p_apu->last_cycles + p_apu->frame_delay + p_apu->frame_period * 3;
			}
		}

		irq_changed(p_apu);
		break;
	}
}


void ines_apu_flush_run(ines_apu_t* p_apu)
{
	ines_int64_t   cur_cycles;

	cur_cycles = CUR_CPU_CYCLES(p_apu);

	INES_LOG(LOG_DBG, MOD_APU, ISTR("APU_FLUSH_RUN, CPU_CYCLE=%")ISTR(PRI64)ISTR("d\n"), cur_cycles);

	run_until(p_apu, cur_cycles);
}

void ines_apu_exp_attach(ines_apu_t* p_apu, void* p_chip, ines_int_t channels, float gain,
                         void (*run)(ines_apu_exp_t*, ines_int_t),
                         void (*reset)(ines_apu_exp_t*))
{
	if(p_apu->exp.p_chip && p_apu->exp.p_chip != p_chip)
		ines_apu_exp_detach(p_apu, p_apu->exp.p_chip);

	memset(&p_apu->exp, 0, sizeof(p_apu->exp));
	p_apu->exp.p_chip    = p_chip;
	p_apu->exp.channels  = channels;
	p_apu->exp.gain      = gain;
	p_apu->exp.run       = run;
	p_apu->exp.reset     = reset;

	if(p_apu->exp.reset)
		p_apu->exp.reset(&p_apu->exp);
}

void ines_apu_exp_detach(ines_apu_t* p_apu, void* p_chip)
{
	if(p_apu->exp.p_chip == p_chip)
		memset(&p_apu->exp, 0, sizeof(p_apu->exp));
}

void ines_apu_setoutbuffer(ines_apu_t* p_apu, ines_byte_t* p_buffer, ines_dword_t  buffer_len, ines_int_t volumn)
{
	p_apu->out_buffer = p_buffer;
	p_apu->out_buffer_len = buffer_len;
	p_apu->out_data_len = 0;
	p_apu->out_volumn = volumn;
}

ines_dword_t ines_apu_getoutlen(ines_apu_t* p_apu)
{
	return p_apu->out_data_len;
}

void ines_apu_start_frame(ines_apu_t* p_apu)
{
	ines_int64_t   old = p_apu->frame_start_cpu_cycles;
	p_apu->frame_start_cpu_cycles = CUR_CPU_CYCLES(p_apu);
	p_apu->last_cycles = 0;

	// 扩展音源: 帧游标复位; 清瞬态缓冲(读档后引擎余量可能跨帧残留)
	p_apu->exp.cursor = 0;
	if(p_apu->exp.p_chip)
		memset(p_apu->exp.buffer, 0, sizeof(p_apu->exp.buffer));
	if(p_apu->next_irq > 0)
		p_apu->next_irq -= (ines_int_t)(p_apu->frame_start_cpu_cycles - old);
	if(p_apu->channel_dmc.next_irq > 0)
		p_apu->channel_dmc.next_irq -= (ines_int_t)(p_apu->frame_start_cpu_cycles - old);
}


void ines_apu_render_frame(ines_apu_t* p_apu, double _unused)
{
	ines_int_t i;
	ines_int_t a;
	ines_int_t c;
	ines_int_t ch;
	ines_byte_t* outbuf;
	ines_int64_t  cur_cycles;
	float volumn;
	ines_int_t  pulse_vol, tnd_vol;
	float fout;
	float fprev; 
	ines_int_t  iout;
	float flown = 2.0f;
	float low_fliter;
	int   dc_n = 2200;  // up 20 Hz (44100 / 20)

	cur_cycles = CUR_CPU_CYCLES(p_apu);

	// 29830 CYCLES ?
	run_until(p_apu, cur_cycles);

	//c = NES_AUDIO_SAMPLE_RATE / 60;
	c = CPUCYCLE2SAMPLENUM(p_apu->last_cycles);

	if(c > (int)p_apu->out_buffer_len)
		c = p_apu->out_buffer_len;

	outbuf = p_apu->out_buffer;
	volumn = (powf(10.0f, (p_apu->out_volumn / 100.0f)) - 1) / 9.0f;

	fprev = p_apu->prev_out;
	low_fliter = p_apu->low_fliter;

	// scale = (float) (p_apu->last_cycles >> 1) / c;
	for(i = 0; i <  c; i++)
	{
		// p_apu->last_cycles => c;
		a = i; // (ines_int_t)(i * scale + 0.5f);
		pulse_vol = (ines_int_t) p_apu->channel_pulse1.buffer[a] +  
					(ines_int_t) p_apu->channel_pulse2.buffer[a]  ;
		tnd_vol   = (ines_int_t) p_apu->channel_triangle.buffer[a] * 3 + 
					(ines_int_t) p_apu->channel_noise.buffer[a] * 2 + 
					(ines_int_t) p_apu->channel_dmc.buffer[a] ;
		fout = (pulse_vol >= 0 ? mix_pulse_table[ pulse_vol ] : -mix_pulse_table[ -pulse_vol ] ) + 
			   (tnd_vol >= 0 ? mix_tnd_table[ tnd_vol ] : -mix_tnd_table[ -tnd_vol ]);

		// 扩展音源: Σ(每声道幅值)/32767 * gain, 线性和
		if(p_apu->exp.p_chip)
		{
			float ext_fout = 0.0f;
			for(ch = 0; ch < p_apu->exp.channels; ch++)
				ext_fout += (float)(p_apu->exp.buffer[ch][i]);
			fout += (ext_fout / 32767.0f) * p_apu->exp.gain;
		}
		//  must -1.0 <= fout <= 1.0f

		// low pass

		low_fliter += ( fout - low_fliter) / dc_n;
		fout -= low_fliter;

		fout = (fout + fprev * (flown - 1.0f)) / flown;

		fprev = fout;

		iout = (ines_int_t)((fout * volumn + 1.0f) / 2.0f * 0xff);
		if(iout < 0 ) iout = 0;
		else if(iout > 0xff) iout = 0xff;

		INES_LOG(LOG_TRA, MOD_APU, ISTR("APU_OUTPUT: (%p) Fill sample [%d]->[%d] = %d\n"), p_apu, i, i+1, iout);

		outbuf[i] = (ines_byte_t)iout ; // (ines_byte_t)(255.0f * fout );
	}
	p_apu->out_data_len = c;

	p_apu->low_fliter = low_fliter;
	p_apu->prev_out = fprev;
}

static void run_until(ines_apu_t* p_apu, ines_int64_t  cur_cpu_cycle)
{
	ines_int_t  end_cycles;
	ines_int_t  cycles;
	//ines_int_t  last_cycle;

	end_cycles = (ines_int_t)(cur_cpu_cycle - p_apu->frame_start_cpu_cycles);
	//last_cycle = p_apu->last_cycles;

	// not need run
	if(end_cycles <= p_apu->last_cycles)
		return;

	INES_LOG(LOG_TRA, MOD_APU, ISTR("APU_RUN_UNTIL: From=%d, to=%d\n"), p_apu->last_cycles, end_cycles);
	

	// limit to 20000 APU Cycles
	//if(end_cycles > (MAX_APU_CYCLE_PER_FRAME << 1))
	//{
	//	end_cycles = (MAX_APU_CYCLE_PER_FRAME << 1);
	//}

	while(1)
	{
		cycles = p_apu->last_cycles + p_apu->frame_delay;
		if ( cycles > end_cycles)
		{
			cycles = end_cycles;
			p_apu->frame_delay -= cycles - p_apu->last_cycles;
		}
		else
		{
			p_apu->frame_delay = 0;
		}

		INES_LOG(LOG_TRA, MOD_APU, ISTR("APU_RUN_UNTIL: FRAME=%d, From=%d, to=%d\n"), p_apu->cur_frame, p_apu->last_cycles, cycles);


		pulse_run(&p_apu->channel_pulse1, p_apu->last_cycles, cycles);
		pulse_run(&p_apu->channel_pulse2, p_apu->last_cycles, cycles);
		triangle_run(&p_apu->channel_triangle, p_apu->last_cycles, cycles);
		noise_run(&p_apu->channel_noise, p_apu->last_cycles, cycles);
		dmc_run(&p_apu->channel_dmc, p_apu->last_cycles, cycles);

		// 扩展音源芯片随 2A03 一起惰性推进(游标只前进; mapper 写音频寄存器前的
		// ines_apu_flush_run 会先进到这里, 保证寄存器变更落在准确的 CPU 周期)
		if(p_apu->exp.p_chip && p_apu->exp.run && p_apu->exp.cursor < cycles)
		{
			p_apu->exp.run(&p_apu->exp, cycles);
			p_apu->exp.cursor = cycles;
		}

		p_apu->last_cycles = cycles;

		if(cycles == end_cycles)
			break;

		p_apu->frame_delay = p_apu->frame_period;

		// next apu frame
		switch(p_apu->cur_frame++)
		{
		case 0:
			if(0 == (p_apu->reg_ctrl & 0xc0))
			{
				// calc next irq time and set this irq flag
				p_apu->next_irq = cycles + p_apu->frame_period * 4 + 1;
				p_apu->irq_flag = 1;
				INES_LOG(LOG_DBG, MOD_APU, ISTR("APU set IRQ, calc next at [%016")ISTR(PRI64)ISTR("x] cycles.\n"), p_apu->frame_start_cpu_cycles + p_apu->next_irq);
				irq_changed(p_apu);
			}
			// no break  for execute clock sweep and length on frame 0 and 2
		case 2:
			pulse_clock_length(&p_apu->channel_pulse1);
			pulse_clock_length(&p_apu->channel_pulse2);
			triangle_clock_length(&p_apu->channel_triangle);
			noise_clock_length(&p_apu->channel_noise);

			pulse_clock_sweep(&p_apu->channel_pulse1, 1);
			pulse_clock_sweep(&p_apu->channel_pulse2, 0);

			break;
		case 1:
			// frame 1 is slight short 
			p_apu->frame_delay -= 2;
			break;
		case 3:
			p_apu->cur_frame = 0;

			// frame 3 is twice long in mode 1, for simulated 5 steps frame.
			if(p_apu->reg_ctrl & 0x80)
			{
				p_apu->frame_delay += p_apu->frame_period - 6;
			}
			break;
		}

		// clock envelopes and linear counter every frame
		triangle_clock_linear_counter(&p_apu->channel_triangle);
		pulse_clock_envelope(&p_apu->channel_pulse1);
		pulse_clock_envelope(&p_apu->channel_pulse2);
		noise_clock_envelope(&p_apu->channel_noise);

	}
}


// set IRQ  
static void irq_changed(ines_apu_t* p_apu)
{
	ines_cpu_t* p_cpu;
	ines_int_t  new_irq = p_apu->next_irq;
	if(p_apu->channel_dmc.next_irq > new_irq)
		new_irq = p_apu->channel_dmc.next_irq;
	p_cpu = &(apu2host(p_apu)->cpu);
	if(new_irq > 0)
	{
		p_cpu->apu_next_irq = (ines_int_t)(p_apu->frame_start_cpu_cycles + new_irq - p_cpu->total_cycles);
	}
	else
	{
		p_cpu->apu_next_irq = 0;
	}
	if(/*p_apu->irq_flag || */p_apu->channel_dmc.irq_flag)
	{
		ines_cpu_IRQ(p_cpu, APU_IRQ_MASK, ines_true);
	}
	else
	{
		ines_cpu_IRQ(p_cpu, APU_IRQ_MASK, ines_false);
	}
}

/***********************************************************************/

/*
 channel implements 
*/


///////////////////////////////////////////////////////////////////////////////////
// Pulse Channel
///////////////////////////////////////////////////////////////////////////////////

static void pulse_reset(ines_apu_pulse_t* pulse)
{
	pulse->length_counter = 0;
	pulse->reg_ctrl[0] = 0x10;
	pulse->reg_ctrl[1] = 0;
	pulse->reg_ctrl[2] = 0;
	pulse->reg_ctrl[3] = 0;
	pulse->reg_written[0] = 1;
	pulse->reg_written[1] = 1;
	pulse->reg_written[2] = 1;
	pulse->reg_written[3] = 1;
	pulse->duty_counter = 0;
	pulse->envelope = 0;
	pulse->env_delay = 0;
	pulse->sweep_delay = 0;
	pulse->period_delay = 0;

}

static void pulse_run(ines_apu_pulse_t* pulse, ines_int_t  from, ines_int_t  to)
{

	ines_byte_t  volume;
	ines_byte_t  duty;
	ines_int_t   period;
	ines_int_t   time_period;
	ines_int_t   offset;
	ines_sbyte_t amp;
	ines_int_t   next;
	ines_int_t   count;
	ines_int_t   out_from, out_to;
	ines_int_t   delay;

	// 音量值 取决于是否为固定音量（Reg 0 Bit 4）
	// 如果Reg 0 Bit 4 设置为1，表示固定音量，则 Reg的0-3 位表示音量值（0-15），否则使用音量衰减单元的输出值，（0-15）
	volume = pulse->length_counter == 0 ? 0 : pulse->reg_ctrl[0] & 0x10 ? pulse->reg_ctrl[0] & 0x0f : pulse->envelope;
	// 频率计数器的初值由Reg 2，Reg 3 的低3位定。共11位。 计数器的时钟直接为APU的周期时钟。为CPU频率的一半
	period = ((ines_int_t)(pulse->reg_ctrl[3]&0x07)<<8) | (pulse->reg_ctrl[2]);

	// two cpu cycle tick one apu cycle
	time_period = (period + 1) << 1;

	INES_LOG(LOG_TRA, MOD_APU, ISTR("APU_PULSE_RUN: (%p) length=%d, volume=%d, period=%d, delay=%d\n"), pulse, pulse->length_counter, volume, period, pulse->period_delay);
	//INES_LOG(LOG_TRA, MOD_APU, ISTR("APU_PULSE_RUN: (%p) period_counter=%d\n"), pulse, pulse->period_counter);

	offset = 0;

	if( (pulse->reg_ctrl[1] & 0x80) == 0)
	{
		offset = period >> (pulse->reg_ctrl[1] & 0x07);
	}

	delay = pulse->period_delay;

	// two cpu cycle tick one apu cycle

	if(volume == 0 || period < 8 || period + offset >= 0x800)
	{
		// 通道静音时：直接填充0个相应的周期
		if(from < to)
		{
			out_from = CPUCYCLE2SAMPLENUM(from);
			out_to = CPUCYCLE2SAMPLENUM(to);
			from += delay;
			if(out_from < out_to)
				INES_LOG(LOG_TRA, MOD_APU, ISTR("APU_PULSE_RUN: (%p) Fill sample [%d]->[%d] = %d\n"), pulse, out_from, out_to, 0);
			while(out_from < out_to)
			{
				assert(out_from >= 0 && out_from < MAX_SAMPLE_PER_FRAME);
				pulse->buffer[out_from] = 0;
				out_from++;
			}
			count = (to - from + time_period) / time_period;
			pulse->duty_counter = (pulse->duty_counter + count) & 0x07;
			from += count * time_period;
			delay = from - to;
		}
	}
	else
	{
		// 方波序列，有4种 ， Reg 0 的最高两位决定。分别产生不同点空比的方波输出 
		duty = pulse_duty[(pulse->reg_ctrl[0] >> 6) & 0x03];

		// 输出音量由序列样式决定，0 输出负音量，1 输出正的音量。
		// 序列计数器， 由频率定时器驱动。0~7 共8个值。决定了当前为序列样式第几Bit
		amp = ((duty >> pulse->duty_counter) & 0x01) ? (ines_sbyte_t)volume : -(ines_sbyte_t)volume;


		out_from = CPUCYCLE2SAMPLENUM(from);

		while(from < to)
		{
			from += delay;
			if(from>to)
				next = to;
			else
				next = from;
			out_to = CPUCYCLE2SAMPLENUM(next);

			if(out_from < out_to)
				INES_LOG(LOG_TRA, MOD_APU, ISTR("APU_PULSE_RUN: (%p) Fill sample [%d]->[%d] = %d\n"), pulse, out_from, out_to, amp);

			while(out_from < out_to)
			{
				assert(out_from >= 0 && out_from < MAX_SAMPLE_PER_FRAME);
				pulse->buffer[out_from] = amp;
				out_from++;
			}

			out_from = out_to;

			if(from <= to)
			{
				delay = time_period;
				// 序列下一个Bit。
				pulse->duty_counter = (pulse->duty_counter + 1) & 0x07;
				amp = ((duty >> pulse->duty_counter) & 0x01) ? (ines_sbyte_t)volume : -(ines_sbyte_t)volume;
			}
			else
			{
				delay = from - to;
			}
		}
	}
	pulse->period_delay = delay; 
}

// 音长计数器时钟
static void pulse_clock_length(ines_apu_pulse_t* pulse)
{
	if ( pulse->length_counter > 0 && !(pulse->reg_ctrl[0] & 0x20) )
		pulse->length_counter--;
	
}

// 扫频计数器时钟
static void pulse_clock_sweep(ines_apu_pulse_t* pulse, ines_byte_t neg_adj)
{
	ines_int_t     period;
	ines_int_t     offset;
	ines_int_t     shift;
	ines_byte_t    sweep;

	if(pulse->sweep_delay == 0)
	{
		// 需要重新装入扫频计数器
		pulse->reg_written[1] = 1;
		sweep = pulse->reg_ctrl[1];
		// 定时器周期
		period = ((ines_int_t)(pulse->reg_ctrl[3]&0x07)<<8) | (pulse->reg_ctrl[2]);
		// 扫频右移量
		shift = sweep & 0x7; 

		if( (sweep & 0x80) && period > 8 && shift > 0)
		{
			// 扫频被激活
			offset = period >> shift;

			if(sweep & 0x08)
			{
				// 减少
				offset = -offset - neg_adj;
			}
			if(period + offset < 0x800)
			{
				period += offset;
				// modify reg for period
				pulse->reg_ctrl[2] = period & 0xff;
				pulse->reg_ctrl[3] = (pulse->reg_ctrl[3] & 0xf8) | ((period >> 8) & 0x07);
			}
		}
	}
	else
	{
		pulse->sweep_delay--;
	}


	if(pulse->reg_written[1])
	{
		pulse->reg_written[1] = 0;
		pulse->sweep_delay = ((pulse->reg_ctrl[1] >> 4) & 0x07);
	}
}

// 音量衰减计数器时钟
static void pulse_clock_envelope(ines_apu_pulse_t* pulse)
{
	//ines_byte_t  period;
	// period = pulse->reg_ctrl[0] & 0x0f;

	if(pulse->reg_written[3])
	{
		// 写入音长寄存器，触发音量衰减速度计数器重新装入
		pulse->reg_written[3] = 0;
		pulse->env_delay = pulse->reg_ctrl[0] & 0x0f;
		pulse->envelope = 0xf;
	}
	else if(pulse->env_delay > 0) 
	{
		// 音量衰减速度计数器向下计数
		pulse->env_delay--;
	}
	else
	{
		// 重新装入音量衰减速度计数器
		pulse->env_delay = pulse->reg_ctrl[0] & 0x0f;
		// 衰减循环使能为1（寄存器0的BIT5）或者衰减音量没到0.则音量衰减一。
		// 如果衰减循环使能为0，则音量衰减到0后会一直停止在0，这样声道则静音。
		if(pulse->envelope > 0 || (pulse->reg_ctrl[0] & 0x20) ) 
		{
			pulse->envelope = (pulse->envelope - 1) & 0xf;
		}
	}
}

///////////////////////////////////////////////////////////////////////////////////
// Triangle
///////////////////////////////////////////////////////////////////////////////////

static void triangle_reset(ines_apu_triangle_t* triangle)
{
	triangle->length_counter = 0;
	triangle->reg_ctrl[0] = 0x10;
	triangle->reg_ctrl[1] = 0;
	triangle->reg_ctrl[2] = 0;
	triangle->reg_ctrl[3] = 0;
	triangle->reg_written[0] = 1;
	triangle->reg_written[1] = 1;
	triangle->reg_written[2] = 1;
	triangle->reg_written[3] = 1;
	triangle->linear_counter = 0;
	triangle->phase_counter = 0;
	triangle->period_delay = 0;
}

static void triangle_run(ines_apu_triangle_t* triangle, ines_int_t  from, ines_int_t  to)
{
	ines_int_t   period;
	ines_int_t   time_period;
	ines_int_t   out_from, out_to;
	ines_int_t   next;
	ines_int_t   delay;
	//ines_int_t   count;
	ines_sbyte_t amp;

	period = ((ines_int_t)(triangle->reg_ctrl[3]&0x07)<<8) | (triangle->reg_ctrl[2]);

	time_period = period + 1;

	INES_LOG(LOG_TRA, MOD_APU, ISTR("APU_TRIANGLE_RUN: (%p) length=%d, linear_counter=%d, period=%d\n"), triangle, triangle->length_counter, triangle->linear_counter, period);

	if(triangle->length_counter == 0 || triangle->linear_counter == 0 || period < 3)
	{
		// 通道静音时：直接填充0个相应的周期
		out_from = CPUCYCLE2SAMPLENUM(from);
		out_to = CPUCYCLE2SAMPLENUM(to);
		if(out_from < out_to)
			INES_LOG(LOG_TRA, MOD_APU, ISTR("APU_TRIANGLE_RUN: (%p) Fill sample [%d]->[%d] = %d\n"), triangle, out_from, out_to, 0);
		while(out_from < out_to)
		{
			assert(out_from >= 0 && out_from < MAX_SAMPLE_PER_FRAME);
			triangle->buffer[out_from] = 0;
			out_from++;
		}
		triangle->period_delay = 0;
	}
	else
	{
		amp = triangle_volume[triangle->phase_counter];

		delay = triangle->period_delay;

		out_from = CPUCYCLE2SAMPLENUM(from);
		while(from < to)
		{
			from += delay;
			if(from > to)
				next = to;
			else
				next = from;
			out_to = CPUCYCLE2SAMPLENUM(next);
			if(out_from < out_to)
				INES_LOG(LOG_TRA, MOD_APU, ISTR("APU_TRIANGLE_RUN: (%p) Fill sample [%d]->[%d] = %d\n"), triangle, out_from, out_to, amp);
			while(out_from < out_to)
			{
				assert(out_from >= 0 && out_from < MAX_SAMPLE_PER_FRAME);
				triangle->buffer[out_from] = amp;
				out_from++;
			}
			out_from = out_to;

			if(from <= to)
			{
				delay = time_period;
				triangle->phase_counter = (triangle->phase_counter + 1) & 0x1f;
				amp = triangle_volume[triangle->phase_counter];
			}
			else
			{
				delay = from - to;
			}
		}
		triangle->period_delay = delay;
	}
}

static void triangle_clock_length(ines_apu_triangle_t* triangle)
{
	if ( triangle->length_counter > 0 && !(triangle->reg_ctrl[0] & 0x80) )
		triangle->length_counter--;
}

static void triangle_clock_linear_counter(ines_apu_triangle_t* triangle)
{
	if(triangle->reg_written[3])
	{
		triangle->linear_counter = triangle->reg_ctrl[0] & 0x7f;
	}
	else if(triangle->linear_counter)
	{
		--triangle->linear_counter;
	}

	if( 0 == (triangle->reg_ctrl[0] & 0x80))
	{
		triangle->reg_written[3] = 0;
	}
}

///////////////////////////////////////////////////////////////////////////////////
// Noise
///////////////////////////////////////////////////////////////////////////////////


static void noise_reset(ines_apu_noise_t* noise)
{
	noise->length_counter = 0;
	noise->reg_ctrl[0] = 0x10;
	noise->reg_ctrl[1] = 0;
	noise->reg_ctrl[2] = 0;
	noise->reg_ctrl[3] = 0;
	noise->reg_written[0] = 1;
	noise->reg_written[1] = 1;
	noise->reg_written[2] = 1;
	noise->reg_written[3] = 1;

	noise->shift_register = 1;  // 初始装入1
	noise->period_delay = 0;
	noise->envelope = 0;
	noise->env_delay = 0;

}

static void noise_run(ines_apu_noise_t* noise, ines_int_t  from, ines_int_t  to)
{
	ines_byte_t    volume;
	ines_int_t     period;
	ines_int_t     time_period;
	ines_sbyte_t   amp;
	ines_int_t     next;
	ines_int_t     out_from, out_to;
	ines_word_t    feedback;
	ines_int_t     tap;
	ines_int_t     delay;


	// 音量值 取决于是否为固定音量（Reg 0 Bit 4）
	// 如果Reg 0 Bit 4 设置为1，表示固定音量，则 Reg的0-3 位表示音量值（0-15），否则使用音量衰减单元的输出值，（0-15）
	volume = noise->length_counter == 0 ? 0 : noise->reg_ctrl[0] & 0x10 ? noise->reg_ctrl[0] & 0x0f : noise->envelope;
	// 频率计数器的初值由Reg 2 的低4位定。通过一个固的表格来转换成计数器装入值
	period = (ines_int_t)(noise_period_table[noise->reg_ctrl[2] & 0x0f]);

	// 多一个周期是因为计数器计数到0后不会立刻重新装入初始值，需要等到下一个APU周期才会重装装入。所以会多运行一个APU周期
	time_period = (period + 1) << 1;

	delay = noise->period_delay;

	INES_LOG(LOG_TRA, MOD_APU, ISTR("APU_NOISE_RUN: (%p) length=%d, volume=%d, period=%d\n"), noise, noise->length_counter, volume, period);
	INES_LOG(LOG_TRA, MOD_APU, ISTR("APU_NOISE_RUN: (%p) period_delay=%d\n"), noise, delay);

	if(volume == 0 )
	{
		if(from < to)
		{
			// 通道静音时：直接填充0个相应的周期
			out_from = CPUCYCLE2SAMPLENUM(from);
			out_to = CPUCYCLE2SAMPLENUM(to);
			if(out_from < out_to)
				INES_LOG(LOG_TRA, MOD_APU, ISTR("APU_NOISE_RUN: (%p) Fill sample [%d]->[%d] = %d\n"), noise, out_from, out_to, 0);
			from += delay;
			while(out_from < out_to)
			{
				assert(out_from >= 0 && out_from < MAX_SAMPLE_PER_FRAME);
				noise->buffer[out_from] = 0;
				out_from++;
			}
			from += (to - from + time_period) / time_period * time_period;

			delay = from - to;
		}
	}
	else
	{
		// 输出音量由序列样式决定，0 输出负音量，1 输出正的音量。
		// 序列计数器， 由频率定时器驱动。0~7 共8个值。决定了当前为序列样式第几Bit
		amp = (noise->shift_register & 0x01) ? (ines_sbyte_t)volume : -(ines_sbyte_t)volume;
		tap = (noise->reg_ctrl[2] & 0x80) ? 8 : 13;

		out_from = CPUCYCLE2SAMPLENUM(from);

		if(from & 1)  from++;

		while(from < to)
		{
			from += delay;
			if(from > to) 
				next = to;
			else
				next = from;
			out_to = CPUCYCLE2SAMPLENUM(next);

			if(out_from < out_to)
				INES_LOG(LOG_TRA, MOD_APU, ISTR("APU_NOISE_RUN: (%p) Fill sample [%d]->[%d] = %d\n"), noise, out_from, out_to, amp);
			// 输出当前序列
			while(out_from < out_to)
			{
				assert(out_from >= 0 && out_from < MAX_SAMPLE_PER_FRAME);
				noise->buffer[out_from] = amp;
				out_from++;
			}
			out_from = out_to;

			// 计算下一个序列的周期。
			if(  from <= to ) 
			{
				// 重新装入定时器值
				delay = time_period; 
				// 序列下一个Bit。
				feedback = (noise->shift_register<<tap)^(noise->shift_register<<14);
				noise->shift_register = (noise->shift_register >> 1) | (feedback & 0x4000);
				
				amp = (noise->shift_register & 0x01) ? (ines_sbyte_t)volume : -(ines_sbyte_t)volume;
			}
			else
			{
				delay = from - to;
			}
		}
	}
	noise->period_delay = delay;
}

static void noise_clock_length(ines_apu_noise_t* noise)
{
	if ( noise->length_counter > 0 && !(noise->reg_ctrl[0] & 0x20) )
		noise->length_counter--;
}

static void noise_clock_envelope(ines_apu_noise_t* noise)
{
	if(noise->reg_written[3])
	{
		// 写入音长寄存器，触发音量衰减速度计数器重新装入
		noise->reg_written[3] = 0;
		noise->env_delay = noise->reg_ctrl[0] & 0x0f;
		noise->envelope = 0xf;
	}
	else if(noise->env_delay > 0) 
	{
		// 音量衰减速度计数器向下计数
		noise->env_delay--;
	}
	else
	{
		// 重新装入音量衰减速度计数器
		noise->env_delay = noise->reg_ctrl[0] & 0x0f;
		// 衰减循环使能为1（寄存器0的BIT5）或者衰减音量没到0.则音量衰减一。
		// 如果衰减循环使能为0，则音量衰减到0后会一直停止在0，这样声道则静音。
		if(noise->envelope > 0 || (noise->reg_ctrl[0] & 0x20) ) 
		{
			noise->envelope = (noise->envelope - 1) & 0xf;
		}
	}	
}

///////////////////////////////////////////////////////////////////////////////////
// DMC
///////////////////////////////////////////////////////////////////////////////////

static void dmc_reset(ines_apu_dmc_t* dmc)
{
	dmc->reg_ctrl[0] = 0x10;
	dmc->reg_ctrl[1] = 0x0;
	dmc->reg_ctrl[2] = 0x0;
	dmc->reg_ctrl[3] = 0x0;
	dmc->reg_written[0] = 1;
	dmc->reg_written[1] = 1;
	dmc->reg_written[2] = 1;
	dmc->reg_written[3] = 1;


	dmc->is_ntsc = apu2host(dmc2apu(dmc))->setting.is_ntsc;
	dmc->length_counter = 0;
	dmc->period = dmc_period_table[dmc->is_ntsc ? 0 : 1][0xf];
	dmc->period_delay = 0;
	dmc->irq_enable = 0;
	dmc->irq_flag = 0;
	dmc->next_irq = 0;
	dmc->address = 0;
	dmc->bit = 0;
	dmc->bit_buffer = 0;
	dmc->bit_remain = 1;
	dmc->bit_empty = 1;
	dmc->mute = 1;
	dmc->dac = 0;
}

static void dmc_recalc_irq(ines_apu_dmc_t* dmc)
{
	ines_int_t new_irq = 0;
	if(dmc->irq_enable && dmc->length_counter)
	{
		new_irq =  dmc2apu(dmc)->last_cycles + dmc->period_delay
			+ ( (ines_int_t)(dmc->length_counter - 1) * 8 + dmc->bit_remain - 1 ) * dmc->period + 1 ;
	}

	if(new_irq != dmc->next_irq)
	{
		dmc->next_irq = new_irq;
		INES_LOG(LOG_DBG, MOD_APU, ISTR("APU_DMC calc next IRQ at [%")ISTR(PRI64)ISTR("x] cycles!\n"), dmc2apu(dmc)->frame_start_cpu_cycles + new_irq);
		irq_changed(dmc2apu(dmc));
	}
}


static void dmc_write(ines_apu_dmc_t* dmc, ines_word_t addr, ines_byte_t  val)
{
	if(addr == 0)
	{
		dmc->period = dmc_period_table[dmc->is_ntsc ? 0 : 1][val & 0x0f];
		dmc->irq_enable = ((val & 0xc0) == 0x80) ? 1 : 0;
		dmc->irq_flag &= dmc->irq_enable;
		dmc_recalc_irq(dmc);
	}
	else if(addr == 1)
	{
		dmc->dac = val  & 0x7f;
	}
}

static void dmc_reload(ines_apu_dmc_t* dmc)
{
	dmc->address = 0xc000 | ((ines_word_t)dmc->reg_ctrl[2] << 6);
	dmc->length_counter = dmc->reg_ctrl[3] * 0x10 + 1;
}

static void dmc_fill_buffer(ines_apu_dmc_t* dmc)
{
	if(dmc->bit_empty && dmc->length_counter > 0)
	{
		ines_host_read(apu2host(dmc2apu(dmc)), dmc->address);
		dmc->address = 0xc000 | (dmc->address + 1);
		dmc->bit_empty = 0;
		if(--dmc->length_counter == 0)
		{
			if(dmc->reg_ctrl[0] & 0x40)
			{
				dmc_reload(dmc);
			}
			else
			{
				dmc2apu(dmc)->reg_ctrl &= ~0x10; // for next enable to call dmc_start
				dmc->irq_flag = dmc->irq_enable;
				dmc->next_irq = 0;
				if(dmc->irq_flag)
				{
					INES_LOG(LOG_DBG, MOD_APU, ISTR("APU_DMC set IRQ!\n"));
				}
				irq_changed(dmc2apu(dmc));
			}
		}
	}
}

static void dmc_start(ines_apu_dmc_t* dmc)
{
	dmc_reload(dmc);
	dmc_fill_buffer(dmc);
	dmc_recalc_irq(dmc);	
}

static void dmc_run(ines_apu_dmc_t* dmc, ines_int_t  from, ines_int_t  to)
{
	ines_int_t     delay;
	ines_int_t     step;
	ines_int_t     count;
	ines_sbyte_t   amp;
	ines_int_t     next;
	ines_int_t     out_from, out_to;

	delay = dmc->period_delay;
	INES_LOG(LOG_TRA, MOD_APU, ISTR("APU_DMC_RUN: (%p) length=%d, bit=%d, bit_remain=%d, dac=%d, period=%d\n"), dmc, dmc->length_counter, dmc->bit, dmc->bit_remain, dmc->dac, dmc->period);
	INES_LOG(LOG_TRA, MOD_APU, ISTR("APU_DMC_RUN: (%p) period_delay=%d\n"), dmc, delay);

	if(from < to)
	{
		if(dmc->mute && dmc->bit_empty)
		{
			amp = dmc->dac;
			out_from = CPUCYCLE2SAMPLENUM(from);
			out_to = CPUCYCLE2SAMPLENUM(to);
			if(out_from < out_to)
				INES_LOG(LOG_TRA, MOD_APU, ISTR("APU_DMC_RUN: (%p) Fill sample [%d]->[%d] = %d\n"), dmc, out_from, out_to, amp);
			while(out_from < out_to)
			{
				assert(out_from >= 0 && out_from < MAX_SAMPLE_PER_FRAME);
				dmc->buffer[out_from] = amp;
				out_from++;
			}

			from += delay;
			count = (to - from + dmc->period) / dmc->period;
			// dmc->bit_remain = (dmc->bit_remain - 1 + 8 - (count % 8) ) % 8 + 1;
			from += dmc->period * count;

			delay = from - to;
		}
		else
		{
			out_from = CPUCYCLE2SAMPLENUM(from);

			do 
			{
				from += delay;
				if(from > to)
					next = to;
				else
					next = from;
				out_to = CPUCYCLE2SAMPLENUM(next);

				if(dmc->mute)
				{
					amp = dmc->dac;

					if(out_from < out_to)
						INES_LOG(LOG_TRA, MOD_APU, ISTR("APU_DMC_RUN: (%p) Fill sample [%d]->[%d] = %d\n"), dmc, out_from, out_to, amp);

					while(out_from < out_to)
					{
						assert(out_from >= 0 && out_from < MAX_SAMPLE_PER_FRAME);
						dmc->buffer[out_from] = amp;
						out_from++;
					}
				}
				else
				{
					//amp = (ines_sbyte_t)((ines_int_t)(dmc->dac << 1) - 0x7f);
					amp = dmc->dac;

					if(out_from < out_to)
						INES_LOG(LOG_TRA, MOD_APU, ISTR("APU_DMC_RUN: (%p) Fill sample [%d]->[%d] = %d\n"), dmc, out_from, out_to, amp);

					while(out_from < out_to)
					{
						assert(out_from >= 0 && out_from < MAX_SAMPLE_PER_FRAME);
						dmc->buffer[out_from] = amp;
						out_from++;
					}


					if(dmc->bit_buffer & 1)
						step = 2;
					else 
						step = -2;
					dmc->bit_buffer >>= 1;

					if((ines_byte_t)(dmc->dac + step) <= 0x7f)
					{
						dmc->dac += step;
					}
				}

				if(from <= to)
				{

					delay = dmc->period;
					
					if(--dmc->bit_remain  == 0)
					{
						dmc->bit_remain = 8;

						if(dmc->bit_empty)
							dmc->mute = 1;
						else
						{
							dmc->mute = 0;
							dmc->bit = dmc->bit_buffer;
							dmc->bit_empty = 1;
							INES_LOG(LOG_TRA, MOD_APU, ISTR("APU_DMC_RUN: (%p) load bit=%d\n"), dmc, dmc->bit);
							dmc_fill_buffer(dmc); // load next byte
						}
					}
				}
				else
				{
					delay = from - to;
				}
			}
			while ( from < to);
		}
	}
	dmc->period_delay = delay;
}

#pragma pack(push, 1)

struct _ines_state_apu_data_
{
	/* 0 - 7 : 8 bytes */
	ines_byte_t         reg_frame_mode;	
	ines_byte_t         reg_ctrl;
	ines_byte_t         irq_flag;
	ines_byte_t         reserved0;
	ines_int_t          next_irq;
	/* 8 - 15 : 8 bytes */
	ines_int_t          cur_frame;
	ines_int_t          frame_period;
	/* 16 - 23 : 8 bytes */
	ines_int_t          frame_delay;
	ines_int_t          reserved1;
	/* 24 - 31 : 8 bytes */
	ines_int64_t        frame_start_cpu_cycles;
	/* 32 - 39 : 8 bytes */
	ines_int_t          last_cycles;
	ines_int_t          prev_out_x1000;  // 用于平滑输出（低通滤波）
};
/* total: 40 bytes */
typedef struct _ines_state_apu_data_   ines_state_apu_data_t;


struct _ines_state_apu_pulse_data_
{
	/* 8 */
	ines_byte_t   reg_ctrl[4];     // 控制寄存器s
	ines_byte_t   reg_written[4];  // 控制寄存器写入标记
	/* 8 */
	ines_byte_t   envelope;        // 
	ines_byte_t   env_delay;
	ines_byte_t   duty_counter;    // 序列计数器
	ines_byte_t   sweep_delay;
	ines_word_t   length_counter;  // 音长计数器
	ines_word_t   period_delay;    // 
};
typedef struct _ines_state_apu_pulse_data_   ines_state_apu_pulse_data_t;


struct _ines_state_apu_triangle_data_
{
	/* 8 */
	ines_byte_t   reg_ctrl[4];     // 控制寄存器
	ines_byte_t   reg_written[4];  // 控制寄存器写入标记
	/* 8 */
	ines_byte_t   linear_counter;  // 线性计数器
	ines_byte_t   phase_counter;   // 三角阶梯计数器
	ines_byte_t   reserved1; 
	ines_byte_t   reserved2; 
	ines_word_t   length_counter;  // 音长计数器
	ines_word_t   period_delay;  // 周期计数器

};

typedef struct _ines_state_apu_triangle_data_    ines_state_apu_triangle_data_t;

struct _ines_state_apu_noise_data_
{
	/* 8 */
	ines_byte_t   reg_ctrl[4];     // 控制寄存器
	ines_byte_t   reg_written[4];  // 控制寄存器写入标记
	/* 8 */
	ines_byte_t   envelope;        // 
	ines_byte_t   env_delay;
	ines_word_t   length_counter;  // 音长计数器
	ines_word_t   shift_register;  // 移位寄存器
	ines_word_t   period_delay;    // 周期计数器
};

typedef struct _ines_state_apu_noise_data_    ines_state_apu_noise_data_t;

struct _ines_state_apu_dmc_data_
{
	/* 8 */
	ines_byte_t   reg_ctrl[4];     // 控制寄存器
	ines_byte_t   reg_written[4];  // 控制寄存器写入标记
	/* 8 */
	ines_word_t   length_counter;  // 音长计数器
	ines_word_t   period;          // 周期
	ines_int_t    period_delay;    // 周期计数器
	/* 8 */
	//ines_byte_t   is_ntsc;
	//ines_byte_t   mute;
	//ines_byte_t   irq_enable;
	//ines_byte_t   irq_flag;
	//ines_byte_t   bit_empty;
	ines_byte_t   flags; // 0: is_ntsc; 1: mute; 2: irq_enable; 3: irq_flag; 4: bit_empty
	ines_byte_t   dac;
	ines_word_t   address;
	ines_byte_t   bit;
	ines_byte_t   bit_buffer;
	ines_byte_t   bit_remain;
	ines_byte_t   reserved1;
	/* 8 */
	ines_int_t    next_irq;
	ines_int_t    reserved2;
};
typedef struct _ines_state_apu_dmc_data_    ines_state_apu_dmc_data_t;

#pragma pack(pop)

ines_int_t pulse_save_state(ines_apu_pulse_t* pulse, FILE* fSave)
{
	ines_state_apu_pulse_data_t   data;
	memset(&data, 0, sizeof(data));
	data.reg_ctrl[0] = pulse->reg_ctrl[0];     // 控制寄存器s
	data.reg_ctrl[1] = pulse->reg_ctrl[1];     // 控制寄存器s
	data.reg_ctrl[2] = pulse->reg_ctrl[2];     // 控制寄存器s
	data.reg_ctrl[3] = pulse->reg_ctrl[3];     // 控制寄存器s
	data.reg_written[0] = pulse->reg_written[0];  // 控制寄存器写入标记
	data.reg_written[1] = pulse->reg_written[1];  // 控制寄存器写入标记
	data.reg_written[2] = pulse->reg_written[2];  // 控制寄存器写入标记
	data.reg_written[3] = pulse->reg_written[3];  // 控制寄存器写入标记
	data.duty_counter = pulse->duty_counter;    // 序列计数器
	data.envelope = pulse->duty_counter;        // 
	data.env_delay = pulse->env_delay;
	data.sweep_delay = pulse->sweep_delay;
	data.length_counter = pulse->length_counter;  // 音长计数器
	data.period_delay = pulse->period_delay;    // 
	fwrite(&data, sizeof(data), 1, fSave);

	return 0;
}

ines_int_t pulse_load_state(ines_apu_pulse_t* pulse, FILE* fSave)
{
	ines_state_apu_pulse_data_t   data;
	if(1 != fread(&data, sizeof(data), 1, fSave))
		return -1;
	
	pulse->reg_ctrl[0] = data.reg_ctrl[0];     // 控制寄存器s
	pulse->reg_ctrl[1] = data.reg_ctrl[1];     // 控制寄存器s
	pulse->reg_ctrl[2] = data.reg_ctrl[2];     // 控制寄存器s
	pulse->reg_ctrl[3] = data.reg_ctrl[3];     // 控制寄存器s
	pulse->reg_written[0] = data.reg_written[0];  // 控制寄存器写入标记
	pulse->reg_written[1] = data.reg_written[1];  // 控制寄存器写入标记
	pulse->reg_written[2] = data.reg_written[2];  // 控制寄存器写入标记
	pulse->reg_written[3] = data.reg_written[3];  // 控制寄存器写入标记
	pulse->duty_counter = data.duty_counter;    // 序列计数器
	pulse->duty_counter = data.envelope;        // 
	pulse->env_delay = data.env_delay;
	pulse->sweep_delay = data.sweep_delay;
	pulse->length_counter = data.length_counter;  // 音长计数器
	pulse->period_delay = data.period_delay;    // 

	return 0;
}


ines_int_t triangle_save_state(ines_apu_triangle_t* triangle, FILE* fSave)
{
	ines_state_apu_triangle_data_t   data;
	memset(&data, 0, sizeof(data));
	data.reg_ctrl[0] = triangle->reg_ctrl[0];     // 控制寄存器
	data.reg_ctrl[1] = triangle->reg_ctrl[1];     // 控制寄存器
	data.reg_ctrl[2] = triangle->reg_ctrl[2];     // 控制寄存器
	data.reg_ctrl[3] = triangle->reg_ctrl[3];     // 控制寄存器
	data.reg_written[0] = triangle->reg_written[0];  // 控制寄存器写入标记
	data.reg_written[1] = triangle->reg_written[1];  // 控制寄存器写入标记
	data.reg_written[2] = triangle->reg_written[2];  // 控制寄存器写入标记
	data.reg_written[3] = triangle->reg_written[3];  // 控制寄存器写入标记
	data.linear_counter = triangle->linear_counter;  // 线性计数器
	data.phase_counter = triangle->phase_counter;   // 三角阶梯计数器
	data.length_counter = triangle->length_counter;  // 音长计数器
	data.period_delay = triangle->period_delay;  // 周期计数器
	fwrite(&data, sizeof(data), 1, fSave);

	return 0;
}


ines_int_t triangle_load_state(ines_apu_triangle_t* triangle, FILE* fSave)
{
	ines_state_apu_triangle_data_t   data;
	if(1 != fread(&data, sizeof(data), 1, fSave))
		return -1;

	triangle->reg_ctrl[0] = data.reg_ctrl[0];     // 控制寄存器
	triangle->reg_ctrl[1] = data.reg_ctrl[1];     // 控制寄存器
	triangle->reg_ctrl[2] = data.reg_ctrl[2];     // 控制寄存器
	triangle->reg_ctrl[3] = data.reg_ctrl[3];     // 控制寄存器
	triangle->reg_written[0] = data.reg_written[0];  // 控制寄存器写入标记
	triangle->reg_written[1] = data.reg_written[1];  // 控制寄存器写入标记
	triangle->reg_written[2] = data.reg_written[2];  // 控制寄存器写入标记
	triangle->reg_written[3] = data.reg_written[3];  // 控制寄存器写入标记
	triangle->linear_counter = data.linear_counter;  // 线性计数器
	triangle->phase_counter = data.phase_counter;   // 三角阶梯计数器
	triangle->length_counter = data.length_counter;  // 音长计数器
	triangle->period_delay = data.period_delay;  // 周期计数器

	return 0;
}

ines_int_t noise_save_state(ines_apu_noise_t* noise, FILE* fSave)
{
	ines_state_apu_noise_data_t  data;
	memset(&data, 0, sizeof(data));
	data.reg_ctrl[0] = noise->reg_ctrl[0];     // 控制寄存器
	data.reg_ctrl[1] = noise->reg_ctrl[1];     // 控制寄存器
	data.reg_ctrl[2] = noise->reg_ctrl[2];     // 控制寄存器
	data.reg_ctrl[3] = noise->reg_ctrl[3];     // 控制寄存器
	data.reg_written[0] = noise->reg_written[0];  // 控制寄存器写入标记
	data.reg_written[1] = noise->reg_written[1];  // 控制寄存器写入标记
	data.reg_written[2] = noise->reg_written[2];  // 控制寄存器写入标记
	data.reg_written[3] = noise->reg_written[3];  // 控制寄存器写入标记
	data.envelope = noise->envelope;        // 
	data.env_delay = noise->env_delay;
	data.length_counter = noise->length_counter;  // 音长计数器
	data.shift_register = noise->shift_register;  // 移位寄存器
	data.period_delay = noise->period_delay;    // 周期计数器
	fwrite(&data, sizeof(data), 1, fSave);

	return 0;
}

ines_int_t noise_load_state(ines_apu_noise_t* noise, FILE* fSave)
{
	ines_state_apu_noise_data_t  data;
	if(1 != fread(&data, sizeof(data), 1, fSave))
		return -1;

	noise->reg_ctrl[0] = data.reg_ctrl[0];     // 控制寄存器
	noise->reg_ctrl[1] = data.reg_ctrl[1];     // 控制寄存器
	noise->reg_ctrl[2] = data.reg_ctrl[2];     // 控制寄存器
	noise->reg_ctrl[3] = data.reg_ctrl[3];     // 控制寄存器
	noise->reg_written[0] = data.reg_written[0];  // 控制寄存器写入标记
	noise->reg_written[1] = data.reg_written[1];  // 控制寄存器写入标记
	noise->reg_written[2] = data.reg_written[2];  // 控制寄存器写入标记
	noise->reg_written[3] = data.reg_written[3];  // 控制寄存器写入标记
	noise->envelope = data.envelope;        // 
	noise->env_delay = data.env_delay;
	noise->length_counter = data.length_counter;  // 音长计数器
	noise->shift_register = data.shift_register;  // 移位寄存器
	noise->period_delay = data.period_delay;    // 周期计数器

	return 0;
}

ines_int_t dmc_save_state(ines_apu_dmc_t* dmc, FILE* fSave)
{
	ines_state_apu_dmc_data_t  data;
	memset(&data, 0, sizeof(data));

	data.reg_ctrl[0] = dmc->reg_ctrl[0];     // 控制寄存器
	data.reg_ctrl[1] = dmc->reg_ctrl[1];     // 控制寄存器
	data.reg_ctrl[2] = dmc->reg_ctrl[2];     // 控制寄存器
	data.reg_ctrl[3] = dmc->reg_ctrl[3];     // 控制寄存器
	data.reg_written[0] = dmc->reg_written[0];  // 控制寄存器写入标记
	data.reg_written[1] = dmc->reg_written[1];  // 控制寄存器写入标记
	data.reg_written[2] = dmc->reg_written[2];  // 控制寄存器写入标记
	data.reg_written[3] = dmc->reg_written[3];  // 控制寄存器写入标记
	data.length_counter = dmc->length_counter;  // 音长计数器
	data.period = dmc->period;          // 周期
	data.period_delay = dmc->period_delay;    // 周期计数器
	data.next_irq = dmc->next_irq;
	//data.is_ntsc = dmc->is_ntsc;
	//data.mute = dmc->mute;
	//data.irq_enable = dmc->irq_enable;
	//data.irq_flag = dmc->irq_flag;
	//data.bit_empty = dmc->bit_empty;
	data.flags = MAKE_BYTES(dmc->is_ntsc,dmc->mute,dmc->irq_enable,dmc->irq_flag,dmc->bit_empty,0,0,0); 
	data.dac = dmc->dac;
	data.address = dmc->address;
	data.bit = dmc->bit;
	data.bit_buffer = dmc->bit_buffer;
	data.bit_remain = dmc->bit_remain;

	fwrite(&data, sizeof(data), 1, fSave);

	return 0;
}

ines_int_t dmc_load_state(ines_apu_dmc_t* dmc, FILE* fSave)
{
	ines_state_apu_dmc_data_t  data;
	if(1 != fread(&data, sizeof(data), 1, fSave))
		return -1;

	dmc->reg_ctrl[0] = data.reg_ctrl[0];     // 控制寄存器
	dmc->reg_ctrl[1] = data.reg_ctrl[1];     // 控制寄存器
	dmc->reg_ctrl[2] = data.reg_ctrl[2];     // 控制寄存器
	dmc->reg_ctrl[3] = data.reg_ctrl[3];     // 控制寄存器
	dmc->reg_written[0] = data.reg_written[0];  // 控制寄存器写入标记
	dmc->reg_written[1] = data.reg_written[1];  // 控制寄存器写入标记
	dmc->reg_written[2] = data.reg_written[2];  // 控制寄存器写入标记
	dmc->reg_written[3] = data.reg_written[3];  // 控制寄存器写入标记
	dmc->length_counter = data.length_counter;  // 音长计数器
	dmc->period = data.period;          // 周期
	dmc->period_delay = data.period_delay;    // 周期计数器
	dmc->next_irq = data.next_irq;
	dmc->is_ntsc = (data.flags & 0x01) ? 1 : 0;
	dmc->mute = (data.flags & 0x02) ? 1 : 0;
	dmc->irq_enable = (data.flags & 0x04) ? 1 : 0;
	dmc->irq_flag = (data.flags & 0x08) ? 1 : 0;
	dmc->bit_empty = (data.flags & 0x10) ? 1 : 0;
	dmc->dac = data.dac;
	dmc->address = data.address;
	dmc->bit = data.bit;
	dmc->bit_buffer = data.bit_buffer;
	dmc->bit_remain = data.bit_remain;

	return 0;
}


ines_int_t ines_apu_save_state(ines_apu_t* p_apu, FILE* fSave)
{
	ines_state_apu_data_t   data;
	memset(&data, 0, sizeof(data));
	data.reg_frame_mode = p_apu->reg_frame_mode;	
	data.reg_ctrl = p_apu->reg_ctrl;
	data.irq_flag = p_apu->irq_flag;
	// data.reserved = 0;
	data.next_irq = p_apu->next_irq;
	data.cur_frame = p_apu->cur_frame;
	data.frame_period = p_apu->frame_period;
	data.frame_delay = p_apu->frame_delay;
	data.frame_start_cpu_cycles = p_apu->frame_start_cpu_cycles;
	data.last_cycles = p_apu->last_cycles;
	data.prev_out_x1000 = (ines_int_t)(p_apu->prev_out * 1000);  // 用于平滑输出（低通滤波）


	fwrite(&data, sizeof(data), 1, fSave);


	pulse_save_state(&p_apu->channel_pulse1, fSave);
	pulse_save_state(&p_apu->channel_pulse2, fSave);
	triangle_save_state(&p_apu->channel_triangle, fSave);
	noise_save_state(&p_apu->channel_noise, fSave);
	dmc_save_state(&p_apu->channel_dmc, fSave);
	
	return 0;
}


ines_int_t ines_apu_load_state(ines_apu_t* p_apu, FILE* fSave)
{
	ines_state_apu_data_t   data;


	if(1 != fread(&data, sizeof(data), 1, fSave))
		return -1;

	memset(p_apu, 0, sizeof(*p_apu));

	p_apu->reg_frame_mode = data.reg_frame_mode;	
	p_apu->reg_ctrl = data.reg_ctrl;
	p_apu->irq_flag = data.irq_flag;
	// data.reserved = 0;
	p_apu->next_irq = data.next_irq;
	p_apu->cur_frame = data.cur_frame;
	p_apu->frame_period = data.frame_period;
	p_apu->frame_delay = data.frame_delay;
	p_apu->frame_start_cpu_cycles = data.frame_start_cpu_cycles;
	p_apu->last_cycles = data.last_cycles;
	p_apu->prev_out = data.prev_out_x1000 / 1000.0f;  // 用于平滑输出（低通滤波）


	if (0 != pulse_load_state(&p_apu->channel_pulse1, fSave) ||
		0 != pulse_load_state(&p_apu->channel_pulse2, fSave) ||
		0 != triangle_load_state(&p_apu->channel_triangle, fSave) ||
		0 != noise_load_state(&p_apu->channel_noise, fSave) ||
		0 != dmc_load_state(&p_apu->channel_dmc, fSave) )
	{
		return -1;
	}
	return 0;
}
