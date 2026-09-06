
#include "nes.h"
#include "cpu.h"
#include "../comm/log.h"

#if 1

#define NES6502_TESTOPS
#define NES6502_LOCAL_OPM  

ines_int_t  nes_cpu_trace_ops = 0;


#ifdef NES6502_TESTOPS

ines_cstr_t dasm_text[] = {
	ISTR("BRK"), 	ISTR("ORA ($nn, X)"), 	ISTR("JAM"), 	ISTR("SLO ($nn, X)"), 	ISTR("NOP $nn"), 	ISTR("ORA $nn"), 	ISTR("ASL $nn"), 	ISTR("SLO $nn"), 	ISTR("PHP"), 	ISTR("ORA #$nn"), 	ISTR("ASL A"), 	ISTR("ANC #$nn"), 	ISTR("NOP $nnnn"), 	ISTR("ORA $nnnn"), 	ISTR("ASL $nnnn"), 	ISTR("SLO $nnnn"), 
	ISTR("BPL $ss"), 	ISTR("ORA ($nn), Y"), 	ISTR("JAM"), 	ISTR("SLO ($nn), Y"), 	ISTR("NOP $nn, X"), 	ISTR("ORA $nn, X"), 	ISTR("ASL $nn, X"), 	ISTR("SLO $nn, X"), 	ISTR("CLC"), 	ISTR("ORA $nnnn, Y"), 	ISTR("NOP"), 	ISTR("SLO $nnnn, Y"), 	ISTR("NOP $nnnn, X"), 	ISTR("ORA $nnnn, X"), 	ISTR("ASL $nnnn, X"), 	ISTR("SLO $nnnn, X"), 
	ISTR("JSR $nnnn"), 	ISTR("AND ($nn, X)"), 	ISTR("JAM"), 	ISTR("RLA ($nn, X)"), 	ISTR("BIT $nn"), 	ISTR("AND $nn"), 	ISTR("ROL $nn"), 	ISTR("RLA $nn"), 	ISTR("PLP"), 	ISTR("AND #$nn"), 	ISTR("ROL A"), 	ISTR("ANC #$nn"), 	ISTR("BIT $nnnn"), 	ISTR("AND $nnnn"), 	ISTR("ROL $nnnn"), 	ISTR("RLA $nnnn"), 
	ISTR("BMI $ss"), 	ISTR("AND ($nn), Y"), 	ISTR("JAM"), 	ISTR("RLA ($nn), Y"), 	ISTR("NOP"), 	ISTR("AND $nn, X"), 	ISTR("ROL $nn, X"), 	ISTR("RLA $nn, X"), 	ISTR("SEC"), 	ISTR("AND $nnnn, Y"), 	ISTR("NOP"), 	ISTR("RLA $nnnn, Y"), 	ISTR("NOP $nnnn, X"), 	ISTR("AND $nnnn, X"), 	ISTR("ROL $nnnn, X"), 	ISTR("RLA $nnnn, X"), 
	ISTR("RTI"), 	ISTR("EOR ($nn, X)"), 	ISTR("JAM"), 	ISTR("SRE ($nn, X)"), 	ISTR("NOP $nn"), 	ISTR("EOR $nn"), 	ISTR("LSR $nn"), 	ISTR("SRE $nn"), 	ISTR("PHA"), 	ISTR("EOR #$nn"), 	ISTR("LSR A"), 	ISTR("ASR #$nn"), 	ISTR("JMP $nnnn"), 	ISTR("EOR $nnnn"), 	ISTR("LSR $nnnn"), 	ISTR("SRE $nnnn"), 
	ISTR("BVC $ss"), 	ISTR("EOR ($nn), Y"), 	ISTR("JAM"), 	ISTR("SRE ($nn), Y"), 	ISTR("NOP $nn, X"), 	ISTR("EOR $nn, X"), 	ISTR("LSR $nn, X"), 	ISTR("SRE $nn, X"), 	ISTR("CLI"), 	ISTR("EOR $nnnn, Y"), 	ISTR("NOP"), 	ISTR("SRE $nnnn, Y"), 	ISTR("NOP $nnnn, X"), 	ISTR("EOR $nnnn, X"), 	ISTR("LSR $nnnn, X"), 	ISTR("SRE $nnnn, X"), 
	ISTR("RTS"), 	ISTR("ADC ($nn, X)"), 	ISTR("JAM"), 	ISTR("RRA ($nn, X)"), 	ISTR("NOP $nn"), 	ISTR("ADC $nn"), 	ISTR("ROR $nn"), 	ISTR("RRA $nn"), 	ISTR("PLA"), 	ISTR("ADC #$nn"), 	ISTR("ROR A"), 	ISTR("ARR #$nn"), 	ISTR("JMP ($nnnn)"), 	ISTR("ADC $nnnn"), 	ISTR("ROR $nnnn"), 	ISTR("RRA $nnnn"), 
	ISTR("BVS $ss"), 	ISTR("ADC ($nn), Y"), 	ISTR("JAM"), 	ISTR("SRE ($nn), Y"), 	ISTR("NOP $nn, X"), 	ISTR("ADC $nn, X"), 	ISTR("ROR $nn, X"), 	ISTR("RRA $nn, X"), 	ISTR("SEI"), 	ISTR("ADC $nnnn, Y"), 	ISTR("NOP"), 	ISTR("RRA $nnnn, Y"), 	ISTR("NOP $nnnn, X"), 	ISTR("ADC $nnnn, X"), 	ISTR("ROR $nnnn, X"), 	ISTR("RRA $nnnn, X"), 
	ISTR("NOP #$nn"), 	ISTR("STA ($nn, X)"), 	ISTR("NOP #$nn"), 	ISTR("SAX ($nn, X)"), 	ISTR("STY $nn"), 	ISTR("STA $nn"), 	ISTR("STX $nn"), 	ISTR("SAX $nn"), 	ISTR("DEY"), 	ISTR("NOP #$nn"), 	ISTR("TXA"), 	ISTR("ANE #$nn"), 	ISTR("STY $nnnn"), 	ISTR("STA $nnnn"), 	ISTR("STX $nnnn"), 	ISTR("SAX $nnnn"), 
	ISTR("BCC $ss"), 	ISTR("STA ($nn), Y"), 	ISTR("JAM"), 	ISTR("SHA ($nn), Y"), 	ISTR("STY $nn, X"), 	ISTR("STA $nn, X"), 	ISTR("STX $nn, Y"), 	ISTR("SAX $nn, Y"), 	ISTR("TYA"), 	ISTR("STA $nnnn, Y"), 	ISTR("TXS"), 	ISTR("SHS $nnnn, Y"), 	ISTR("SHY $nnnn, X"), 	ISTR("STA $nnnn, X"), 	ISTR("SHX $nnnn, Y"), 	ISTR("SHA $nnnn, Y"), 
	ISTR("LDY #$nn"), 	ISTR("LDA ($nn, X)"), 	ISTR("LDX #$nn"), 	ISTR("LAX ($nn, X)"), 	ISTR("LDY $nn"), 	ISTR("LDA $nn"), 	ISTR("LDX $nn"), 	ISTR("LAX $nn"), 	ISTR("TAY"), 	ISTR("LDA #$nn"), 	ISTR("TAX"), 	ISTR("LXA #$nn"), 	ISTR("LDY $nnnn"), 	ISTR("LDA $nnnn"), 	ISTR("LDX $nnnn"), 	ISTR("LAX $nnnn"), 
	ISTR("BCS $ss"), 	ISTR("LDA ($nn), Y"), 	ISTR("JAM"), 	ISTR("LAX ($nn), Y"), 	ISTR("LDY $nn, X"), 	ISTR("LDA $nn, X"), 	ISTR("LDX $nn, Y"), 	ISTR("LAX $nn, Y"), 	ISTR("CLV"), 	ISTR("LDA $nnnn, Y"), 	ISTR("TSX"), 	ISTR("LAS $nnnn, Y"), 	ISTR("LDY $nnnn, X"), 	ISTR("LDA $nnnn, X"), 	ISTR("LDX $nnnn, Y"), 	ISTR("LAX $nnnn, Y"), 
	ISTR("CPY #$nn"), 	ISTR("CMP ($nn, X)"), 	ISTR("NOP #$nn"), 	ISTR("DCP ($nn, X)"), 	ISTR("CPY $nn"), 	ISTR("CMP $nn"), 	ISTR("DEC $nn"), 	ISTR("DCP $nn"), 	ISTR("INY"), 	ISTR("CMP #$nn"), 	ISTR("DEX"), 	ISTR("SBX #$nn"), 	ISTR("CPY $nnnn"), 	ISTR("CMP $nnnn"), 	ISTR("DEC $nnnn"), 	ISTR("DCP $nnnn"), 
	ISTR("BNE $ss"), 	ISTR("CMP ($nn), Y"), 	ISTR("JAM"), 	ISTR("DCP ($nn), Y"), 	ISTR("NOP $nn, X"), 	ISTR("CMP $nn, X"), 	ISTR("DEC $nn, X"), 	ISTR("DCP $nn, X"), 	ISTR("CLD"), 	ISTR("CMP $nnnn, Y"), 	ISTR("NOP"), 	ISTR("DCP $nnnn, Y"), 	ISTR("NOP $nnnn, X"), 	ISTR("CMP $nnnn, X"), 	ISTR("DEC $nnnn, X"), 	ISTR("DCP $nnnn, X"), 
	ISTR("CPX #$nn"), 	ISTR("SBC ($nn, X)"), 	ISTR("NOP #$nn"), 	ISTR("ISB ($nn, X)"), 	ISTR("CPX $nn"), 	ISTR("SBC $nn"), 	ISTR("INC $nn"), 	ISTR("ISB $nn"), 	ISTR("INX"), 	ISTR("SBC #$nn"), 	ISTR("NOP"), 	ISTR("USBC #$nn"), 	ISTR("CPX $nnnn"), 	ISTR("SBC $nnnn"), 	ISTR("INC $nnnn"), 	ISTR("ISB $nnnn"), 
	ISTR("BEQ $ss"), 	ISTR("SBC ($nn), Y"), 	ISTR("JAM"), 	ISTR("ISB ($nn), Y"), 	ISTR("NOP ($nn, X)"), 	ISTR("SBC $nn, X"), 	ISTR("INC $nn, X"), 	ISTR("ISB $nn, X"), 	ISTR("SED"), 	ISTR("SBC $nnnn, Y"), 	ISTR("NOP"), 	ISTR("ISB $nnnn, Y"), 	ISTR("NOP $nnnn, X"), 	ISTR("SBC $nnnn, X"), 	ISTR("INC $nnnn, X"), 	ISTR("ISB $nnnn, X"), 
};

