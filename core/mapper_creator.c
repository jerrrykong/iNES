
#include "nes.h"
#include "mapper.h"
#include "../comm/log.h"


/** creators  forward declare*/

extern ines_bool_t mapper0_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper1_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper2_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper3_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper4_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper5_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper6_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper7_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper8_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper9_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper10_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper11_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper12_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper13_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper14_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper15_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper16_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper17_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper18_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper19_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper20_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper21_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper22_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper23_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper24_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper25_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper26_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper27_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper28_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper29_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper30_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper31_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper32_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper33_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper34_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper35_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper36_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper37_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper38_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper39_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper40_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper41_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper42_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper43_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper44_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper45_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper46_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper47_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper48_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper49_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper50_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper51_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper52_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper53_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper54_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper55_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper56_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper57_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper58_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper59_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper60_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper61_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper62_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper63_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper64_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper65_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper66_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper67_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper68_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper69_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper70_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper71_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper72_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper73_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper74_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper75_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper76_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper77_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper78_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper79_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper80_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper81_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper82_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper83_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper84_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper85_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper86_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper87_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper88_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper89_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper90_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper91_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper92_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper93_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper94_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper95_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper96_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper97_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper98_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper99_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper100_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper101_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper102_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper103_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper104_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper105_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper106_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper107_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper108_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper109_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper110_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper111_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper112_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper113_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper114_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper115_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper116_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper117_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper118_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper119_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper120_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper121_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper122_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper123_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper124_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper125_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper126_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper127_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper128_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper129_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper130_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper131_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper132_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper133_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper134_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper135_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper136_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper137_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper138_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper139_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper140_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper141_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper142_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper143_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper144_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper145_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper146_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper147_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper148_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper149_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper150_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper151_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper152_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper153_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper154_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper155_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper156_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper157_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper158_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper159_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper160_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper161_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper162_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper163_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper164_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper165_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper166_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper167_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper168_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper169_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper170_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper171_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper172_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper173_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper174_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper175_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper176_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper177_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper178_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper179_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper180_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper181_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper182_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper183_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper184_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper185_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper186_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper187_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper188_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper189_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper190_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper191_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper192_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper193_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper194_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper195_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper196_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper197_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper198_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper199_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper200_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper201_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper202_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper203_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper204_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper205_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper206_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper207_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper208_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper209_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper210_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper211_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper212_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper213_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper214_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper215_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper216_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper217_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper218_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper219_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper220_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper221_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper222_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper223_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper224_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper225_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper226_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper227_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper228_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper229_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper230_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper231_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper232_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper233_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper234_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper235_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper236_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper237_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper238_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper239_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper240_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper241_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper242_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper243_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper244_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper245_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper246_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper247_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper248_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper249_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper250_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper251_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper252_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper253_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper254_create(ines_mapper_t* p_mapper);
extern ines_bool_t mapper255_create(ines_mapper_t* p_mapper);

