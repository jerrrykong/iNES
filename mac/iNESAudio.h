#ifndef __INES_AUDIO_H__
#define __INES_AUDIO_H__


#import <Foundation/Foundation.h>

#include "../comm/idef.h"


// 音频输出参数: 与核心 APU 的输出一致(44.1kHz 单声道), 但采样位深需要从
// APU 的 8bit unsigned 转换成 CoreAudio 使用的 SInt16。
#define INES_AUDIO_SAMPLE_RATE       44100
// APU 一帧最多产出的字节数(1024 为余量, 与 libinescore.c 保持一致)
#define INES_AUDIO_MAX_FRAME_LEN     (1024 + INES_AUDIO_SAMPLE_RATE / 5)
// 缓冲池大小(等价 win32 前端的 MAX_BUF_NUM)
#define INES_AUDIO_BUFFER_NUM        10
// 每个缓冲的容量: 200ms(等价 win32 的 NES_AUDIO_BYTES_PER_SECOND/5)
#define INES_AUDIO_BUFFER_SAMPLES    (INES_AUDIO_SAMPLE_RATE / 5)
// 启动时的静音预填充帧数, 用于建立初始延迟
#define INES_AUDIO_PREROLL_COUNT     4


// AudioQueue 音频输出。
//
// 线程约定: 请在"模拟线程"上调用 start / pushFrame / stop —— 与 win32 前端
// 里 waveOut 只在主循环里被使用的模型一致, 从而避免开始/停止与投喂之间的竞态。
@interface iNESAudio : NSObject

// 打开音频设备并预填充静音缓冲。失败时返回 NO(此时模拟器仍可无声运行)。
- (BOOL)start;

// 关闭音频设备(会等待正在播放的缓冲结束)
- (void)stop;

// 当前在飞的缓冲数量, 等价 win32 前端的 wvPlayingNum, 用于帧率微调
- (ines_int_t)playingCount;

// 用静音缓冲把队列补到至少 count 个在飞(最多补满缓冲池), 返回补充后的在飞数量。
// 等价 win32 前端在 wvPlayingNum == 0 时用静音填满缓冲池的做法: 音频断流后靠它重建缓冲深度,
// 否则播放会一直停滞, 表现为声音断续甚至彻底无声。
- (ines_int_t)refillSilence:(ines_int_t)count;

// 提交一帧音频。pData 为 APU 输出的 8bit unsigned(0x80 为静音),
// len 为字节数。没有空闲缓冲时返回 -1(该帧音频被丢弃)。
- (ines_int_t)pushFrame:(const ines_byte_t*)pData length:(ines_int_t)len;

@end


#endif
