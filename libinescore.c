#include "core/nes.h"
#include "comm/log.h"
#include "comm/thread.h"
#ifdef WIN32  // for timeb
#include <windows.h>
#include <sys/timeb.h>
#endif


#ifdef WIN32
	#ifdef INESCORE_DLL_EXPORTS
		#define DLLEXPORT   __declspec(dllexport)
	#else
		#define DLLEXPORT   __declspec(dllimport)
	#endif
#else
	#define DLLEXPORT
#endif

extern ines_int_t  nes_cpu_trace_ops;
static   ines_host_t    g_host;
static   ines_mutex_t   g_mutex_input;
static   ines_mutex_t   g_mutex_output;
static   ines_thread_t  g_thread;
static   ines_int_t     g_running = 0;   // flag for mark the host is in running

// input
static   ines_int_t     g_volumn = 100;  // set output audio volumn
static   ines_int_t     g_stop_flag = 0;  // for stop the nes host thread
static   ines_int_t     g_main_key_state;
static   ines_int_t     g_second_key_state;
static   ines_int_t     g_reset_key_state;

static  ines_char_t     g_ram_file[1024];

// output
static   ines_byte_t    g_render_buffer[2][SCREEN_PIXELS];
static   ines_int_t     g_render_idx = 0;  // for swap render buffer
static   ines_int_t     g_render_ready = 0;  // for flip render buffer
#define MAX_AUDIO_BUFFER_LEN    (1024+NES_AUDIO_BYTES_PER_SECOND/5)
static   ines_byte_t    g_audio_buffer[MAX_AUDIO_BUFFER_LEN];
static   ines_byte_t    g_audio_buffer_run[1024];
static   ines_int_t     g_audio_len = 0;
static   ines_int64_t   g_audio_total_bytes = 0; /// for calc total audio time

// static
static   ines_int64_t   g_frame_count;     
static   ines_int64_t   g_last_frame_time;  // us
static   ines_int64_t   g_start_frame_time;  // us
static   ines_int_t     g_frame_rate;       // fps*100
static   ines_int_t     g_cpu_usage;        // rate*1000
static   ines_int64_t   g_frame_time_us;
static   ines_int64_t   g_last_fps_time;  // us
static   ines_int64_t   g_count_cpu_time;  // us

static ines_int64_t  get_cycles(void)
{
	return g_host.cpu.total_cycles;
}


static ines_int64_t  get_cur_time_us()
{
#ifdef WIN32
	struct _timeb timebuffer;
	LARGE_INTEGER   lf, lc;

// 计算更高精度的时间
	static  ines_int64_t    g_time_base_us = 0;
	static  ines_int64_t    g_time_base_counter = 0;
	static  ines_int64_t    g_time_base_freq = 0;

	if(g_time_base_counter == 0)
	{
		QueryPerformanceFrequency(&lf);
		QueryPerformanceCounter(&lc);
		_ftime( &timebuffer ); // only support ms
		g_time_base_freq = (ines_int64_t)lf.QuadPart;
		g_time_base_counter = (ines_int64_t)lc.QuadPart;
		g_time_base_us = timebuffer.time* 1000000 + timebuffer.millitm * 1000;
		return g_time_base_us;
	}

	QueryPerformanceCounter(&lc);

	return g_time_base_us + (ines_int64_t)( (double)(lc.QuadPart - g_time_base_counter) / g_time_base_freq * 1000000.0 );

#elif defined (linux)

	struct timeval  tv;
	gettimeofday(&tv, NULL);

	return (ines_int64_t)tv.tv_sec * 1000000 + tv.tv_usec;
#endif
}

static void do_sleep(ines_int64_t  us)
{
#ifdef WIN32
	Sleep((DWORD)(us/1000));
#elif defined (linux)
	usleep(us);
#else
	usleep(us);
#endif 
}


// 如果是windows 则传入的字符串要从utf8转换到ascii或者unicode
#ifdef WIN32
static ines_char_t g_tbuffer_1[1024];
static ines_char_t g_tbuffer_2[1024];
static ines_char_t g_tbuffer_3[8192];