void disasm(ines_cpu_t* p_cpu, ines_word_t  PC, ines_byte_t  A, ines_byte_t  X, ines_byte_t  Y, ines_byte_t  P, ines_byte_t  SP, ines_int64_t Cycles)
{
	ines_byte_t    op;
	ines_word_t    opd;
	ines_char_t    strop[128]; 
	ines_char_t    strcode[64]; 
	ines_size_t    len;

	ines_cstr_t    strfmt;
	ines_cstr_t    rep;


	op = ines_host_read(cpu2host(p_cpu), PC);
	strfmt = dasm_text[op];


	if(NULL != (rep = ines_strstr(strfmt, ISTR("$nnnn"))))
	{
		opd = (ines_word_t)ines_host_read(cpu2host(p_cpu), PC+1) | 
			( (ines_word_t)ines_host_read(cpu2host(p_cpu), PC+2) << 8 );

		ines_sprintf(strcode, ISTR("%04X: %02X %02X %02X  "), PC, op, opd&0xff, opd>>8);
		len = rep-strfmt;
		ines_strncpy(strop, strfmt, len);
		strfmt += len + 5;
		len += ines_sprintf(strop+len, ISTR("$%04X"), opd);
		ines_strcpy(strop+len, strfmt);

	}
	else if(NULL != (rep = ines_strstr(strfmt, ISTR("$nn"))))
	{
		opd = (ines_word_t)ines_host_read(cpu2host(p_cpu), PC+1);

		ines_sprintf(strcode, ISTR("%04X: %02X %02X     "), PC, op, opd&0xff);

		len = rep-strfmt;
		ines_strncpy(strop, strfmt, len);
		strfmt += len + 3;
		len += ines_sprintf(strop+len, ISTR("$%02X"), opd);
		ines_strcpy(strop+len, strfmt);
	}
	else if(NULL != (rep = ines_strstr(strfmt, ISTR("$ss"))))
	{
		opd = (ines_word_t)ines_host_read(cpu2host(p_cpu), PC+1);


		ines_sprintf(strcode, ISTR("%04X: %02X %02X     "), PC, op, opd&0xff);

		len = rep-strfmt;
		ines_strncpy(strop, strfmt, len);
		strfmt += len + 3;
		len += ines_sprintf(strop+len, ISTR("$%04X"), (ines_word_t)((int)(PC+2) + (int)(ines_sbyte_t)opd));
		ines_strcpy(strop+len, strfmt);
	}
	else
	{
		ines_sprintf(strcode, ISTR("%04X: %02X        "), PC, op);
		ines_strcpy(strop, strfmt);
	}
	INES_LOG(LOG_TRA, MOD_CPU, ISTR(" exec: %s %-16s [ A:%02X X:%02X Y:%02X P:%02X SP:%02X ]\n"),  strcode,strop,
		A, X, Y, P, SP);
}

void cpu_Jam(ines_cpu_t* p_cpu)
{
	p_cpu->jammed = ines_true;
	//p_cpu->reg_PC--;
	INES_LOG(LOG_ERR, MOD_CPU, ISTR("CPU JAM\n") );
}

#endif

#if defined(__GNUC__) && !defined(NES6502_DISASM)
#define  NES6502_JUMPTABLE
#endif /* __GNUC__ */


#ifndef NES6502_LOCAL_OPM  
#define _CPU_   (p_cpu)
#define _PC_    (_CPU_->reg_PC)
#define _X_     (_CPU_->reg_X)
#define _Y_     (_CPU_->reg_Y)
#define _A_     (_CPU_->reg_A)
#define _SP_    (_CPU_->reg_SP)
#define _P_     (_CPU_->reg_P)
#define _total_cycles_    (_CPU_->total_cycles)
#define _DMA_cycles_      (_CPU_->DMA_cycles)
#define _JAMMED_          (_CPU_->jammed)
#define _INT_pending_     (_CPU_->INT_pending)
#define _RAM_   (_CPU_->RAM)
#define _STACK_   (_CPU_->RAM+0x100)
#define _TA_     temp_addr     // 保存临时地址的变量 uint32
#define _TBA_    temp_baddr    // 保存临时地址的变量 uint8
#define _TB_     temp_byte     // 保存临时字节的变量 uint8
#define _TD_     temp_data     // 保存临时数值的变量 uint32
#define _TBD_    temp_bdata    // 保存临时数值的变量 uint8

#define DECL_LOCAL_VARS()   \
	ines_byte_t  temp_byte, temp_baddr, temp_bdata;  ines_dword_t   temp_addr, temp_data; 

#define GET_LOCAL_VARS()    /* nothing */
#define SAVE_LOCAL_VARS()  /* nothing */

#else

#define _CPU_   (p_cpu)
#define _PC_    PC
#define _X_     X
#define _Y_     Y
#define _A_     A
#define _SP_    SP
#define _P_     P
#define _total_cycles_    (_CPU_->total_cycles)
#define _DMA_cycles_      (_CPU_->DMA_cycles)
#define _JAMMED_          (_CPU_->jammed)
#define _INT_pending_     (_CPU_->INT_pending)
#define _apu_next_irq_    (_CPU_->apu_next_irq)
#define _RAM_     (RAM)
#define _STACK_   (STACK)
#define _TA_     temp_addr     // 保存临时地址的变量 uint32
#define _TBA_    temp_baddr    // 保存临时地址的变量 uint8
#define _TB_     temp_byte     // 保存临时字节的变量 uint8
#define _TD_     temp_data     // 保存临时数值的变量 uint32
#define _TBD_    temp_bdata    // 保存临时数值的变量 uint8

#define DECL_LOCAL_VARS()   \
	ines_byte_t  temp_byte, temp_baddr, temp_bdata;  ines_dword_t   temp_addr, temp_data; \
	ines_byte_t  A, X, Y, SP, P, /* *RAM */ *STACK; ines_dword_t PC; /*ines_int64_t total_cycles*/;



#define GET_LOCAL_VARS() \
{ \
 A = (_CPU_)->reg_A; \
 X = (_CPU_)->reg_X; \
 Y = (_CPU_)->reg_Y; \
 SP = (_CPU_)->reg_SP; \
 P = (_CPU_)->reg_P; \
 PC = (_CPU_)->reg_PC; \
 /*RAM = (_CPU_)->RAM;*/ \
 STACK = (_CPU_)->RAM+0x100; \
 /*total_cycles = (_CPU_)->total_cycles;*/ \
}

#define SAVE_LOCAL_VARS() \
{ \
  (_CPU_)->reg_A = A; \
  (_CPU_)->reg_X = X; \
  (_CPU_)->reg_Y = Y; \
  (_CPU_)->reg_SP = SP; \
  (_CPU_)->reg_P = P; \
  (_CPU_)->reg_PC = PC; \
  /*(_CPU_)->total_cycles = total_cycles;*/ \
}

#endif


#define CHECK_APU_IRQ(x)  \
{ \
	if(_apu_next_irq_ > 0) { \
		if( (_apu_next_irq_ -= (x) ) <= 0) { \
			ines_apu_flush_run(&cpu2host(_CPU_)->apu); \
		} \
	} \
}

#define  ADD_CYCLES(x) \
{ \
	remaining_cycles -= (x); \
	_total_cycles_ += (x); \
	CHECK_APU_IRQ(x); \
}

/*
** Check to see if an index reg addition overflowed to next page
*/
#define PAGE_CROSS_CHECK(addr, reg) \
{ \
	if ((reg) > (ines_byte_t) (addr)) \
	ADD_CYCLES(1); \
}

#define EMPRD(val)  /* empty */

/* for read/write memory*/
#ifdef  NES6502_TESTOPS
#define ZP_READBYTE(addr)             read_byte(_CPU_, (addr))
#define ZP_WRITEBYTE(addr, val)       write_byte(_CPU_, (addr), (val))
#else
#define ZP_READBYTE(addr)             (_RAM_[(ines_byte_t)(addr)])
#define ZP_WRITEBYTE(addr, val)       (_RAM_[(ines_byte_t)(addr)] = (ines_byte_t)(val))
#endif
#define ZP_READWORD(addr)             ( (ines_word_t)ZP_READBYTE(addr) | ((ines_word_t)ZP_READBYTE((addr)+1)<<8 ) )

#define MEM_READBYTE(addr)             read_byte(_CPU_, (addr))
#define MEM_READWORD(addr)             read_word(_CPU_, (addr))
#define MEM_WRITEBYTE(addr, val)       write_byte(_CPU_, (addr), (val))


/*
** Addressing mode macros
*/

/* Immediate */
#define IMMBYTE(val) \
{ \
	(val) = MEM_READBYTE(_PC_++); \
}

/* Absolute */
#define ABSADDR(addr) \
{ \
	(addr) = MEM_READWORD(_PC_); \
	_PC_ += 2; \
}

#define ABSRD(addr, val) \
{ \
	ABSADDR(addr); \
	(val) = MEM_READBYTE(addr); \
}


#define ABSBYTE(val) \
{ \
	ABSRD(_TD_, (val)); \
}

/* Absolute indexed X */
#define ABSIXADDR(addr) \
{ \
	ABSADDR(addr); \
	(addr) = ( (addr) + _X_) & 0xFFFF; \
	PAGE_CROSS_CHECK( (addr), _X_); \
}

#define ABSIXRD(addr, val) \
{ \
	ABSIXADDR(addr); \
	val = MEM_READBYTE(addr); \
}

#define ABSIXBYTE(val) \
{ \
	ABSIXRD(_TD_, (val)); \
}

/* Absolute indexed Y */
#define ABSIYADDR(addr) \
{ \
	ABSADDR(addr); \
	(addr) = ((addr) + _Y_) & 0xFFFF; \
	PAGE_CROSS_CHECK((addr), _Y_); \
}

#define ABSIYRD(addr, val) \
{ \
	ABSIYADDR(addr); \
	(val) = MEM_READBYTE(addr); \
}

#define ABSIYBYTE(val) \
{ \
	ABSIYRD(_TD_, (val)); \
}

/* Zero-page */
#define ZPADDR(addr) \
{ \
	IMMBYTE(addr); \
}


#define ZPRD(addr, val) \
{ \
	ZPADDR(addr); \
	(val) = ZP_READBYTE(addr); \
}


#define ZPBYTE(val) \
{ \
	ZPRD(_TBD_, (val)); \
}

/* Zero-page indexed X */
#define ZPIXADDR(addr) \
{ \
	ZPADDR(addr); \
	(addr) += _X_; \
}

#define ZPIXRD(addr, val) \
{ \
	ZPIXADDR(addr); \
	(val) = ZP_READBYTE(addr); \
}

#define ZPIXBYTE(val) \
{ \
	ZPIXRD(_TBD_, (val)); \
}

/* Zero-page indexed Y */
/* Not really an adressing mode, just for LDx/STx */
#define ZPIYADDR(addr) \
{ \
	ZPADDR(addr); \
	(addr) += _Y_; \
}

