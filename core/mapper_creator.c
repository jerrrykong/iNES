
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
	/* 033 */  mapper33_create, /* implemented -- Taito TC0190 (掩码 $A003: 8KB PRG 双窗口 + 2x2KB/4x1KB CHR + H-V 镜像, 无 IRQ) */
	/* 034 */  mapper34_create, /* implemented -- BNROM(CHR<=8KB, 32KB PRG bank + AND 型总线冲突) / NINA-001、002(CHR>8KB, $7FFD-$7FFF 寄存器叠在 8K PRG-RAM 上 + 2x4KB CHR, 无 IRQ) */
	/* 035 */  mapper35_create,
	/* 036 */  mapper36_create,
	/* 037 */  mapper37_create,
	/* 038 */  mapper38_create,
	/* 039 */  mapper39_create,
	/* 040 */  mapper40_create,
	/* 041 */  mapper41_create, /* implemented -- Caltron 6-in-1 多合一板 (外层 $6000-$67FF 由写入**地址**译码: 32KB PRG + 外层32KB/内层8KB 两级 CHR + H-V 镜像; 内层 CHR 写 $8000+ 且仅 PRG bank 4..7 时有效, 无 PRG-RAM, 无 IRQ) */
	/* 042 */  mapper42_create,
	/* 043 */  mapper43_create, /* implemented -- TONY-I / YS-612（SMB2J 的 FDS->ROM 转卡带，两者只差 IRQ 寄存器地址 $4122 / $8122）：PRG 80KB = 两块 32KB + 2KB 芯片重复四遍 + 8KB 芯片；$6000/$8000/$A000 固定 #2/#1/#0、$C000 可切（$4022 bit2-0 经译码表 4,3,4,4,4,7,5,6）、$E000 是 8KB 芯片；$5000-$5FFF 是 2KB 芯片重复一次（宿主 bank2 无 PRG 接口，由 readlow 提供）；CHR 8KB 不分页；IRQ 为 12 位 M2 周期计数器溢出触发（hsync 里按 cpu.total_cycles 增量推进）；$6000 是 PRG 故 custom_sram=1 */
	/* 044 */  mapper44_create, /* implemented -- Super Big 7-in-1：MMC3 多合一，$A001 低 3 位选块（选 7 等同 6），MMC3 页号（含两个固定页）经 AND/OR 映射到块内；块 0-5 各 128KB、块 6/7 各 256KB PRG+CHR，整卡 1MB+1MB；上电选块 0 */
	/* 045 */  mapper45_create, /* implemented -- GA23C 多合一：MMC3 内核 + $6000 四个外层 bank 寄存器（按写入次序轮流填 #0-#3，写 $6001 清零并解锁，#3.bit6 锁定后 $6000 写入失效）；MMC3 选出的页号（含两个固定页 0x3E/0x3F）经 AND/OR 映射到外层窗口；外层寄存器叠在 WRAM 上且不受 MMC3 的 WRAM 位控制 */
	/* 046 */  mapper46_create, /* implemented -- Rumble Station 15-in-1：外层 $6000-$7FFF 写入值 [CCCC PPPP] 选 64KB CHR / 64KB PRG bank，内层 $8000-$FFFF 写入值 [.CCC ...P]（Color Dreams 的缩减子集）选 bank 内的 8KB CHR 与低/高 32KB PRG；合成后 32KB PRG bank = (外层 PPPP << 1) | 内层 P、8KB CHR bank = (外层 CCCC << 3) | 内层 CCC，各最多 1MB；$6000-$7FFF 是寄存器故无 PRG-RAM（custom_sram = 1），无 IRQ、不控镜像；上电外层为 0 */
	/* 047 */  mapper47_create,
	/* 048 */  mapper48_create, /* implemented -- Taito TC0690 (TC0190 超集: 掩码 $E003, 8KB PRG 双窗口 + 2x2KB/4x1KB CHR + $E000 镜像 + MMC3 式 IRQ(reload 取反)) */
	/* 049 */  mapper49_create,
	/* 050 */  mapper50_create,
	/* 051 */  mapper51_create,
	/* 052 */  mapper52_create,
	/* 053 */  mapper53_create,
	/* 054 */  mapper54_create,
	/* 055 */  mapper55_create,
	/* 056 */  mapper56_create,
	/* 057 */  mapper57_create, /* implemented -- GK 47-in-1 / SuperGK 6-in-1：寄存器掩码 $8800（只按 A11 分组）。$8000 = [CH.. ..AA]（C = CHR Mode 0=CNROM/1=NROM，H = CHR A16，AA = CNROM 模式下的 CHR A13-14）；$8800 = [PPPO MBbb]（PPP = PRG Reg，O = PRG Mode，M = 镜像 0 垂直/1 水平，B = CHR A15，bb = NROM 模式下的 CHR A13-14）。CHR 是整块 8KB 一起切：bank = (H<<3)|(B<<2)|(C ? bb : AA)。PRG：PPP 在两种模式下都是 16KB 页号 —— Mode 0 = 16KB bank（= PPP）同时镜像到 $8000 与 $C000 两个窗口，Mode 1 = 整个 32KB 窗口 = 32KB bank #(PPP >> 1)。无 IRQ、无 PRG-RAM。SuperGK 第 4 项（Mode 1，PPP = 6）实测确认右移 */
	/* 058 */  mapper58_create, /* implemented -- 简单 NROM/CNROM 型多合一（GK-192 118-in-1 / HKX5268 68-in-1 等）：**Address Latch**，bank 号由写入地址译码、写入数据只用来选镜像（与 41/225/255 同族）。$8000-$FFFF 写：A6 = PRG Mode（0 = NROM-256，32KB bank #((A2..A1)>>1)；1 = NROM-128，16KB bank #(A2..A0) 镜像到 $8000 与 $C000），A5..A3 = 8KB CHR bank，数据 D1 = 镜像（1 垂直 / 0 水平）。CHR 整块 8KB 一起切。无 IRQ、无 PRG-RAM、无状态寄存器，上电 = 16KB bank 0 镜像。iNES Mapper 213 是本编号的重复。实现参照 VirtuaNES 0.97 Mapper058.cpp */
	/* 059 */  mapper59_create,
	/* 060 */  mapper60_create, /* implemented -- Reset-based NROM-128 4-in-1 多合一（kevtris "Reset Based Four in One"）：卡带里是若干个各自独立的 NROM-128 游戏，每块 16KB PRG + 8KB CHR，**没有任何 bank 寄存器** —— 当前块由片内计数器决定，只能靠软复位递增（上电停在块 0，之后每复位 +1）。16KB 块 #N 镜像到 $8000 与 $C000，CHR 是 8KB 块 #N 整块切换；块数 = min(PRG 16KB 块数, CHR 8KB 块数)，计数器对块数取模（4-in-1 即 2 位）。无 IRQ、无 PRG-RAM、不控镜像。注意 FCEUX 把 T3H53 放在 60、把本卡带挤走，故标 60 却非 reset-based 的 ROM 实为 mapper 59 */
	/* 061 */  mapper61_create,
	/* 062 */  mapper62_create,
	/* 063 */  mapper63_create,
	/* 064 */  mapper64_create, /* implemented -- Tengen RAMBO-1（MMC3 的 Tengen 版，40-pin ASIC）：PRG 三个 8KB 可切页 + $E000 固定最后一页；CHR 支持 2×2KB+4×1KB 与 4×1KB+4×1KB 两种切法（K 位），并可与高区整体互换（C 位）。$8000-$9FFE even = Bank select [CPKx RRRR]（C=CHR A12 反相、P=PRG 页序、K=1 启用 R8/R9、RRRR=0-5 CHR / 6,7,F PRG / 8,9 额外 CHR），$8001 odd = Bank data，$A000 even = 镜像（1 水平），$A001 odd 未实现（无 PRG-RAM）；IRQ 有扫描线与 CPU 周期两种模式（$C001 bit0），计数器规则：写过 $C001 或归零则重载 latch（非 0 时再 OR 1），否则递减，归零且允许时触发（CPU 周期模式每 4 个 CPU 周期一步，在 hsync 里按 cpu.total_cycles 增量批处理）。Mapper 158 是同一芯片的 TLSROM 镜像变体，实现在 core/mapper/64.c（tlsrom 标志），158.c 只做转发 */
	/* 065 */  mapper65_create, /* implemented -- Irem H3001：PRG 三个 8KB 窗口（$8000 ← Reg0 或 $C000 ← Reg0，由 $9000 bit7 选择，另一边恒为 $3E；$A000 恒为 Reg1；$E000 恒为 $3F，页号按卡带容量回卷），$C000 **不可切换**（资料明确纠正了 Disch 旧文档的说法，写入忽略）；$B000-$B007 = 八个 1KB CHR 窗口；$9001 [MM.. ....] = 镜像（00 垂直 / 10 水平 / 01,11 单屏 A）；IRQ 是一个 16 位递减计数器，允许时每 CPU 周期减 1，减到 0 触发并**停在 0**（不回卷、不自动重载），写 $9004 装入重载值、写 $9003/$9004 应答，$9005 是重载值**高**字节、$9006 是低字节。上电 PRG Reg0=$00、Reg1=$01（资料注明游戏依赖此初值否则崩溃）。地址掩码 $F007；CPU 周期型 IRQ 在 hsync 里按 cpu.total_cycles 增量批处理推进 */
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
	/* 158 */  mapper158_create, /* implemented（= 064 的镜像变体）-- RAMBO-1 的 TLSROM 接法：CIRAM A10 接 CHR A17，映射到 PPU $0000-$0FFF 的每个 CHR 页的 bit7 决定对应 nametable 用 CIRAM 哪一页（资料点名 Alien Syndrome）。bank 切换与 IRQ 与 64 完全一致，故 core/mapper/158.c 以 tlsrom = 1 转发到 64 的实现，不另写一套。本编号无实测 ROM（清单里没有标 158 的卡带） */
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
	/* 213 */  mapper213_create, /* implemented（= 058 的同名/重复编号）-- NESDev 原文 "iNES Mapper 213 is a duplicate of INES Mapper 058"（9999999-in-1、168-in-1 等），并注明这些 ROM "run well as mapper 58"，故 core/mapper/213.c 直接复用 58 的实现而不另写一套。注意 BMC-411120-C 板是 NES 2.0 Mapper 287（MMC3 变体），与本编号无关 */
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
	/* 225 */  mapper225_create, /* implemented -- ET-4310 / K-1010 多合一板 (bank 由写入地址译码: 16K/32K PRG + 8KB CHR + H-V 镜像, $5800-$5FFF 4x4bit RAM, 无 IRQ) */
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
	/* 255 */  mapper255_create, /* implemented -- 110-in-1 多合一板 (与 225 同构: bank 由写入地址译码, 16K/32K PRG + 8KB CHR + H-V 镜像, $5800-$5FFF 4x4bit RAM, 无 IRQ) */
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