ines_char_t *  utf8_to_t(const char*  str, ines_char_t* buffer, ines_size_t  size)
{
#ifdef _UNICODE
	MultiByteToWideChar(CP_UTF8, 0, str, -1, buffer, size);   // utf-8 to unicode
#else
	wchar_t wsz[8192];
	MultiByteToWideChar(CP_UTF8, 0, str, -1, wsz, 8192);   // utf-8 to unicode
	WideCharToMultiByte(CP_ACP, 0, wsz, -1, buffer, size, NULL, NULL);  // unicode to ascii
#endif
	return buffer;
}
#else
#define utf8_to_t(str, buf, sz)   (str)
#endif

static int nes_proc(void* ud)
{
	ines_int64_t   cur_us;
	ines_int64_t   diff_us;
	ines_int64_t   frame_us;
	ines_int64_t   audio_us;
	ines_int64_t   last_save_us;

	ines_int_t     main_key_state;
	ines_int_t     second_key_state;
	ines_int_t     reset_key_state;
	ines_int_t     audio_volumn;

	ines_byte_t*    render_buffer;
	ines_byte_t*    audio_buffer;
	ines_int_t      audio_buffer_len;

	INES_LOG(LOG_NTY, MOD_SYS, ISTR(" ++ HOST THREAD RUNNING ++\n"));

	last_save_us = 0;
	
	g_frame_count = 0;
	g_last_frame_time = get_cur_time_us();
	g_start_frame_time = g_last_frame_time;

	g_last_fps_time = g_last_frame_time;
	g_count_cpu_time = 0;


	// reset first
	ines_host_reset(&g_host);


	while(g_stop_flag == 0)
	{
		frame_us = (ines_int64_t)(g_host.setting.frame_rate * 1000000.0);
		audio_us = (ines_int64_t)(g_audio_total_bytes * (1000000.0/44100.0)) + g_start_frame_time - g_last_frame_time;

		// 如果
		if(audio_us < 0)
			frame_us -= frame_us >> 2;
		else if(audio_us > (frame_us >> 1))
			frame_us += frame_us >> 2;


		// 计算时间
		while(1)
		{
			cur_us = get_cur_time_us();
			diff_us = cur_us - g_last_frame_time;

			

			if(diff_us >= frame_us)
				break;

			if(g_stop_flag != 0)
				break;

			if(frame_us - diff_us > 1000) // > 1 ms
			{
				do_sleep(  1000  );  // check each 1 ms
			}
			else
			{
				do_sleep(0); // 
			}
		}

		if(g_stop_flag != 0)
			break;

		g_last_frame_time = cur_us;

		// 读取输入
		ines_mutex_lock(&g_mutex_input);
		main_key_state = g_main_key_state;
		second_key_state = g_second_key_state;
		reset_key_state = g_reset_key_state;
		// reset
		// g_reset_key_state = 0;
		audio_volumn = g_volumn;
		ines_mutex_unlock(&g_mutex_input);

		// 输出
		render_buffer = g_render_buffer[g_render_idx];
		audio_buffer = g_audio_buffer_run;
		audio_buffer_len = sizeof(g_audio_buffer_run);

		
		if(reset_key_state == 1)
		{
			// do soft reset
			ines_host_reset(&g_host);

			// clear render buffer and audio buffer
			memset(render_buffer, 0,  SCREEN_PIXELS);

			g_last_frame_time = get_cur_time_us();
			g_start_frame_time = g_last_frame_time;
			g_last_fps_time = g_last_frame_time;
			g_count_cpu_time = 0;


			ines_mutex_lock(&g_mutex_output);
			g_audio_len = 0;
			g_audio_total_bytes = 0;
			g_render_idx = 1 - g_render_idx;
			g_render_ready = 1;
			g_frame_time_us = 0;
			g_frame_count = 0;
			ines_mutex_unlock(&g_mutex_output);

		}
		else
		{
			ines_apu_setoutbuffer(&g_host.apu, audio_buffer, audio_buffer_len, audio_volumn);

			ines_joypad_update_bits(&g_host.joypad, main_key_state, second_key_state);

			ines_host_doframe(&g_host, render_buffer);

			// check save RAM each 5 sec
			if(cur_us - last_save_us > 5000000) 
			{
				last_save_us = cur_us;
				ines_host_save_sram(&g_host, g_ram_file);

			}

			audio_buffer_len = ines_apu_getoutlen(&g_host.apu);

			cur_us = get_cur_time_us();

			ines_mutex_lock(&g_mutex_output);
			if(g_audio_len + audio_buffer_len > sizeof(g_audio_buffer))
			{
				// cut overflow bytes
				int cut_bytes = g_audio_len + audio_buffer_len - sizeof(g_audio_buffer);
				memmove(g_audio_buffer, g_audio_buffer + cut_bytes,  g_audio_len - cut_bytes);
				g_audio_len -= cut_bytes;
			}
			memcpy(g_audio_buffer + g_audio_len, audio_buffer, audio_buffer_len);
			g_audio_len += audio_buffer_len;
			g_audio_total_bytes += audio_buffer_len;
			g_render_idx = 1 - g_render_idx;
			g_render_ready = 1;

			g_frame_time_us =  cur_us - g_last_frame_time;
			g_count_cpu_time += g_frame_time_us;
			if(cur_us - g_last_fps_time > 1000000)  // 1 second
			{
				g_cpu_usage = (ines_int_t) (1000 * g_count_cpu_time / (cur_us - g_last_fps_time));
				g_last_fps_time = cur_us;
				g_count_cpu_time = 0;
			}
			g_frame_count ++;
			ines_mutex_unlock(&g_mutex_output);

			INES_LOG(LOG_DBG, MOD_SYS, ISTR("[%")ISTR(PRI64)ISTR("d] NES_Frame_No [%")ISTR(PRI64)ISTR("d], Time: %")ISTR(PRI64)ISTR("d us\n"),cur_us, g_frame_count, g_frame_time_us);
		}
	}

	// check save RAM while stopped
	ines_host_save_sram(&g_host, g_ram_file);

	INES_LOG(LOG_NTY, MOD_SYS, ISTR(" -- HOST THREAD STOPPED --\n"));
	return 0;
}