#define ZPIYBYTE(val) \
{ \
	ZPIYADDR(_TBD_); \
	(val) = ZP_READBYTE(_TBD_); \
}  

/* Indexed indirect */
#define IIXADDR(addr) \
{ \
	IMMBYTE(_TBD_); \
	_TBD_ += _X_; \
	(addr) = ZP_READWORD(_TBD_); \
}

#define IIXRD(addr, val) \
{ \
	IIXADDR(addr); \
	(val) = MEM_READBYTE(addr); \
} 

#define IIXBYTE(val) \
{ \
	IIXRD(_TD_, val); \
}

/* Indirect indexed */
#define IIYADDR(addr) \
{ \
	IMMBYTE(_TBD_); \
	(addr) = (ZP_READWORD(_TBD_) + _Y_) & 0xFFFF; \
	PAGE_CROSS_CHECK((addr), _Y_); \
}

#define IIYRD(addr, val) \
{ \
	IIYADDR(addr); \
	(val) = MEM_READBYTE(addr); \
} 

#define IIYBYTE(val) \
{ \
	IIYRD(_TD_, (val)); \
}



/* Stack push/pull */
#define  PUSH(val)             (_STACK_[_SP_--] = (ines_byte_t) (val))
#define  POP()                  (_STACK_[++_SP_])



/*
** flag register helper macros
*/


#define SETFLAG(flag)    { _P_ |= (flag); }
#define CLRFLAG(flag)    { _P_ &= ~(flag); }
#define UPDATEFLAG(flag, b)  { \
	if(b)  { \
	  SETFLAG(flag); } else { \
	  CLRFLAG(flag); } \
} 

#define CHKFLAG(flag)   ((_P_&(flag)) != 0)
#define CHKC()  CHKFLAG(C_FLAG)
#define CHKD()  CHKFLAG(D_FLAG)
#define CHKZ()  CHKFLAG(Z_FLAG)
#define CHKN()  CHKFLAG(N_FLAG)
#define CHKV()  CHKFLAG(V_FLAG)
#define CHKB()  CHKFLAG(B_FLAG)
#define CHKI()  CHKFLAG(I_FLAG)


#define SETNZ(val)   { \
	UPDATEFLAG(N_FLAG, (val) & 0x80);  \
	UPDATEFLAG(Z_FLAG, (ines_byte_t)(val) == 0);  }

#define TSETN(val)  UPDATEFLAG(N_FLAG, val)
#define TSETZ(val)  UPDATEFLAG(Z_FLAG, val)
#define TSETC(val)  UPDATEFLAG(C_FLAG, val)
#define TSETD(val)  UPDATEFLAG(D_FLAG, val)
#define TSETI(val)  UPDATEFLAG(I_FLAG, val)
#define TSETV(val)  UPDATEFLAG(V_FLAG, val)
#define TSETB(val)  UPDATEFLAG(B_FLAG, val)


/* For BCC, BCS, BEQ, BMI, BNE, BPL, BVC, BVS */
#define CJMP(cond) \
{ \
	if (cond) { \
	  IMMBYTE(_TBD_); \
	  if (((ines_sbyte_t) _TBD_ + (_PC_ & 0x00FF)) & 0x100) { \
	    ADD_CYCLES(1); } \
	  ADD_CYCLES(3); \
	  _PC_ = (ines_word_t)((ines_sword_t)_PC_ +  ((ines_sbyte_t) _TBD_)); } else { \
	  _PC_++; \
	  ADD_CYCLES(2); } \
}

#define JMP(addr) \
{ \
	_PC_ = read_word(_CPU_, (addr)); \
}

/*
** Interrupt macros
*/
#define NMI_PROC() \
{ \
	PUSH(_PC_ >> 8);   \
	PUSH(_PC_ & 0xFF); \
	CLRFLAG(B_FLAG);   \
	PUSH(_P_);         \
	SETFLAG(I_FLAG);   \
	JMP(NMI_VECTOR);   \
}

#define IRQ_PROC() \
{ \
	PUSH(_PC_ >> 8); \
	PUSH(_PC_ & 0xFF); \
	CLRFLAG(B_FLAG); \
	PUSH(_P_); \
	SETFLAG(I_FLAG); \
	JMP(IRQ_VECTOR); \
}

#define NMI() \
{ \
	NMI_PROC(); \
	ADD_CYCLES(INT_CYCLES); \
}
#define IRQ() \
{ \
	IRQ_PROC(); \
	ADD_CYCLES(INT_CYCLES); \
}

/*
** Instruction macros
*/
/* Warning! NES CPU has no decimal mode, so by default this does no BCD! */
#ifdef NES6502_DECIMAL
#define ADC(cycles, read_func) \
{ \
	read_func(_TB_); \
	if (CHKFLAG(D_FLAG)) { \
	  _TD_ = (_A_ & 0x0F) + (_TB_ & 0x0F) + (CHKC() ? 1 : 0); \
	  if (_TD_ >= 10) { _TD_ = (_TD_ - 10) | 0x10; } \
	  _TD_ += (_A_ & 0xF0) + (_TB_ & 0xF0); \
	  TSETZ(!((A + _TB_ + (CHKC() ? 1 : 0)) & 0xFF)); \
	  TSETN(_TD_ & 0x80); \
	  TSETV((~(_A_ ^ _TB_)) & (_A_ ^ _TD_) & 0x80); \
	  if (_TD_ > 0x9F) { _TD_ += 0x60; } \
	  TSETC((_TD_ > 0xFF) ); \
	  _A_ = (ines_byte_t) _TD_; } else { \
	  _TD_ = _A_ + _TB_ + (CHKC() ? 1 : 0); \
	  TSETC( (_TD_ > 0xFF) ); \
	  TSETV( (~(_A_ ^ _TB_)) & (_A_ ^ _TD_) & 0x80 ); \
	  _A_ = (ines_byte_t) _TD_; \
	  SETNZ(_A_); }\
	ADD_CYCLES(cycles); \
}
#else
#define ADC(cycles, read_func) \
{ \
	read_func(_TB_); \
	_TD_ = _A_ + _TB_ + (CHKC() ? 1 : 0); \
	TSETC( _TD_ > 0xFF ); \
	TSETV( (~(_A_ ^ _TB_)) & (_A_ ^ _TD_) & 0x80 ); \
	_A_ = (ines_byte_t) _TD_; \
	SETNZ(_A_); \
	ADD_CYCLES(cycles); \
}
#endif /* NES6502_DECIMAL */
/* undocumented */
#define ANC(cycles, read_func) \
{ \
	read_func(_TB_); \
	_A_ &= _TB_; \
	SETNZ(_A_); \
	TSETC(CHKN()); \
	ADD_CYCLES(cycles); \
}

#define AND(cycles, read_func) \
{ \
	read_func(_TB_); \
	_A_ &= _TB_; \
	SETNZ(_A_); \
	ADD_CYCLES(cycles); \
}

/* undocumented */
#define ANE(cycles, read_func) \
{ \
	read_func(_TB_); \
	_A_ = (_A_ | 0xEE) & _X_ & _TB_; \
	SETNZ(_A_); \
	ADD_CYCLES(cycles); \
}

/* undocumented */
#ifdef NES6502_DECIMAL
#define ARR(cycles, read_func) \
{ \
	read_func(_TB_); \
	_TB_ &= _A_; \
	if (CHKD()) { \
	  _TD_ = (_TB_ >> 1) | (CHKC() ? 0x80 : 0); \
	  SETNZ(_TD_); \
	  TSETV( (_TD_ ^ _TB_) & 0x40 ); \
	  if (((_TB_ & 0x0F) + (_TB_ & 0x01)) > 5) \
	    _TA_ = (_TD_ & 0xF0) | ((_TD_ + 0x6) & 0x0F); \
	  if (((_TB_ & 0xF0) + (_TB_ & 0x10)) > 0x50) { \
	    _TD_ = (_TD_ & 0x0F) | ((_TD_ + 0x60) & 0xF0); \
	    SETFLAG(C_FLAG); } else { \
	    CLRFLAG(C_FLAG); } \
	  _A_ = (ines_byte_t) _TD_; } else { \
	  _A_ = (_TB_ >> 1) | ( CHKC() ? 0x80 : 0); \
	  SETNZ(_A_); \
	  TSETC(_A_ & 0x40); \
	  TSETV( ((_A_ >> 6) ^ (_A_ >> 5)) & 1 ); }\
	ADD_CYCLES(cycles); \
}
#else
#define ARR(cycles, read_func) \
{ \
	read_func(_TB_); \
	_TB_ &= _A_; \
	_A_ = (_TB_ >> 1) | ( CHKC() ? 0x80 : 0); \
	SETNZ(_A_); \
	TSETC(_A_ & 0x40); \
	TSETV( ((_A_ >> 6) ^ (_A_ >> 5)) & 1 ); \
	ADD_CYCLES(cycles); \
}
#endif /* NES6502_DECIMAL */

#define ASL(cycles, read_func, write_func, addr) \
{ \
	read_func(addr, _TB_); \
	TSETC(_TB_ & 0x80); \
	_TB_ <<= 1; \
	write_func(addr, _TB_); \
	SETNZ((ines_byte_t)_TB_); \
	ADD_CYCLES(cycles); \
}

#define ASL_A() \
{ \
	TSETC(_A_ & 0x80); \
	_A_ <<= 1; \
	SETNZ(_A_); \
	ADD_CYCLES(2); \
}

/* undocumented */
#define ASR(cycles, read_func) \
{ \
	read_func(_TB_); \
	_TB_ &= _A_; \
	TSETC(_TB_ & 0x01); \
	_A_ = _TB_ >> 1; \
	SETNZ(_A_); \
	ADD_CYCLES(cycles); \
}

#define BCC() \
{ \
	CJMP(!CHKC()); \
}

#define BCS() \
{ \
	CJMP(CHKC()); \
}

#define BEQ() \
{ \
	CJMP(CHKZ()); \
}

#define BIT(cycles, read_func) \
{ \
	read_func(_TB_); \
	TSETZ(!(_TB_ & _A_)); \
	/* move bit 7/6 of data into N/V flags */ \
	TSETN(_TB_ & 0x80); \
	TSETV(_TB_ & 0x40); \
	ADD_CYCLES(cycles); \
}

#define BMI() \
{ \
	CJMP(CHKN()); \
}

#define BNE() \
{ \
	CJMP(!CHKZ()); \
}

#define BPL() \
{ \
	CJMP(!CHKN()); \
}

/* Software interrupt type thang */
#define BRK() \
{ \
	_PC_++; \
	PUSH(_PC_ >> 8); \
	PUSH(_PC_ & 0xFF); \
	SETFLAG(B_FLAG); \
	PUSH(_P_); \
	SETFLAG(I_FLAG); \
	JMP(IRQ_VECTOR); \
	ADD_CYCLES(7); \
}

