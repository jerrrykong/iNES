
#include "nes.h"
#include "joypad.h"
#include "../comm/log.h"


void ines_joypad_init(ines_joypad_t* p_joypad)
{
	if(!p_joypad)
		return;

	memset(p_joypad, 0, sizeof(*p_joypad));
}


void ines_joypad_free(ines_joypad_t* p_joypad)
{

}


void ines_joypad_reset(ines_joypad_t* p_joypad)
{
	ines_joypad_init(p_joypad);
}



void ines_joypad_input_brush(ines_joypad_t* p_joypad, ines_byte_t  brush)
{
	if((brush & 0x01))
	{
		if(!p_joypad->input_brush)
		{
			p_joypad->input_brush = ines_true;
		}
	}
	else
	{
		if(p_joypad->input_brush)
		{
			p_joypad->input_brush = ines_false;
			p_joypad->shift_num[0] = p_joypad->shift_num[1] = 0;
		}
	}
}



void ines_joypad_update_bits(ines_joypad_t* p_joypad, ines_int_t man_key_state, ines_int_t second_key_state)
{
	p_joypad->joypad_bits[0] = man_key_state | 0x010000;
	p_joypad->joypad_bits[1] = second_key_state;
	/*
	p_joypad->flash_count++;
	if(p_joypad->flash_count >= 3) //
	{
		p_joypad->flash_count = 0;
		p_joypad->flash_switch = p_joypad->flash_switch ? 0 : 1;
	}
	p_joypad->joypad_bits[0] = man_key_state;
	p_joypad->joypad_bits[1] = second_key_state;
	if(p_joypad->flash_switch == 0)
	{
		UPDATE_BIT_WITH_KEY(p_joypad->joypad_bits[0], JOYPAD_KEY_A, 'S');  // 连发A
		UPDATE_BIT_WITH_KEY(p_joypad->joypad_bits[0], JOYPAD_KEY_B, 'A');  // 连发B
	}
	UPDATE_BIT_WITH_KEY(p_joypad->joypad_bits[0], JOYPAD_KEY_A, 'X');
	UPDATE_BIT_WITH_KEY(p_joypad->joypad_bits[0], JOYPAD_KEY_B, 'Z');
	UPDATE_BIT_WITH_KEY(p_joypad->joypad_bits[0], JOYPAD_KEY_SELECT, VK_RSHIFT);
	UPDATE_BIT_WITH_KEY(p_joypad->joypad_bits[0], JOYPAD_KEY_START, VK_RETURN);
	UPDATE_BIT_WITH_KEY(p_joypad->joypad_bits[0], JOYPAD_KEY_UP, VK_UP);
	UPDATE_BIT_WITH_KEY(p_joypad->joypad_bits[0], JOYPAD_KEY_DOWN, VK_DOWN);
	UPDATE_BIT_WITH_KEY(p_joypad->joypad_bits[0], JOYPAD_KEY_LEFT, VK_LEFT);
	UPDATE_BIT_WITH_KEY(p_joypad->joypad_bits[0], JOYPAD_KEY_RIGHT, VK_RIGHT);

	p_joypad->joypad_bits[0] |= 0x010000;

	p_joypad->joypad_bits[1] = 0x000000;
	*/
}

ines_byte_t  ines_joypad_read(ines_joypad_t* p_joypad, ines_byte_t index)
{
	ines_byte_t  bt = (p_joypad->joypad_bits[index] >> p_joypad->shift_num[index]) & 0x01;
	p_joypad->shift_num[index] = (p_joypad->shift_num[index] + 1) % 24;
	return bt;
}




ines_int_t  ines_joypad_save_state(ines_joypad_t* p_joypad, FILE* fSave)
{
	return 0;
}

ines_int_t  ines_joypad_load_state(ines_joypad_t* p_joypad, FILE* fSave)
{
	return 0;
}