DLLEXPORT  int ines_init_lib(void)
{
	ines_set_log_stamp_func(get_cycles);

	INES_LOG(LOG_NTY, MOD_SYS, ISTR("=========================================================\n"));
#ifdef _DEBUG
	INES_LOG(LOG_NTY, MOD_SYS, ISTR("iNES v1.0(Debug), Build Time ")ISTR(__TIMESTAMP__)ISTR(", Initializing...\n"));
#else
	INES_LOG(LOG_NTY, MOD_SYS, ISTR("iNES v1.0, Build Time ")ISTR(__TIMESTAMP__)ISTR(", Initializing...\n"));
#endif

	g_running = 0;

	g_volumn = 100;

	return 0;
}

DLLEXPORT  void ines_set_loglevel(int level, int cpu_trace)
{
	ines_set_log_level(level);
	nes_cpu_trace_ops = cpu_trace;
}

DLLEXPORT  void ines_log_text(int level, const char* message)
{
	ines_cstr_t ts = utf8_to_t(message, g_tbuffer_3, sizeof(g_tbuffer_3)/sizeof(g_tbuffer_3[0]));
	INES_LOG(level, MOD_SYS, ISTR("%s\n"), ts);
}


DLLEXPORT  void ines_set_volumn(int vol)  // 0~100
{
	if(0 == g_running)
		g_volumn = vol; // maybe unsafe
	else
	{
		ines_mutex_lock(&g_mutex_input);
		g_volumn = vol; 
		ines_mutex_unlock(&g_mutex_input);

	}
}


DLLEXPORT  int ines_is_running(void)
{
	return g_running;
}



DLLEXPORT  int ines_start(int is_ntsc, const char*  rom_file, const char* ram_file)
{
	// int ret;
	ines_cstr_t  _rom;
	ines_cstr_t  _ram;

	// is in running ?
	if(g_running != 0)
		return -1;

	_rom = utf8_to_t(rom_file, g_tbuffer_1, sizeof(g_tbuffer_1)/sizeof(g_tbuffer_1[0]));
	_ram = utf8_to_t(ram_file, g_tbuffer_2, sizeof(g_tbuffer_2)/sizeof(g_tbuffer_2[0]));

	// init host
	
	ines_host_init(&g_host, is_ntsc);

	if(! ines_host_load_rom(&g_host, _rom, _ram ) )
	{
		ines_host_free(&g_host);
		return -1;
	}

	ines_strncpy(g_ram_file, _ram, sizeof(g_ram_file)/sizeof(g_ram_file[0]));

	g_running = 1; // 先设置。防止线程直接即出先修改了running标志
	g_stop_flag = 0;

	g_frame_count = 0;
	g_frame_rate = 0;
	g_cpu_usage = 0;
	g_frame_time_us = 0;

	g_render_idx = 0;
	g_render_ready = 0;
	g_audio_len = 0;
	g_audio_total_bytes = 0;

	g_main_key_state = 0;
	g_second_key_state = 0;
	g_reset_key_state = 0;

	// create thread etc

	if(0 != ines_mutex_init(&g_mutex_input) ||
		0 != ines_mutex_init(&g_mutex_output) ||
		0 != ines_thread_init(&g_thread, nes_proc, NULL) ||
		0 != ines_thread_start(&g_thread))
	{
		INES_LOG(LOG_ERR, MOD_SYS, ISTR("create nes host thread failed!\n"));
		ines_host_free(&g_host);
		g_running = 0;
		return -1;
	}


	//ines_thread_set_auto_detach(&g_thread);
	
	return 0;
}