#define BVC() \
{ \
	CJMP(!CHKV()); \
}

#define BVS() \
{ \
	CJMP(CHKV()); \
}

#define CLC() \
{ \
	CLRFLAG(C_FLAG); \
	ADD_CYCLES(2); \
}

#define CLD() \
{ \
	CLRFLAG(D_FLAG); \
	ADD_CYCLES(2); \
}

#define CLI() \
{ \
	CLRFLAG(I_FLAG); \
	ADD_CYCLES(2); \
	if (_INT_pending_ && (remaining_cycles > 0)) { \
	 IRQ(); \
	 _INT_pending_ = 0; } \
}

#define CLV() \
{ \
	CLRFLAG(V_FLAG); \
	ADD_CYCLES(2); \
}

#define _COMPARE(reg, value) \
{ \
	_TD_ = (reg) - (value); \
	/* C is clear when data > A */ \
	TSETC(0 == (_TD_ & 0x100)); \
	SETNZ((ines_byte_t) _TD_); \
}

#define CMP(cycles, read_func) \
{ \
	read_func(_TB_); \
	_COMPARE(_A_, _TB_); \
	ADD_CYCLES(cycles); \
}

#define CPX(cycles, read_func) \
{ \
	read_func(_TB_); \
	_COMPARE(_X_, _TB_); \
	ADD_CYCLES(cycles); \
}

#define CPY(cycles, read_func) \
{ \
	read_func(_TB_); \
	_COMPARE(_Y_, _TB_); \
	ADD_CYCLES(cycles); \
}

/* undocumented */
#define DCP(cycles, read_func, write_func, addr) \
{ \
	read_func(addr, _TB_); \
	_TB_--; \
	write_func(addr, _TB_); \
	CMP(cycles, EMPRD); \
}

#define DEC(cycles, read_func, write_func, addr) \
{ \
	read_func(addr, _TB_); \
	_TB_--; \
	write_func(addr, _TB_); \
	SETNZ(_TB_); \
	ADD_CYCLES(cycles); \
}

#define DEX() \
{ \
	_X_--; \
	SETNZ(_X_); \
	ADD_CYCLES(2); \
}

#define DEY() \
{ \
	_Y_--; \
	SETNZ(_Y_); \
	ADD_CYCLES(2); \
}

/* undocumented (double-NOP) */
#define DOP(cycles) \
{ \
	_PC_++; \
	ADD_CYCLES(cycles); \
}

#define EOR(cycles, read_func) \
{ \
	read_func(_TB_); \
	_A_ ^= _TB_; \
	SETNZ(_A_); \
	ADD_CYCLES(cycles); \
}

#define INC(cycles, read_func, write_func, addr) \
{ \
	read_func(addr, _TB_); \
	_TB_++; \
	write_func(addr, _TB_); \
	SETNZ((ines_byte_t)_TB_); \
	ADD_CYCLES(cycles); \
}

#define INX() \
{ \
	_X_++; \
	SETNZ(_X_); \
	ADD_CYCLES(2); \
}

#define INY() \
{ \
	_Y_++; \
	SETNZ(_Y_); \
	ADD_CYCLES(2); \
}

/* undocumented */
#define ISB(cycles, read_func, write_func, addr) \
{ \
	read_func(addr, _TB_); \
	_TB_++; \
	write_func(addr, _TB_); \
	SBC(cycles, EMPRD); \
}

#ifdef NES6502_TESTOPS
#define JAM() \
{ \
	cpu_Jam(_CPU_); \
}
#else /* !NES6502_TESTOPS */
#define JAM() \
{ \
	_PC_--; \
	_JAMMED_ = ines_true; \
	_INT_pending_ = 0; \
	ADD_CYCLES(2); \
}
#endif /* !NES6502_TESTOPS */

#define JMP_INDIRECT() \
{ \
	_TD_ = MEM_READWORD(_PC_); \
	/* bug in crossing page boundaries */ \
	if (0xFF == (_TD_ & 0xFF)) { \
	  _PC_ = (MEM_READBYTE(_TD_ & 0xFF00) << 8) | MEM_READBYTE(_TD_); } else { \
	  JMP(_TD_); } \
	ADD_CYCLES(5); \
}

#define JMP_ABSOLUTE() \
{ \
	JMP(_PC_); \
	ADD_CYCLES(3); \
}

#define JSR() \
{ \
	_PC_++; \
	PUSH(_PC_ >> 8); \
	PUSH(_PC_ & 0xFF); \
	JMP(_PC_ - 1); \
	ADD_CYCLES(6); \
}

/* undocumented */
#define LAS(cycles, read_func) \
{ \
	read_func(_TB_); \
	_A_ = _X_ = _SP_ = (_SP_ & _TB_); \
	SETNZ(_A_); \
	ADD_CYCLES(cycles); \
}

/* undocumented */
#define LAX(cycles, read_func) \
{ \
	read_func(_A_); \
	_X_ = _A_; \
	SETNZ(_A_); \
	ADD_CYCLES(cycles); \
}

#define LDA(cycles, read_func) \
{ \
	read_func(_A_); \
	SETNZ(_A_); \
	ADD_CYCLES(cycles); \
}

#define LDX(cycles, read_func) \
{ \
	read_func(_X_); \
	SETNZ(_X_);\
	ADD_CYCLES(cycles); \
}

#define LDY(cycles, read_func) \
{ \
	read_func(_Y_); \
	SETNZ(_Y_);\
	ADD_CYCLES(cycles); \
}

#define LSR(cycles, read_func, write_func, addr) \
{ \
	read_func(addr, _TB_); \
	TSETC(_TB_ & 0x01); \
	_TB_ >>= 1; \
	write_func(addr, (ines_byte_t)_TB_); \
	SETNZ((ines_byte_t)_TB_); \
	ADD_CYCLES(cycles); \
}

#define LSR_A() \
{ \
	TSETC(_A_ & 0x01); \
	_A_ >>= 1; \
	SETNZ(_A_); \
	ADD_CYCLES(2); \
}

/* undocumented */
#define LXA(cycles, read_func) \
{ \
	read_func(_TB_); \
	_A_ = _X_ = ((_A_ | 0xEE) & _TB_); \
	SETNZ(_A_); \
	ADD_CYCLES(cycles); \
}

#define NOP() \
{ \
	ADD_CYCLES(2); \
}

#define ORA(cycles, read_func) \
{ \
	read_func(_TB_); \
	_A_ |= _TB_; \
	SETNZ(_A_);\
	ADD_CYCLES(cycles); \
}

#define PHA() \
{ \
	PUSH(_A_); \
	ADD_CYCLES(3); \
}

#define PHP() \
{ \
	/* B flag is pushed on stack as well */ \
	PUSH(_P_ | B_FLAG); \
	ADD_CYCLES(3); \
}

#define PLA() \
{ \
	_A_ = POP(); \
	SETNZ(_A_); \
	ADD_CYCLES(4); \
}

#define PLP() \
{ \
	_P_ = POP() | R_FLAG; \
	ADD_CYCLES(4); \
}

/* undocumented */
#define RLA(cycles, read_func, write_func, addr) \
{ \
	read_func(addr, _TB_); \
	if(CHKC()) { \
	  TSETC(_TB_ & 0x80); \
	  _TB_ = (_TB_ << 1) | 1; } else { \
	  TSETC(_TB_ & 0x80); \
	  _TB_ = (_TB_ << 1); } \
	write_func(addr, (ines_byte_t)_TB_); \
	_A_ &= (ines_byte_t)_TB_; \
	SETNZ(_A_); \
	ADD_CYCLES(cycles); \
}

/* 9-bit rotation (carry flag used for rollover) */
#define ROL(cycles, read_func, write_func, addr) \
{ \
	read_func(addr, _TB_); \
	if (CHKC())  { \
	  TSETC(_TB_ & 0x80); \
	  _TB_ = (_TB_ << 1) | 1;  } else { \
  	  TSETC(_TB_ & 0x80); \
	  _TB_ = (_TB_ << 1);  } \
	write_func(addr, (ines_byte_t)_TB_); \
	SETNZ((ines_byte_t)_TB_); \
	ADD_CYCLES(cycles); \
}

#define ROL_A() \
{ \
	if (CHKC()) { \
 	  TSETC(_A_ & 0x80); \
	  _A_ = (_A_ << 1) | 0x1; }  else { \
	  TSETC(_A_ & 0x80); \
	  _A_ <<= 1; } \
	SETNZ(_A_); \
	ADD_CYCLES(2); \
}

#define ROR(cycles, read_func, write_func, addr) \
{ \
	read_func(addr, _TB_); \
	if (CHKC()) { \
  	  TSETC(_TB_ &  0x1); \
	  _TB_= (_TB_ >> 1) | 0x80; } else { \
	  TSETC(_TB_ &  0x1); \
	  _TB_ >>= 1; } \
	write_func(addr, _TB_); \
	SETNZ((ines_byte_t)_TB_); \
	ADD_CYCLES(cycles); \
}

#define ROR_A() \
{ \
	if (CHKC()) { \
	  TSETC(_A_ & 1); \
	  _A_ = (_A_ >> 1) | 0x80; } else { \
	  TSETC(_A_ & 1); \
	  _A_ >>= 1; } \
	SETNZ(_A_); \
	ADD_CYCLES(2); \
}

/* undocumented */
#define RRA(cycles, read_func, write_func, addr) \
{ \
	read_func(addr, _TB_); \
	if (CHKC()) { \
	  TSETC(_TB_ & 1); \
	  _TB_ = (_TB_ >> 1) | 0x80; } else { \
	  TSETC(_TB_ & 1); \
	  _TB_ >>= 1; } \
	write_func(addr, (ines_byte_t)_TB_); \
	ADC(cycles, EMPRD); \
}

#define RTI() \
{ \
	_P_ = POP() | R_FLAG; \
	_PC_ = POP(); \
	_PC_ |= POP() << 8; \
	ADD_CYCLES(6);  }
/* \
	if (!CHKI() && _INT_pending_ && (remaining_cycles > 0)) \
	{ \
	  _INT_pending_ = 0; \
	  IRQ(); \
	} \
}*/

#define RTS() \
{ \
	_PC_ = POP(); \
	_PC_ = (_PC_ | (POP() << 8)) + 1; \
	ADD_CYCLES(6); \
}

/* undocumented */
#define SAX(cycles, read_func, write_func, addr) \
{ \
	read_func(addr); \
	_TB_ = _A_ & _X_; \
	write_func(addr, (ines_byte_t)_TB_); \
	ADD_CYCLES(cycles); \
}