/***/
#define MAX_MAPPER_CREATOR    256
static ines_bool_t (*mapper_creator_func[MAX_MAPPER_CREATOR] )(ines_mapper_t* ) = 
{
	/* 000 */  mapper0_create, /* implemented */
	/* 001 */  mapper1_create, /* implemented */
	/* 002 */  mapper2_create, /* implemented */
	/* 003 */  mapper3_create, /* implemented */
	/* 004 */  mapper4_create, /* implemented */
	/* 005 */  mapper5_create, /* implemented */
	/* 006 */  mapper6_create, /* implemented */
	/* 007 */  mapper7_create, /* implemented */
	/* 008 */  mapper8_create, /* implemented */
	/* 009 */  mapper9_create, /* implemented */
	/* 010 */  mapper10_create, /* implemented */
	/* 011 */  mapper11_create, /* implemented */
	/* 012 */  mapper12_create, /* implemented */
	/* 013 */  mapper13_create, /* implemented */
	/* 014 */  mapper14_create,
	/* 015 */  mapper15_create, /* implemented */
	/* 016 */  mapper16_create, /* implemented -- FCG, no EEPROM */
	/* 017 */  mapper17_create, /* implemented -- Front Fareast Super Magic Card (4M PRG / 1KB CHR-RAM / WRAM 切页 / IRQ 计数器 / trainer) */
	/* 018 */  mapper18_create, /* implemented */
	/* 019 */  mapper19_create, /* implemented -- Namco 163 (映射/IRQ/WRAM 写保护/12 窗口 CHR 与 CIRAM 当 CHR/扩展音) */
	/* 020 */  mapper20_create,
	/* 021 */  mapper21_create, /* implemented -- Konami VRC4a/c */
	/* 022 */  mapper22_create, /* implemented -- Konami VRC2a */
	/* 023 */  mapper23_create, /* implemented -- Konami VRC2b/VRC4f */
	/* 024 */  mapper24_create, /* implemented -- Konami VRC6a */
	/* 025 */  mapper25_create, /* implemented -- Konami VRC2c/VRC4b/d/e */
	/* 026 */  mapper26_create, /* implemented -- Konami VRC6b */
	/* 027 */  mapper27_create,
	/* 028 */  mapper28_create,
	/* 029 */  mapper29_create,
	/* 030 */  mapper30_create,
	/* 031 */  mapper31_create,
	/* 032 */  mapper32_create, /* implemented -- Irem G-101 (8KB PRG 双窗口 + $8000/$C000 模式交换 / 8x1KB CHR / H-V 镜像, 无 IRQ) */
	/* 033 */  mapper33_create,
	/* 034 */  mapper34_create,
	/* 035 */  mapper35_create,
	/* 036 */  mapper36_create,
	/* 037 */  mapper37_create,
	/* 038 */  mapper38_create,
	/* 039 */  mapper39_create,
	/* 040 */  mapper40_create,
	/* 041 */  mapper41_create,
	/* 042 */  mapper42_create,
	/* 043 */  mapper43_create,
	/* 044 */  mapper44_create,
	/* 045 */  mapper45_create,
	/* 046 */  mapper46_create,
	/* 047 */  mapper47_create,
	/* 048 */  mapper48_create,
	/* 049 */  mapper49_create,
	/* 050 */  mapper50_create,
	/* 051 */  mapper51_create,
	/* 052 */  mapper52_create,
	/* 053 */  mapper53_create,
	/* 054 */  mapper54_create,
	/* 055 */  mapper55_create,
	/* 056 */  mapper56_create,
	/* 057 */  mapper57_create,
	/* 058 */  mapper58_create,
	/* 059 */  mapper59_create,
	/* 060 */  mapper60_create,
	/* 061 */  mapper61_create,
	/* 062 */  mapper62_create,
	/* 063 */  mapper63_create,
	/* 064 */  mapper64_create,
	/* 065 */  mapper65_create,
	/* 066 */  mapper66_create,
	/* 067 */  mapper67_create,
	/* 068 */  mapper68_create,
	/* 069 */  mapper69_create,
	/* 070 */  mapper70_create,
	/* 071 */  mapper71_create,
	/* 072 */  mapper72_create,
	/* 073 */  mapper73_create,
	/* 074 */  mapper74_create,
	/* 075 */  mapper75_create,
	/* 076 */  mapper76_create,
	/* 077 */  mapper77_create,
	/* 078 */  mapper78_create,
	/* 079 */  mapper79_create,
	/* 080 */  mapper80_create,
	/* 081 */  mapper81_create,
	/* 082 */  mapper82_create,
	/* 083 */  mapper83_create,
	/* 084 */  mapper84_create,
	/* 085 */  mapper85_create, /* implemented -- Konami VRC7 */
	/* 086 */  mapper86_create,
	/* 087 */  mapper87_create,
	/* 088 */  mapper88_create,
	/* 089 */  mapper89_create,
	/* 090 */  mapper90_create,
	/* 091 */  mapper91_create,
	/* 092 */  mapper92_create,
	/* 093 */  mapper93_create,
	/* 094 */  mapper94_create,
	/* 095 */  mapper95_create,
	/* 096 */  mapper96_create,
	/* 097 */  mapper97_create,
	/* 098 */  mapper98_create,
	/* 099 */  mapper99_create,
	/* 100 */  mapper100_create,
	/* 101 */  mapper101_create,
	/* 102 */  mapper102_create,
	/* 103 */  mapper103_create,
	/* 104 */  mapper104_create,
	/* 105 */  mapper105_create,
	/* 106 */  mapper106_create,
	/* 107 */  mapper107_create,
	/* 108 */  mapper108_create,
	/* 109 */  mapper109_create,
	/* 110 */  mapper110_create,
	/* 111 */  mapper111_create,
	/* 112 */  mapper112_create,
	/* 113 */  mapper113_create,
	/* 114 */  mapper114_create,
	/* 115 */  mapper115_create,
	/* 116 */  mapper116_create,
	/* 117 */  mapper117_create,
	/* 118 */  mapper118_create,
	/* 119 */  mapper119_create,
	/* 120 */  mapper120_create,
	/* 121 */  mapper121_create,
	/* 122 */  mapper122_create,
	/* 123 */  mapper123_create,
	/* 124 */  mapper124_create,
	/* 125 */  mapper125_create,
	/* 126 */  mapper126_create,
	/* 127 */  mapper127_create,
	/* 128 */  mapper128_create,
	/* 129 */  mapper129_create,
	/* 130 */  mapper130_create,
	/* 131 */  mapper131_create,
	/* 132 */  mapper132_create,
	/* 133 */  mapper133_create,
	/* 134 */  mapper134_create,
	/* 135 */  mapper135_create,
	/* 136 */  mapper136_create,
	/* 137 */  mapper137_create,
	/* 138 */  mapper138_create,
	/* 139 */  mapper139_create,
	/* 140 */  mapper140_create,
	/* 141 */  mapper141_create,
	/* 142 */  mapper142_create,
	/* 143 */  mapper143_create,
	/* 144 */  mapper144_create,
	/* 145 */  mapper145_create,
	/* 146 */  mapper146_create,
	/* 147 */  mapper147_create,
	/* 148 */  mapper148_create,
	/* 149 */  mapper149_create,
	/* 150 */  mapper150_create,
	/* 151 */  mapper151_create,
	/* 152 */  mapper152_create,
	/* 153 */  mapper153_create,
	/* 154 */  mapper154_create,
	/* 155 */  mapper155_create,
	/* 156 */  mapper156_create,
	/* 157 */  mapper157_create,
	/* 158 */  mapper158_create,
	/* 159 */  mapper159_create,
	/* 160 */  mapper160_create,
	/* 161 */  mapper161_create,
	/* 162 */  mapper162_create,
	/* 163 */  mapper163_create,
	/* 164 */  mapper164_create,
	/* 165 */  mapper165_create,
	/* 166 */  mapper166_create,
	/* 167 */  mapper167_create,
	/* 168 */  mapper168_create,
	/* 169 */  mapper169_create,
	/* 170 */  mapper170_create,
	/* 171 */  mapper171_create,
	/* 172 */  mapper172_create,
	/* 173 */  mapper173_create,
	/* 174 */  mapper174_create,
	/* 175 */  mapper175_create,
	/* 176 */  mapper176_create,
	/* 177 */  mapper177_create,
	/* 178 */  mapper178_create,
	/* 179 */  mapper179_create,
	/* 180 */  mapper180_create,
	/* 181 */  mapper181_create,
	/* 182 */  mapper182_create,
	/* 183 */  mapper183_create,
	/* 184 */  mapper184_create,
	/* 185 */  mapper185_create,
	/* 186 */  mapper186_create,
	/* 187 */  mapper187_create,
	/* 188 */  mapper188_create,
	/* 189 */  mapper189_create,
	/* 190 */  mapper190_create,
	/* 191 */  mapper191_create,
	/* 192 */  mapper192_create,
	/* 193 */  mapper193_create,
	/* 194 */  mapper194_create,
	/* 195 */  mapper195_create,
	/* 196 */  mapper196_create,
	/* 197 */  mapper197_create,
	/* 198 */  mapper198_create,
	/* 199 */  mapper199_create,
	/* 200 */  mapper200_create,
	/* 201 */  mapper201_create,
	/* 202 */  mapper202_create,
	/* 203 */  mapper203_create,
	/* 204 */  mapper204_create,
	/* 205 */  mapper205_create,
	/* 206 */  mapper206_create,
	/* 207 */  mapper207_create,
	/* 208 */  mapper208_create,
	/* 209 */  mapper209_create,
	/* 210 */  mapper210_create, /* implemented -- Namco 175/340 (同 iNES 号, 340 可选镜像, 变体不区分) */
	/* 211 */  mapper211_create,
	/* 212 */  mapper212_create,
	/* 213 */  mapper213_create,
	/* 214 */  mapper214_create,
	/* 215 */  mapper215_create,
	/* 216 */  mapper216_create,
	/* 217 */  mapper217_create,
	/* 218 */  mapper218_create,
	/* 219 */  mapper219_create,
	/* 220 */  mapper220_create,
	/* 221 */  mapper221_create,
	/* 222 */  mapper222_create,
	/* 223 */  mapper223_create,
	/* 224 */  mapper224_create,
	/* 225 */  mapper225_create,
	/* 226 */  mapper226_create,
	/* 227 */  mapper227_create,
	/* 228 */  mapper228_create,
	/* 229 */  mapper229_create,
	/* 230 */  mapper230_create,
	/* 231 */  mapper231_create,
	/* 232 */  mapper232_create,
	/* 233 */  mapper233_create,
	/* 234 */  mapper234_create,
	/* 235 */  mapper235_create,
	/* 236 */  mapper236_create,
	/* 237 */  mapper237_create,
	/* 238 */  mapper238_create,
	/* 239 */  mapper239_create,
	/* 240 */  mapper240_create,
	/* 241 */  mapper241_create,
	/* 242 */  mapper242_create,
	/* 243 */  mapper243_create,
	/* 244 */  mapper244_create,
	/* 245 */  mapper245_create,
	/* 246 */  mapper246_create,
	/* 247 */  mapper247_create,
	/* 248 */  mapper248_create,
	/* 249 */  mapper249_create,
	/* 250 */  mapper250_create,
	/* 251 */  mapper251_create,
	/* 252 */  mapper252_create,
	/* 253 */  mapper253_create,
	/* 254 */  mapper254_create,
	/* 255 */  mapper255_create,
};


ines_bool_t ines_mapper_create(ines_mapper_t* p_mapper, ines_int_t mapper_id)
{
	if(p_mapper == NULL)
		return ines_false;

	if(mapper_id < 0 || mapper_id>= MAX_MAPPER_CREATOR)
	{
		INES_LOG(LOG_ERR, MOD_MMC, ISTR("Invalid mapper id [%d]\n"), mapper_id);
		return ines_false;
	}

	if(mapper_creator_func[mapper_id] == NULL)
	{
		INES_LOG(LOG_ERR, MOD_MMC, ISTR("Unsupport mapper id [%d]\n"), mapper_id);
		return ines_false;
	}



	return (*mapper_creator_func[mapper_id])(p_mapper);
}