DLLEXPORT  int ines_stop(void)
{
	if(g_running != 1)
		return -1;  // not in running
	
	g_stop_flag = 1;

	ines_thread_wait(&g_thread);

	g_running = 0;

	ines_host_free(&g_host);
	INES_LOG(LOG_NTY, MOD_SYS, ISTR("iNES Shutdown OK.\n"));


	ines_thread_fini(&g_thread);
	ines_mutex_fini(&g_mutex_input);	
	ines_mutex_fini(&g_mutex_output);	

	return 0;
}


DLLEXPORT  int ines_update_input(int main_key_state, int second_key_state, int reset_key_state)
{
	if(0 == g_running)
		return -1;

	ines_mutex_lock(&g_mutex_input);
	g_main_key_state = main_key_state;
	g_second_key_state = second_key_state;
	g_reset_key_state = reset_key_state;
	ines_mutex_unlock(&g_mutex_input);


	return 0;
}

DLLEXPORT  int ines_is_vedio_ready(void)
{
	int ret = 0;

	if(0 == g_running)
		return 0;

	ines_mutex_lock(&g_mutex_output);
	ret = g_render_ready;
	ines_mutex_unlock(&g_mutex_output);
	return ret;
}

DLLEXPORT  int ines_get_vedio_data(ines_byte_t*  buffer, int len)
{
	if(len > SCREEN_PIXELS)
		len = SCREEN_PIXELS;
	if(0 == g_running)
	{
		memset(buffer, 0, len);
	}
	else
	{
		ines_mutex_lock(&g_mutex_output);
		memcpy(buffer, g_render_buffer[1 - g_render_idx], len);
		ines_mutex_unlock(&g_mutex_output);
	}

	return len;
}

DLLEXPORT  int ines_get_audio_length(void)
{
	int ret = 0;

	if(0 == g_running)
		return 0;

	ines_mutex_lock(&g_mutex_output);
	ret = g_audio_len;
	ines_mutex_unlock(&g_mutex_output);
	return ret;
}

DLLEXPORT  int ines_get_audio_data(ines_byte_t*  buffer, int len)
{
	if( 0 == g_running )
	{
		return 0;
	}
	else
	{
		ines_mutex_lock(&g_mutex_output);
		if(len > g_audio_len)
			len = g_audio_len;
		memcpy(buffer, g_audio_buffer, len);
		if(len < g_audio_len)
		{
			g_audio_len -= len;
			memmove(g_audio_buffer, g_audio_buffer + len, g_audio_len);
		}
		else
		{
			g_audio_len = 0;
		}
		ines_mutex_unlock(&g_mutex_output);

	}
	return len;
}


DLLEXPORT  int ines_get_frame_time_us()
{
	int ret;
	if( 0 == g_running )
	{
		return 0;
	}
	else
	{
		ines_mutex_lock(&g_mutex_output);
		ret = (int)g_frame_time_us;
		ines_mutex_unlock(&g_mutex_output);
	}
	return ret;
}


DLLEXPORT  int ines_pause()
{
	return 0;
}

DLLEXPORT  int ines_step()
{
	return 0;
}

DLLEXPORT  int ines_load(const char* save_file)
{
	if(g_host.status != NES_STATUS_RUNNING)
		return -5;
	return 0;
}

DLLEXPORT  int ines_save(const char* save_file)
{
	if(g_host.status != NES_STATUS_RUNNING)
		return -5;
	return 0;
}


#if 0  // {{{

#pragma pack(push, 1) // 需要和C#定义对齐