/* Warning! NES CPU has no decimal mode, so by default this does no BCD! */
#ifdef NES6502_DECIMAL
#define SBC(cycles, read_func) \
{ \
	read_func(_TB_); \
	_TD_ = _A_ - _TB_ - (CHKC() ? 0 : 1); \
	if (d_flag) { \
	  ines_byte_t al, ah; \
	  al = (_A_ & 0x0F) - (_TB_ & 0x0F) - (CHKC() ? 0 : 1); \
	  ah = (_A_ >> 4) - (_TB_ >> 4); \
	  if (al & 0x10) { \
	    al -= 6; \
	    ah--; } \
	  if (ah & 0x10) ah -= 6; \
	  TSETC(_TD_ < 0x100); \
	  TSETV(((_A_ ^ _TD_) & 0x80) && ((_A_ ^ _TB_) & 0x80)); \
	  SETNZ((ines_byte_t)_TD_); \
	  _A_ = (ah << 4) | (al & 0x0F); } else { \
	  TSETV(((_A_ ^ _TD_) & 0x80) && ((_A_ ^ _TB_) & 0x80)); \
	  TSETC(_TD_ < 0x100); \
	  _A_ = (ines_byte_t) _TD_; \
	  SETNZ(_A_); } \
	ADD_CYCLES(cycles); \
}
#else
#define SBC(cycles, read_func) \
{ \
	read_func(_TB_); \
	_TD_ = _A_ - _TB_ - (CHKC() ? 0 : 1); \
	TSETV( ((_A_ ^ _TB_) & (_A_ ^ _TD_) & 0x80) ); \
	TSETC( _TD_ < 0x100 ); \
	_A_ = (ines_byte_t) _TD_; \
	SETNZ(_A_); \
	ADD_CYCLES(cycles); \
}
#endif /* NES6502_DECIMAL */

/* undocumented */
#define SBX(cycles, read_func) \
{ \
	read_func(_TB_); \
	_TD_ = (_A_ & _X_) - _TB_; \
	TSETC(_TD_ < 0x100); \
	_X_ = _TD_ & 0xFF; \
	SETNZ(_X_); \
	ADD_CYCLES(cycles); \
}

#define SEC() \
{ \
	SETFLAG(C_FLAG); \
	ADD_CYCLES(2); \
}

#define SED() \
{ \
	SETFLAG(D_FLAG); \
	ADD_CYCLES(2); \
}

#define SEI() \
{ \
	SETFLAG(I_FLAG); \
	ADD_CYCLES(2); \
}

/* undocumented */
#define SHA(cycles, read_func, write_func, addr) \
{ \
	read_func(addr); \
	_TB_ = _A_ & _X_ & ((ines_byte_t) (( (addr) >> 8) + 1)); \
	write_func(addr, _TB_); \
	ADD_CYCLES(cycles); \
}

/* undocumented */
#define SHS(cycles, read_func, write_func, addr) \
{ \
	read_func(addr); \
	_SP_ = _A_ & _X_; \
	_TB_ = _SP_ & ((ines_byte_t) (( (addr) >> 8) + 1)); \
	write_func(addr, _TB_); \
	ADD_CYCLES(cycles); \
}

/* undocumented */
#define SHX(cycles, read_func, write_func, addr) \
{ \
	read_func(addr); \
	_TB_ = _X_ & ((ines_byte_t) (( (addr) >> 8) + 1)); \
	write_func(addr, _TB_); \
	ADD_CYCLES(cycles); \
}

/* undocumented */
#define SHY(cycles, read_func, write_func, addr) \
{ \
	read_func(addr); \
	_TB_ = _Y_ & ((ines_byte_t) (( (addr) >> 8 ) + 1)); \
	write_func(addr, _TB_); \
	ADD_CYCLES(cycles); \
}

/* undocumented */
#define SLO(cycles, read_func, write_func, addr) \
{ \
	read_func(addr, _TB_); \
	TSETC( _TB_ & 0x80); \
	_TB_ <<= 1; \
	write_func(addr, _TB_); \
	_A_ |= _TB_; \
	SETNZ(_A_); \
	ADD_CYCLES(cycles); \
}

/* undocumented */
#define SRE(cycles, read_func, write_func, addr) \
{ \
	read_func(addr, _TB_); \
	TSETC(_TB_ & 1); \
	_TB_ >>= 1; \
	write_func(addr, _TB_); \
	_A_ ^= _TB_; \
	SETNZ(_A_); \
	ADD_CYCLES(cycles); \
}

#define STA(cycles, read_func, write_func, addr) \
{ \
	read_func(addr); \
	write_func(addr, _A_); \
	ADD_CYCLES(cycles); \
}

#define STX(cycles, read_func, write_func, addr) \
{ \
	read_func(addr); \
	write_func(addr, _X_); \
	ADD_CYCLES(cycles); \
}

#define STY(cycles, read_func, write_func, addr) \
{ \
	read_func(addr); \
	write_func(addr, _Y_); \
	ADD_CYCLES(cycles); \
}

#define TAX() \
{ \
	_X_ = _A_; \
	SETNZ(_X_);\
	ADD_CYCLES(2); \
}

#define TAY() \
{ \
	_Y_ = _A_; \
	SETNZ(_Y_);\
	ADD_CYCLES(2); \
}

/* undocumented (triple-NOP) */
#define TOP() \
{ \
	_PC_ += 2; \
	ADD_CYCLES(4); \
}

#define TSX() \
{ \
	_X_ = _SP_; \
	SETNZ(_X_);\
	ADD_CYCLES(2); \
}

#define TXA() \
{ \
	_A_ = _X_; \
	SETNZ(_A_);\
	ADD_CYCLES(2); \
}

#define TXS() \
{ \
	_SP_ = _X_; \
	ADD_CYCLES(2); \
}

#define TYA() \
{ \
	_A_ = _Y_; \
	SETNZ(_A_); \
	ADD_CYCLES(2); \
}




static ines_byte_t read_byte(ines_cpu_t* p_cpu, ines_word_t  addr)
{
	ines_byte_t b = 0;
	ines_int_t  bn = addr>>13;
	switch(bn)
	{
	case 0:  // 0~ 0x1fff
		// 读取CPU的内部RAM   共2K
		b = p_cpu->RAM[addr&0x7ff];
		break;
	case 1:  // low regs
	case 2:  // high regs
	case 3:  /* 0x6000-0x7fff */  /* SRAM  with battery */
		// 向外部总线请求读取
		b = ines_host_read(cpu2host(p_cpu), addr);
		break;
	case 4:  /* 0x8000-0x9fff */  /* PROM Block 0 */
	case 5:  /* 0xa000-0xbfff */  /* PROM Block 0 */
	case 6:  /* 0xc000-0xdfff */  /* PROM Block 1 */
	case 7:  /* 0xe000-0xffff */  /* PROM Block 1 */
		b = p_cpu->mem_bank[bn][addr&0x1fff];
		break;
	}

#ifdef NES6502_TESTOPS
	//INES_LOG(LOG_TRA,MOD_CPU, "cpu read byte addr=%04x. ret=%02x.", addr, b);
#endif
	return b;
}


static void write_byte(ines_cpu_t* p_cpu, ines_word_t  addr, ines_byte_t val)
{
	ines_int_t  bn = addr>>13;
#ifdef NES6502_TESTOPS
	// INES_LOG(LOG_TRA,MOD_CPU, "WRITE_CPU($%04X)=$%02X.\n", addr, val);
#endif
	switch(bn)
	{
	case 0:  // cpu inner ram
		// 写入CPU的内部RAM
		//INES_LOG(LOG_DBG,MOD_CPU, "WRITE_CPU_RAM($%04X)=$%02X.\n", addr, val);
		p_cpu->RAM[addr&0x7ff] = val; 
		break;
	case 1:  // low regs
	case 2:  // high regs
		// 写请求到外部总线
		ines_host_write(cpu2host(p_cpu), addr, val);
		break;
	case 3:  // save ram in game card
	case 4:  /* 0x8000-0x9fff */  /* PROM Block 0 */
	case 5:  /* 0xa000-0xbfff */  /* PROM Block 0 */
	case 6:  /* 0xc000-0xdfff */  /* PROM Block 1 */
	case 7:  /* 0xe000-0xffff */  /* PROM Block 1 */
		if(p_cpu->bank_writeable[bn] == 1)
		{
			//INES_LOG(LOG_DBG,MOD_CPU, "WRITE_CPU_SRAM($%04X)=$%02X.\n", addr, val);
			p_cpu->mem_bank[bn][addr&0x1fff] = val;
			cpu2host(p_cpu)->SRAM_write_flag = 1;  // modified
		}
		else
		{
			// 写请求到外部总线
			ines_host_write(cpu2host(p_cpu), addr, val);
		}
		break;
	}
}


#define read_word(p_cpu, addr)   \
	( (ines_word_t)( read_byte( (p_cpu), (addr) ) | ((ines_word_t)read_byte((p_cpu), (addr)+1) << 8) )  )
#define write_word(p_cpu, addr, val)   \
{ write_byte( (p_cpu), (addr),    (ines_byte_t)(val) );  \
	write_byte( (p_cpu), (addr)+1), (ines_byte_t)( (val) >> 8) ) ); }


// 初始化
void ines_cpu_init(ines_cpu_t* p_cpu)
{
	// nothing
}

// 删除
void ines_cpu_free(ines_cpu_t* p_cpu)
{
	// nothing
}

// 软件复位
void ines_cpu_reset(ines_cpu_t* p_cpu)
{
	if(p_cpu == NULL)
		return;
	p_cpu->reg_A = 0;
	p_cpu->reg_X = 0;
	p_cpu->reg_Y = 0;
	p_cpu->reg_SP = 0xff;
	p_cpu->reg_P = R_FLAG|Z_FLAG|I_FLAG;
	p_cpu->INT_pending = 0;
	p_cpu->burn_cycles = 0; // RESET_CYCLES;// 复位需要6个时钟同期
	p_cpu->jammed = ines_false;
	p_cpu->total_cycles = 0; 
	p_cpu->apu_next_irq = 0;
	p_cpu->reg_PC = read_word(p_cpu, RESET_VECTOR); // 复位的入口
	INES_LOG(LOG_NTY, MOD_CPU, ISTR("CPU Reset to $%04X!\n"), p_cpu->reg_PC);
}


void ines_cpu_IRQ(ines_cpu_t* p_cpu, ines_byte_t irq_mask, ines_bool_t  is_set)
{
	//DECL_LOCAL_VARS();
	if(!_JAMMED_)
	{
		//if(!CHKI())
		{
			//IRQ_PROC();
			//p_cpu->burn_cycles += INT_CYCLES;
			if(is_set)
			{
				if((p_cpu->INT_pending & irq_mask) == 0)
				{
					p_cpu->INT_pending  |= irq_mask;
					INES_LOG(LOG_DBG, MOD_CPU, ISTR("CPU IRQ [$%02x] is set!\n"), irq_mask);
				}
			}
			else
			{
				if((p_cpu->INT_pending & irq_mask) == irq_mask)
				{
					p_cpu->INT_pending  &= ~irq_mask;
					INES_LOG(LOG_DBG, MOD_CPU, ISTR("CPU IRQ [$%02x] is clear!\n"), irq_mask);
				}
			}
			// INES_LOG(LOG_DBG, MOD_CPU, ISTR("CPU IRQ toggled!\n"));
		}
	}
}

