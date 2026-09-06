#ifndef __JOYPAD_H__
#define __JOYPAD_H__


#include "../comm/idef.h"


#ifdef __cplusplus
extern "C"
{
#endif

#define JOYPAD_NUM        2



#define JOYPAD_KEY_A        0x01
#define JOYPAD_KEY_B        0x02
#define JOYPAD_KEY_SELECT   0x04
#define JOYPAD_KEY_START    0x08
#define JOYPAD_KEY_UP       0x10
#define JOYPAD_KEY_DOWN     0x20
#define JOYPAD_KEY_LEFT     0x40
#define JOYPAD_KEY_RIGHT    0x80

struct _ines_joypad_;
typedef struct _ines_joypad_  ines_joypad_t;

struct _ines_joypad_ {
	ines_dword_t    joypad_bits[JOYPAD_NUM];
	ines_byte_t     shift_num[JOYPAD_NUM];
	ines_bool_t     input_brush;
	//ines_int_t      flash_count; // 用于连发频率控制
	//ines_bool_t     flash_switch;
};



void ines_joypad_init(ines_joypad_t* p_joypad);
void ines_joypad_reset(ines_joypad_t* p_joypad);
void ines_joypad_free(ines_joypad_t* p_joypad);

void ines_joypad_input_brush(ines_joypad_t* p_joypad, ines_byte_t  brush);
void ines_joypad_update_bits(ines_joypad_t* p_joypad, ines_int_t man_key_state, ines_int_t second_key_state);
ines_byte_t ines_joypad_read(ines_joypad_t* p_joypad, ines_byte_t index);


ines_int_t  ines_joypad_save_state(ines_joypad_t* p_joypad, FILE* fSave);
ines_int_t  ines_joypad_load_state(ines_joypad_t* p_joypad, FILE* fSave);

#ifdef __cplusplus
};
#endif

#endif