typedef struct _ines_running_context_   ines_running_context_t;

struct _ines_running_context_  
{
	/* input context */
	ines_int_t    log_level;
	ines_int_t    cpu_trace_ops;
	ines_int_t    main_key_state;
	ines_int_t    second_key_state;
	ines_int_t    audio_volume;
	/* output context */
	//ines_byte_t   render_buffer[SCREEN_PIXELS]; // bmp, 8bit , 256 * 240, max 64 colors 
	//ines_byte_t   audio_buffer[4096]; // single channel. 8 bit, sample rate: 44100 
};

#pragma pack(pop)


DLLEXPORT  int ines_test(ines_running_context_t*  context)
{
	INES_LOG(LOG_NTY, MOD_SYS, ISTR("%d,%d,%d,%d,%d"), context->log_level, context->cpu_trace_ops, context->main_key_state, context->second_key_state, context->audio_volume);
	memset(context->render_buffer, 0, sizeof(context->render_buffer));
	memset(context->audio_buffer, 127, sizeof(context->audio_buffer));
	context->render_buffer[0] = 15;
	context->render_buffer[1] = 16;
	context->render_buffer[2] = 17;

DLLEXPORT  int ines_test(int  n, float fv, char*  out_str, int len)
{
	ines_byte_t    b1 = 0xfb;
	ines_sbyte_t   b2 = b1;
	ines_sword_t   s1 = b1;
	ines_sword_t   s2 = b2;
	ines_int_t     i1 = (int)(ines_sbyte_t)s1;



	len = _snprintf(out_str, len, "call c dll: ines_test(n=%d, fv=%g).\n b1=%u, b2=%d, s1=%d, s2=%d, i1=%d, ", n, fv, b1, b2, s1, s2, i1);
	return len;
}



DLLEXPORT  int ines_init(int is_ntsc, const char*  rom_file, const char* ram_file)
{
	g_is_ntsc = is_ntsc;
	ines_set_log_stamp_func(get_cycles);

	INES_LOG(LOG_NTY, MOD_SYS, ISTR("=========================================================\n"));
#ifdef _DEBUG
	INES_LOG(LOG_NTY, MOD_SYS, ISTR("iNES v1.0(Debug), Build Time ")ISTR(__TIMESTAMP__)ISTR(", Initializing...\n"));
#else
	INES_LOG(LOG_NTY, MOD_SYS, ISTR("iNES v1.0, Build Time ")ISTR(__TIMESTAMP__)ISTR(", Initializing...\n"));
#endif

	ines_host_init(&g_host, g_is_ntsc);

	if( ! ines_host_load_rom(&g_host,rom_file, ram_file ) )
	{
		ines_host_free(&g_host);
		return -1;
	}


	ines_host_reset(&g_host);

	return 0;
}

DLLEXPORT  int ines_load(const char* save_file)
{
	if(g_host.status != NES_STATUS_RUNNING)
		return -5;
	return 0;
}

DLLEXPORT  int ines_save(const char* save_file)
{
	if(g_host.status != NES_STATUS_RUNNING)
		return -5;
	return 0;
}

DLLEXPORT  void ines_reset()
{
	ines_host_reset(&g_host);
}

extern ines_int_t  nes_cpu_trace_ops;


DLLEXPORT  int ines_frame(ines_running_context_t*  context, ines_byte_t* render_buffer, ines_byte_t* audio_buffer, int* audio_len)
{
	if(context == NULL)
		return -1;
	if(g_host.status != NES_STATUS_RUNNING)
		return -5;
	nes_cpu_trace_ops = context->cpu_trace_ops;
	ines_set_log_level(context->log_level);
	ines_apu_setoutbuffer(&g_host.apu, audio_buffer, *audio_len, context->audio_volume);

	ines_joypad_update_bits(&g_host.joypad, context->main_key_state, context->second_key_state);

	ines_host_doframe(&g_host, render_buffer);

	// for test
	// memset (render_buffer, 15, 25600);

	*audio_len = ines_apu_getoutlen(&g_host.apu);
	// check save SRAM
	// ines_host_save_sram(&host, szRAMFilePath);

	return 0;
}



DLLEXPORT  void ines_fini()
{
	ines_host_free(&g_host);
	INES_LOG(LOG_NTY, MOD_SYS, ISTR("iNES Shutdown OK.\n"));

}

#endif  // 0 }}} 