void ines_cpu_NMI(ines_cpu_t* p_cpu)
{
	//DECL_LOCAL_VARS();
	if(!_JAMMED_)
	{
		//if(!CHKI())  // 不可屏蔽
		{
			//NMI_PROC();
			//p_cpu->burn_cycles += INT_CYCLES;
			p_cpu->INT_pending  |= NMI_MASK;
			INES_LOG(LOG_DBG, MOD_CPU, ISTR("CPU NMI toggled!\n"));
		}
	}
}



#define  OPCODE_BEGIN(xx)  case 0x##xx:
#define  OPCODE_END        break;

#define OP(xx, op)          OPCODE_BEGIN(xx)   op;   OPCODE_END

ines_int_t  ines_cpu_exec(ines_cpu_t* p_cpu, ines_int_t remaining_cycles)
{
	ines_bool_t     disasm_enable;
	ines_byte_t    op;
	ines_int64_t   old_cycles;
	DECL_LOCAL_VARS();
	GET_LOCAL_VARS();


	disasm_enable = ines_check_level(LOG_TRA) && nes_cpu_trace_ops;

	old_cycles = _total_cycles_;

	if(_JAMMED_) 
		remaining_cycles = 0;
	
	while(remaining_cycles > 0)
	{
		// DMA
		if(p_cpu->burn_cycles > 0)
		{
			if(p_cpu->burn_cycles > remaining_cycles)
			{
				ADD_CYCLES(remaining_cycles);
				p_cpu->burn_cycles -= remaining_cycles;
			}
			else
			{
				ADD_CYCLES(p_cpu->burn_cycles);
				p_cpu->burn_cycles = 0;
			}
		}
		else
		{
#ifdef NES6502_TESTOPS
			if(disasm_enable)
			{
				disasm(_CPU_, _PC_, _A_, _X_, _Y_, _P_, _SP_, _total_cycles_);
			}
#endif
			op = read_byte(_CPU_, _PC_++);
			switch(op)
			{
#if 1  /* 00-0f */
			/* BRK          */  OP(00, BRK());
			/* ORA ($nn, X) */  OP(01, ORA(6, IIXBYTE) );
			/* JAM          */  OP(02, { JAM();  remaining_cycles = 0; }  );
			/* SLO ($nn, X) */  OP(03, SLO(8, IIXRD, MEM_WRITEBYTE, _TA_) );
			/* NOP $nn      */  OP(04, DOP(3) );
			/* ORA $nn      */  OP(05, ORA(3, ZPBYTE) );
			/* ASL $nn      */  OP(06, ASL(5, ZPRD, ZP_WRITEBYTE, _TBA_) );
			/* SLO $nn      */  OP(07, SLO(5, ZPRD, ZP_WRITEBYTE, _TBA_) );
			/* PHP          */  OP(08, PHP() );
			/* ORA #$nn     */  OP(09, ORA(2, IMMBYTE) );
			/* ASL A        */  OP(0A, ASL_A() );
			/* ANC #$nn     */  OP(0B, ANC(2, IMMBYTE) );
			/* NOP $nnnn    */  OP(0C, TOP() );
			/* ORA $nnnn    */  OP(0D, ORA(4, ABSBYTE) );
			/* ASL $nnnn    */  OP(0E, ASL(6, ABSRD, MEM_WRITEBYTE, _TA_) );
			/* SLO $nnnn    */  OP(0F, SLO(6, ABSRD, MEM_WRITEBYTE, _TA_) );
#endif
#if 1 /* 10 ~1f */
			/* BPL $nn      */  OP(10, BPL());
			/* ORA ($nn), Y */  OP(11, ORA(5, IIYBYTE) );
			/* JAM          */  OP(12, { JAM();  remaining_cycles = 0; }  );
			/* SLO ($nn), Y */  OP(13, SLO(8, IIYRD, MEM_WRITEBYTE, _TA_) );
			/* NOP $nn, X   */  OP(14, DOP(4) );
			/* ORA $nn, X   */  OP(15, ORA(4, ZPIXBYTE) );
			/* ASL $nn, X   */  OP(16, ASL(6, ZPIXRD, ZP_WRITEBYTE, _TBA_) );
			/* SLO $nn, X   */  OP(17, SLO(6, ZPIXRD, ZP_WRITEBYTE, _TBA_) );
			/* CLC          */  OP(18, CLC() );
			/* ORA $nnnn, Y */  OP(19, ORA(4, ABSIYBYTE) );
			/* NOP          */  OP(1A, NOP() );
			/* SLO $nnnn, Y */  OP(1B, SLO(7, ABSIYRD, MEM_WRITEBYTE, _TA_) );
			/* NOP $nnnn, X */  OP(1C, TOP() );
			/* ORA $nnnn, X */  OP(1D, ORA(4, ABSIXBYTE) );
			/* ASL $nnnn, X */  OP(1E, ASL(7, ABSIXRD, MEM_WRITEBYTE, _TA_) );
			/* SLO $nnnn, X */  OP(1F, SLO(7, ABSIXRD, MEM_WRITEBYTE, _TA_) );
#endif
#if 1 /* 20 ~2f */
			/* JSR $nnnn    */  OP(20, JSR() );
			/* AND ($nn, X) */  OP(21, AND(6, IIXBYTE) );
			/* JAM          */  OP(22, { JAM();  remaining_cycles = 0; }  );
			/* RLA ($nn, X) */  OP(23, RLA(8, IIXRD, MEM_WRITEBYTE, _TA_) );
			/* BIT $nn      */  OP(24, BIT(3, ZPBYTE) );
			/* AND $nn      */  OP(25, AND(3, ZPBYTE) );
			/* ROL $nn      */  OP(26, ROL(5, ZPRD, ZP_WRITEBYTE, _TBA_) );
			/* RLA $nn      */  OP(27, RLA(5, ZPRD, ZP_WRITEBYTE, _TBA_) );
			/* PLP          */  OP(28, PLP() );
			/* AND #$nn     */  OP(29, AND(2, IMMBYTE) );
			/* ROL A        */  OP(2A, ROL_A() );
			/* ANC #$nn     */  OP(2B, ANC(2, IMMBYTE) );
			/* BIT $nnnn    */  OP(2C, BIT(4, ABSBYTE) );
			/* AND $nnnn    */  OP(2D, AND(4, ABSBYTE) );
			/* ROL $nnnn    */  OP(2E, ROL(6, ABSRD, MEM_WRITEBYTE, _TA_) );
			/* RLA $nnnn    */  OP(2F, RLA(6, ABSRD, MEM_WRITEBYTE, _TA_) );
#endif
#if 1 /* 30 ~3f */
			/* BMI $nn      */  OP(30, BMI() );
			/* AND ($nn), Y */  OP(31, AND(5, IIYBYTE) );
			/* JAM          */  OP(32, { JAM();  remaining_cycles = 0; }  );
			/* RLA ($nn), Y */  OP(33, RLA(8, IIYRD, MEM_WRITEBYTE, _TA_) );
			/* NOP          */  OP(34, DOP(4) );
			/* AND $nn, X   */  OP(35, AND(4, ZPIXBYTE) );
			/* ROL $nn, X   */  OP(36, ROL(6, ZPIXRD, ZP_WRITEBYTE, _TBA_) );
			/* RLA $nn, X   */  OP(37, RLA(6, ZPIXRD, ZP_WRITEBYTE, _TBA_) );
			/* SEC          */  OP(38, SEC() );
			/* AND $nnnn, Y */  OP(39, AND(4, ABSIYBYTE) );
			/* NOP          */  OP(3A, NOP() );
			/* RLA $nnnn, Y */  OP(3B, RLA(6, ABSIYRD, MEM_WRITEBYTE, _TA_) );
			/* NOP $nnnn, X */  OP(3C, TOP() );
			/* AND $nnnn, X */  OP(3D, AND(4, ABSIXBYTE) );
			/* ROL $nnnn, X */  OP(3E, ROL(7, ABSIXRD, MEM_WRITEBYTE, _TA_) );
			/* RLA $nnnn, X */  OP(3F, RLA(7, ABSIXRD, MEM_WRITEBYTE, _TA_) );
#endif
#if 1 /* 40 ~4f */
			/* RTI          */  OP(40, RTI() );
			/* EOR ($nn, X) */  OP(41, EOR(6, IIXBYTE) );
			/* JAM          */  OP(42, { JAM();  remaining_cycles = 0; }  );
			/* SRE ($nn, X) */  OP(43, SRE(8, IIXRD, MEM_WRITEBYTE, _TA_) );
			/* NOP $nn      */  OP(44, DOP(3) );
			/* EOR $nn      */  OP(45, EOR(3, ZPBYTE) );
			/* LSR $nn      */  OP(46, LSR(5, ZPRD, ZP_WRITEBYTE, _TBA_) );
			/* SRE $nn      */  OP(47, SRE(5, ZPRD, ZP_WRITEBYTE, _TBA_) );
			/* PHA          */  OP(48, PHA() );
			/* EOR #$nn     */  OP(49, EOR(2, IMMBYTE) );
			/* LSR A        */  OP(4A, LSR_A() );
			/* ASR #$nn     */  OP(4B, ASR(2, IMMBYTE) );
			/* JMP $nnnn    */  OP(4C, JMP_ABSOLUTE() );
			/* EOR $nnnn    */  OP(4D, EOR(4, ABSBYTE) );
			/* LSR $nnnn    */  OP(4E, LSR(6, ABSRD, MEM_WRITEBYTE, _TA_) );
			/* SRE $nnnn    */  OP(4F, SRE(6, ABSRD, MEM_WRITEBYTE, _TA_) );
#endif
#if 1 /* 50 ~5f */
			/* BVC $nn      */  OP(50, BVC() );
			/* EOR ($nn), Y */  OP(51, EOR(5, IIYBYTE) );
			/* JAM          */  OP(52, { JAM();  remaining_cycles = 0; }  );
			/* SRE ($nn), Y */  OP(53, SRE(8, IIYRD, MEM_WRITEBYTE, _TA_) );
			/* NOP $nn, X   */  OP(54, DOP(4) );
			/* EOR $nn, X   */  OP(55, EOR(4, ZPIXBYTE) );
			/* LSR $nn, X   */  OP(56, LSR(6, ZPIXRD, ZP_WRITEBYTE, _TBA_) );
			/* SRE $nn, X   */  OP(57, SRE(6, ZPIXRD, ZP_WRITEBYTE, _TBA_) );
			/* CLI          */  OP(58, CLI() );
			/* EOR $nnnn, Y */  OP(59, EOR(4, ABSIYBYTE) );
			/* NOP          */  OP(5A, NOP() );
			/* SRE $nnnn, Y */  OP(5B, SRE(7, ABSIYRD, MEM_WRITEBYTE, _TA_) );
			/* NOP $nnnn, X */  OP(5C, TOP() );
			/* EOR $nnnn, X */  OP(5D, EOR(4, ABSIXBYTE) );
			/* LSR $nnnn, X */  OP(5E, LSR(7, ABSIXRD, MEM_WRITEBYTE, _TA_) );
			/* SRE $nnnn, X */  OP(5F, SRE(7, ABSIXRD, MEM_WRITEBYTE, _TA_) );
#endif
#if 1 /* 60 ~6f */
			/* RTS          */  OP(60, RTS() );
			/* ADC ($nn, X) */  OP(61, ADC(6, IIXBYTE) );
			/* JAM          */  OP(62, { JAM();  remaining_cycles = 0; }  );
			/* RRA ($nn, X) */  OP(63, RRA(8, IIXRD, MEM_WRITEBYTE, _TA_) );
			/* NOP $nn      */  OP(64, DOP(3) );
			/* ADC $nn      */  OP(65, ADC(3, ZPBYTE) );
			/* ROR $nn      */  OP(66, ROR(5, ZPRD, ZP_WRITEBYTE, _TBA_) );
			/* RRA $nn      */  OP(67, RRA(5, ZPRD, ZP_WRITEBYTE, _TBA_) );
			/* PLA          */  OP(68, PLA() );
			/* ADC #$nn     */  OP(69, ADC(2, IMMBYTE) );
			/* ROR A        */  OP(6A, ROR_A() );
			/* ARR #$nn     */  OP(6B, ARR(2, IMMBYTE) );
			/* JMP ($nnnn)  */  OP(6C, JMP_INDIRECT() );
			/* ADC $nnnn    */  OP(6D, ADC(4, ABSBYTE) );
			/* ROR $nnnn    */  OP(6E, ROR(6, ABSRD, MEM_WRITEBYTE, _TA_) );
			/* RRA $nnnn    */  OP(6F, RRA(6, ABSRD, MEM_WRITEBYTE, _TA_) );
#endif
#if 1 /* 70 ~7f */
			/* BVS $nn      */  OP(70, BVS() );
			/* ADC ($nn), Y */  OP(71, ADC(5, IIYBYTE) );
			/* JAM          */  OP(72, { JAM();  remaining_cycles = 0; }  );
			/* SRE ($nn), Y */  OP(73, RRA(8, IIYRD, MEM_WRITEBYTE, _TA_) );
			/* NOP $nn, X   */  OP(74, DOP(4) );
			/* ADC $nn, X   */  OP(75, ADC(4, ZPIXBYTE) );
			/* ROR $nn, X   */  OP(76, ROR(6, ZPIXRD, ZP_WRITEBYTE, _TBA_) );
			/* RRA $nn, X   */  OP(77, RRA(6, ZPIXRD, ZP_WRITEBYTE, _TBA_) );
			/* SEI          */  OP(78, SEI() );
			/* ADC $nnnn, Y */  OP(79, ADC(4, ABSIYBYTE) );
			/* NOP          */  OP(7A, NOP() );
			/* RRA $nnnn, Y */  OP(7B, RRA(7, ABSIYRD, MEM_WRITEBYTE, _TA_) );
			/* NOP $nnnn, X */  OP(7C, TOP() );
			/* ADC $nnnn, X */  OP(7D, ADC(4, ABSIXBYTE) );
			/* ROR $nnnn, X */  OP(7E, ROR(7, ABSIXRD, MEM_WRITEBYTE, _TA_) );
			/* RRA $nnnn, X */  OP(7F, RRA(7, ABSIXRD, MEM_WRITEBYTE, _TA_) );
#endif
#if 1 /* 80 ~8f */
			/* NOP #$nn     */  OP(80, DOP(2) );
			/* STA ($nn, X) */  OP(81, STA(6, IIXADDR, MEM_WRITEBYTE, _TA_) );
			/* NOP #$nn     */  OP(82, DOP(2) );
			/* SAX ($nn, X) */  OP(83, SAX(6, IIXADDR, MEM_WRITEBYTE, _TA_) );
			/* STY $nn      */  OP(84, STY(3, ZPADDR, ZP_WRITEBYTE, _TBA_) );
			/* STA $nn      */  OP(85, STA(3, ZPADDR, ZP_WRITEBYTE, _TBA_) );
			/* STX $nn      */  OP(86, STX(3, ZPADDR, ZP_WRITEBYTE, _TBA_) );
			/* SAX $nn      */  OP(87, SAX(3, ZPADDR, ZP_WRITEBYTE, _TBA_) );
			/* DEY          */  OP(88, DEY() );
			/* NOP #$nn     */  OP(89, DOP(2) );
			/* TXA          */  OP(8A, TXA() );
			/* ANE #$nn     */  OP(8B, ANE(2, IMMBYTE) );
			/* STY $nnnn    */  OP(8C, STY(4, ABSADDR, MEM_WRITEBYTE, _TA_) );
			/* STA $nnnn    */  OP(8D, STA(4, ABSADDR, MEM_WRITEBYTE, _TA_) );
			/* STX $nnnn    */  OP(8E, STX(4, ABSADDR, MEM_WRITEBYTE, _TA_) );
			/* SAX $nnnn    */  OP(8F, SAX(4, ABSADDR, MEM_WRITEBYTE, _TA_) );
#endif
#if 1 /* 90 ~9f */
			/* BCC $nn      */  OP(90, BCC() );
			/* STA ($nn), Y */  OP(91, STA(6, IIYADDR, MEM_WRITEBYTE, _TA_) );
			/* JAM          */  OP(92, { JAM();  remaining_cycles = 0; }  );
			/* SHA ($nn), Y */  OP(93, SHA(6, IIYADDR, MEM_WRITEBYTE, _TA_) );
			/* STY $nn, X   */  OP(94, STY(4, ZPIXADDR, ZP_WRITEBYTE, _TBA_) );
			/* STA $nn, X   */  OP(95, STA(4, ZPIXADDR, ZP_WRITEBYTE, _TBA_) );
			/* STX $nn, Y   */  OP(96, STX(4, ZPIYADDR, ZP_WRITEBYTE, _TBA_) );
			/* SAX $nn, Y   */  OP(97, SAX(4, ZPIYADDR, ZP_WRITEBYTE, _TBA_) );
			/* TYA          */  OP(98, TYA() );
			/* STA $nnnn, Y */  OP(99, STA(5, ABSIYADDR, MEM_WRITEBYTE, _TA_) );
			/* TXS          */  OP(9A, TXS() );
			/* SHS $nnnn, Y */  OP(9B, SHS(5, ABSIYADDR, MEM_WRITEBYTE, _TA_) );
			/* SHY $nnnn, X */  OP(9C, SHY(5, ABSIXADDR, MEM_WRITEBYTE, _TA_) );
			/* STA $nnnn, X */  OP(9D, STA(5, ABSIXADDR, MEM_WRITEBYTE, _TA_) );
			/* SHX $nnnn, Y */  OP(9E, SHX(5, ABSIYADDR, MEM_WRITEBYTE, _TA_) );
			/* SHA $nnnn, Y */  OP(9F, SHA(5, ABSIYADDR, MEM_WRITEBYTE, _TA_) );
#endif
#if 1 /* A0 ~AF */
			/* LDY #$nn     */  OP(A0, LDY(2, IMMBYTE) );
			/* LDA ($nn, X) */  OP(A1, LDA(6, IIXBYTE) );
			/* LDX #$nn     */  OP(A2, LDX(2, IMMBYTE) );
			/* LAX ($nn, X) */  OP(A3, LAX(6, IIXBYTE) );
			/* LDY $nn      */  OP(A4, LDY(3, ZPBYTE) );
			/* LDA $nn      */  OP(A5, LDA(3, ZPBYTE) );
			/* LDX $nn      */  OP(A6, LDX(3, ZPBYTE) );
			/* LAX $nn      */  OP(A7, LAX(3, ZPBYTE) );
			/* TAY          */  OP(A8, TAY() );
			/* LDA #$nn     */  OP(A9, LDA(2, IMMBYTE) );
			/* TAX          */  OP(AA, TAX() );
			/* LXA #$nn     */  OP(AB, LXA(2, IMMBYTE) );
			/* LDY $nnnn    */  OP(AC, LDY(4, ABSBYTE) );
			/* LDA $nnnn    */  OP(AD, LDA(4, ABSBYTE) );
			/* LDX $nnnn    */  OP(AE, LDX(4, ABSBYTE) );
			/* LAX $nnnn    */  OP(AF, LAX(4, ABSBYTE) );
#endif
#if 1 /* B0 ~BF */
			/* BCS $nn      */  OP(B0, BCS() );
			/* LDA ($nn), Y */  OP(B1, LDA(5, IIYBYTE) );
			/* JAM          */  OP(B2, { JAM();  remaining_cycles = 0; }  );
			/* LAX ($nn), Y */  OP(B3, LAX(5, IIYBYTE) );
			/* LDY $nn, X   */  OP(B4, LDY(4, ZPIXBYTE) );
			/* LDA $nn, X   */  OP(B5, LDA(4, ZPIXBYTE) );
			/* LDX $nn, Y   */  OP(B6, LDX(4, ZPIYBYTE) );
			/* LAX $nn, Y   */  OP(B7, LAX(4, ZPIYBYTE) );
			/* CLV          */  OP(B8, CLV() );
			/* LDA $nnnn, Y */  OP(B9, LDA(4, ABSIYBYTE) );
			/* TSX          */  OP(BA, TSX() );
			/* LAS $nnnn, Y */  OP(BB, LAS(4, ABSIYBYTE) );
			/* LDY $nnnn, X */  OP(BC, LDY(4, ABSIXBYTE) );
			/* LDA $nnnn, X */  OP(BD, LDA(4, ABSIXBYTE) );
			/* LDX $nnnn, Y */  OP(BE, LDX(4, ABSIYBYTE) );
			/* LAX $nnnn, Y */  OP(BF, LAX(4, ABSIYBYTE) );
#endif
#if 1 /* C0 ~CF */
			/* CPY #$nn     */  OP(C0, CPY(2, IMMBYTE) );
			/* CMP ($nn, X) */  OP(C1, CMP(6, IIXBYTE) );
			/* NOP #$nn     */  OP(C2, DOP(2) );
			/* DCP ($nn, X) */  OP(C3, DCP(8, IIXRD, MEM_WRITEBYTE, _TA_) );
			/* CPY $nn      */  OP(C4, CPY(3, ZPBYTE) );
			/* CMP $nn      */  OP(C5, CMP(3, ZPBYTE) );
			/* DEC $nn      */  OP(C6, DEC(5, ZPRD, ZP_WRITEBYTE, _TBA_) );
			/* DCP $nn      */  OP(C7, DCP(5, ZPRD, ZP_WRITEBYTE, _TBA_) );
			/* INY          */  OP(C8, INY() );
			/* CMP #$nn     */  OP(C9, CMP(2, IMMBYTE) );
			/* DEX          */  OP(CA, DEX() );
			/* SBX #$nn     */  OP(CB, SBX(2, IMMBYTE) );
			/* CPY $nnnn    */  OP(CC, CPY(4, ABSBYTE) );
			/* CMP $nnnn    */  OP(CD, CMP(4, ABSBYTE) );
			/* DEC $nnnn    */  OP(CE, DEC(6, ABSRD, MEM_WRITEBYTE, _TA_) );
			/* DCP $nnnn    */  OP(CF, DCP(6, ABSRD, MEM_WRITEBYTE, _TA_) );
#endif
#if 1 /* D0 ~DF */
			/* BNE $nn      */  OP(D0, BNE() );
			/* CMP ($nn), Y */  OP(D1, CMP(5, IIYBYTE) );
			/* JAM          */  OP(D2, { JAM();  remaining_cycles = 0; }  );
			/* DCP ($nn), Y */  OP(D3, DCP(8, IIYRD, MEM_WRITEBYTE, _TA_) );
			/* NOP $nn, X   */  OP(D4, DOP(4) );
			/* CMP $nn, X   */  OP(D5, CMP(4, ZPIXBYTE) );
			/* DEC $nn, X   */  OP(D6, DEC(6, ZPIXRD, ZP_WRITEBYTE, _TBA_) );
			/* DCP $nn, X   */  OP(D7, DCP(6, ZPIXRD, ZP_WRITEBYTE, _TBA_) );
			/* CLD          */  OP(D8, CLD() );
			/* CMP $nnnn, Y */  OP(D9, CMP(4, ABSIYBYTE) );
			/* NOP          */  OP(DA, NOP() );
			/* DCP $nnnn, Y */  OP(DB, DCP(7, ABSIYRD, MEM_WRITEBYTE, _TA_) );
			/* NOP $nnnn, X */  OP(DC, TOP() );
			/* CMP $nnnn, X */  OP(DD, CMP(4, ABSIXBYTE) );
			/* DEC $nnnn, X */  OP(DE, DEC(7, ABSIXRD, MEM_WRITEBYTE, _TA_) );
			/* DCP $nnnn, X */  OP(DF, DCP(7, ABSIXRD, MEM_WRITEBYTE, _TA_) );
#endif
#if 1 /* E0 ~EF */
			/* CPX #$nn     */  OP(E0, CPX(2, IMMBYTE) );
			/* SBC ($nn, X) */  OP(E1, SBC(6, IIXBYTE) );
			/* NOP #$nn     */  OP(E2, DOP(2) );
			/* ISB ($nn, X) */  OP(E3, ISB(8, IIXRD, MEM_WRITEBYTE, _TA_) );
			/* CPX $nn      */  OP(E4, CPX(3, ZPBYTE) );
			/* SBC $nn      */  OP(E5, SBC(3, ZPBYTE) );
			/* INC $nn      */  OP(E6, INC(5, ZPRD, ZP_WRITEBYTE, _TBA_) );
			/* ISB $nn      */  OP(E7, ISB(5, ZPRD, ZP_WRITEBYTE, _TBA_) );
			/* INX          */  OP(E8, INX() );
			/* SBC #$nn     */  OP(E9, SBC(2, IMMBYTE) );
			/* NOP          */  OP(EA, NOP() );
			/* USBC #$nn    */  OP(EB, SBC(2, IMMBYTE) );
			/* CPX $nnnn    */  OP(EC, CPX(4, ABSBYTE) );
			/* SBC $nnnn    */  OP(ED, SBC(4, ABSBYTE) );
			/* INC $nnnn    */  OP(EE, INC(6, ABSRD, MEM_WRITEBYTE, _TA_) );
			/* ISB $nnnn    */  OP(EF, ISB(6, ABSRD, MEM_WRITEBYTE, _TA_) );
#endif
#if 1 /* F0 ~FF */
			/* BEQ $nn      */  OP(F0, BEQ() );
			/* SBC ($nn), Y */  OP(F1, SBC(5, IIYBYTE) );
			/* JAM          */  OP(F2, { JAM();  remaining_cycles = 0; }  );
			/* ISB ($nn), Y */  OP(F3, ISB(8, IIYRD, MEM_WRITEBYTE, _TA_) );
			/* NOP ($nn, X) */  OP(F4, DOP(4) );
			/* SBC $nn, X   */  OP(F5, SBC(4, ZPIXBYTE) );
			/* INC $nn, X   */  OP(F6, INC(6, ZPIXRD, ZP_WRITEBYTE, _TBA_) );
			/* ISB $nn, X   */  OP(F7, ISB(6, ZPIXRD, ZP_WRITEBYTE, _TBA_) );
			/* SED          */  OP(F8, SED() );
			/* SBC $nnnn, Y */  OP(F9, SBC(4, ABSIYBYTE) );
			/* NOP          */  OP(FA, NOP() );
			/* ISB $nnnn, Y */  OP(FB, ISB(7, ABSIYRD, MEM_WRITEBYTE, _TA_) );
			/* NOP $nnnn, X */  OP(FC, TOP() );
			/* SBC $nnnn, X */  OP(FD, SBC(4, ABSIXBYTE) );
			/* INC $nnnn, X */  OP(FE, INC(7, ABSIXRD, MEM_WRITEBYTE, _TA_) );
			/* ISB $nnnn, X */  OP(FF, ISB(7, ABSIXRD, MEM_WRITEBYTE, _TA_) );
#endif
			}
		}


		if(p_cpu->INT_pending)
		{
			if((p_cpu->INT_pending & NMI_MASK))
			{
				p_cpu->INT_pending &= ~NMI_MASK;
				NMI();
			}
			else if((p_cpu->INT_pending & IRQ_MASK))
			{
				if(!CHKI())
				{
					p_cpu->INT_pending &= ~IRQ_MASK;
					IRQ();
				}
			}
		}
	}
	SAVE_LOCAL_VARS();
	return (ines_int_t)(_total_cycles_ - old_cycles);
}

#pragma pack(push, 1)
struct _ines_state_cpu_data_
{
/* 0 - 7 : 8 bytes  */
	ines_byte_t    A;
	ines_byte_t    X;
	ines_byte_t    Y;
	ines_byte_t    P;
	ines_byte_t    SP;
	ines_byte_t    IRQ;
	ines_word_t    PC;
	//ines_byte_t    jammed;
/* 8 - 15 : 8 bytes  */
	ines_int_t     next_IRQ_cycles;
	ines_int_t     burn_cycles;
/* 16 - 23 : 8 bytes  */
	ines_int64_t   total_cycles;
/* 24 - 31 : 8 bytes  */
	ines_byte_t    WR[8];  // 3~7 unit is 0: bank to PROM, 1,2: bank to SRAM
/* 32 - 47 : 16 bytes  */
	ines_word_t    BANK[8];
/* 48 - 2095 : 2048 bytes  */
	ines_byte_t    RAM[0x800];
};
/* total 2096 bytes */

typedef struct _ines_state_cpu_data_   ines_state_cpu_data_t;
#pragma pack(pop)

int ines_cpu_save_state(ines_cpu_t* p_cpu, FILE* fSave)
{
	int  n;
	ines_byte_t*  PROM;
	ines_byte_t*  SRAM;

	ines_state_cpu_data_t   data;
	memset(&data, 0, sizeof(data));
	data.A = p_cpu->reg_A;
	data.X = p_cpu->reg_X;
	data.Y = p_cpu->reg_Y;
	data.P = p_cpu->reg_P;
	data.SP = p_cpu->reg_SP;
	data.PC = p_cpu->reg_PC;
	data.IRQ = p_cpu->INT_pending;
	// data.jammed = p_cpu->jammed;
	data.next_IRQ_cycles = p_cpu->apu_next_irq;
	data.burn_cycles = p_cpu->burn_cycles;
	data.total_cycles = p_cpu->total_cycles;

	PROM = cpu2host(p_cpu)->rom.pPROMs;
	SRAM = cpu2host(p_cpu)->SRAM;
#define  PROM28KNUM(p)      ( ((p)-PROM)>>13 )
#define  SRAM28KNUM(p)      ( ((p)-SRAM)>>13 )
	for(n = 3; n < 8; n++)
	{
		data.WR[n] = p_cpu->bank_writeable[n];
		if(p_cpu->bank_writeable[n] == 0)
		{
			data.BANK[n] = PROM28KNUM(p_cpu->mem_bank[n]);
		}
		else
		{
			data.BANK[n] = SRAM28KNUM(p_cpu->mem_bank[n]);
		}
	}
	memcpy(data.RAM,  p_cpu->RAM, sizeof(data.RAM));

	fwrite(&data, sizeof(data), 1, fSave);
	return 0;
}

ines_int_t ines_cpu_load_state(ines_cpu_t* p_cpu, FILE* fSave)
{
	int  n;

	ines_state_cpu_data_t   data;
	memset(p_cpu, 0, sizeof(*p_cpu));


	fread(&data, sizeof(data), 1, fSave);

	p_cpu->reg_A = data.A;
	p_cpu->reg_X = data.X;
	p_cpu->reg_Y = data.Y;
	p_cpu->reg_P = data.P;
	p_cpu->reg_SP = data.SP;
	p_cpu->reg_PC = data.PC;
	p_cpu->INT_pending = data.IRQ;
	p_cpu->jammed = 0;
	p_cpu->apu_next_irq = data.next_IRQ_cycles;
	p_cpu->burn_cycles = data.burn_cycles;
	p_cpu->total_cycles = data.total_cycles;

	for(n = 3; n < 8; n++)
	{
		p_cpu->bank_writeable[n] = data.WR[n];
		if(p_cpu->bank_writeable[n] == 0)
		{
			if(data.BANK[n] >= cpu2host(p_cpu)->prom_8k_num)
				return -1;
			p_cpu->mem_bank[n] = cpu2host(p_cpu)->rom.pPROMs + ((ines_dword_t) data.BANK[n] << 13);
		}
		else
		{
			if(data.BANK[n] >= NES_MAX_SRAM_BANKS)
				return -1;
			p_cpu->mem_bank[n] = cpu2host(p_cpu)->SRAM + ((ines_dword_t) data.BANK[n] << 13);
		}
	}
	memcpy(p_cpu->RAM, data.RAM,  sizeof(data.RAM));

	return 0;
}


#endif
