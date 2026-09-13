// 音频输出: AudioQueue + 缓冲池。
//
// 模型与 win32 前端的 waveOut 一一对应:
//   * 预分配 INES_AUDIO_BUFFER_NUM 个缓冲;
//   * 模拟线程每产出一帧音频, 就找一个空闲缓冲写进去并提交;
//   * 缓冲播放完毕后由 AudioQueueOutputCallback 回收(等价 MM_WOM_DONE)。
//
// 格式转换: 核心 APU 输出 8bit unsigned(0x80 为静音), 而 CoreAudio 的 8bit
//           LinearPCM 是 signed, 音质也较差, 因此统一转换为 SInt16。

#import "iNESAudio.h"

#import "../comm/log.h"

#import <AudioToolbox/AudioToolbox.h>


typedef struct _ines_audio_slot_
{
	AudioQueueBufferRef  buffer;
	BOOL                 inQueue;
} ines_audio_slot_t;


static void iNESAudioOutputCallback(void* pUserData, AudioQueueRef pQueue, AudioQueueBufferRef pBuffer);


@interface iNESAudio ()
// 由 AudioQueue 的回调线程调用
- (void)bufferDidFinishPlaying:(AudioQueueBufferRef)pBuffer queue:(AudioQueueRef)pQueue;
@end


@implementation iNESAudio
{
	AudioQueueRef        _queue;
	ines_audio_slot_t    _slots[INES_AUDIO_BUFFER_NUM];
	ines_int_t           _playing;      // 在飞的缓冲数
	NSLock*              _lock;
	BOOL                 _started;
	SInt16*              _convertBuffer; // 8bit unsigned -> SInt16 的临时缓冲

	// ---- 临时诊断(定位"运行一段时间后无声", 结论明确后整块移除) ----
	ines_int_t           _diagFrames;    // pushFrame 调用次数
	ines_int_t           _diagNoslot;    // 无空闲缓冲次数
	ines_int_t           _diagNoStart;   // 设备未开启次数
	ines_int_t           _diagEnqFail;   // 入队失败次数
	ines_int_t           _diagUnderrun;  // 投喂时在飞缓冲为 0 的次数(播放停顿)
	ines_int_t           _diagNotRun;    // 队列未运行(IsRunning=0)的次数
	ines_int_t           _diagRecycled;  // 回调成功回收次数
	ines_int_t           _diagUnknown;   // 回调收到未知缓冲次数
	ines_int_t           _diagMaxLen;    // 观察到的一帧最大字节数
	ines_int_t           _diagBadState;  // 在飞缓冲数达到上限的累计次数
}

- (instancetype)init
{
	self = [super init];
	if(self == nil)
		return nil;

	_lock          = [[NSLock alloc] init];
	_queue         = NULL;
	_playing       = 0;
	_started       = NO;
	_convertBuffer = (SInt16*)calloc(INES_AUDIO_MAX_FRAME_LEN, sizeof(SInt16));

	_diagFrames = _diagNoslot = _diagNoStart = _diagEnqFail = _diagUnderrun = 0;
	_diagNotRun = _diagRecycled = _diagUnknown = _diagMaxLen = _diagBadState = 0;

	if(_convertBuffer == NULL)
	{
		INES_LOG(LOG_ERR, MOD_SYS, ISTR("audio: allocate convert buffer Failed!\n"));
		return nil;
	}

	memset(_slots, 0, sizeof(_slots));

	return self;
}

- (void)dealloc
{
	[self stop];
	free(_convertBuffer);
}

- (ines_int_t)playingCount
{
	ines_int_t  count;

	[_lock lock];
	count = _playing;
	[_lock unlock];

	return count;
}

- (double)playedSamples
{
	AudioTimeStamp  ts;
	double          t = 0.0;

	[_lock lock];
	if(_started && _queue != NULL)
	{
		memset(&ts, 0, sizeof(ts));
		if(AudioQueueGetCurrentTime(_queue, NULL, &ts, NULL) == noErr)
			t = ts.mSampleTime;
	}
	[_lock unlock];

	return t;
}

- (ines_int_t)refillSilence:(ines_int_t)count
{
	AudioQueueBufferRef  pending[INES_AUDIO_BUFFER_NUM];
	ines_int_t           pendingNum = 0;
	ines_int_t           i;
	ines_int_t           playing;
	UInt32               bytes = (UInt32)(INES_AUDIO_SAMPLE_RATE / 60) * (UInt32)sizeof(SInt16);

	// AudioQueueEnqueueBuffer 在队列饱和时会阻塞等待缓冲播放完毕, 而此时回调
	// bufferDidFinishPlaying: 正需要 _lock 才能回收缓冲 —— 持锁调用就会被长时间卡住
	// (实测把帧周期从 16.7ms 拖到 24ms, 帧率跌到 41fps, 导致每帧都欠载)。
	// 因此锁内只做状态准备, 入队一律放到锁外, 与 win32 前端在无锁主循环里调用
	// waveOutWrite 的语义一致。
	[_lock lock];

	if(_started && _queue != NULL)
	{
		for(i = 0; i < INES_AUDIO_BUFFER_NUM && _playing < count; i++)
		{
			if(_slots[i].inQueue)
				continue;

			// SInt16 的 0 即静音
			memset(_slots[i].buffer->mAudioData, 0, bytes);
			_slots[i].buffer->mAudioDataByteSize = bytes;
			_slots[i].inQueue = YES;
			_playing++;

			pending[pendingNum++] = _slots[i].buffer;
		}
	}

	playing = _playing;

	[_lock unlock];

	for(i = 0; i < pendingNum; i++)
	{
		if(AudioQueueEnqueueBuffer(_queue, pending[i], 0, NULL) != noErr)
		{
			ines_int_t  j;

			[_lock lock];
			for(j = 0; j < INES_AUDIO_BUFFER_NUM; j++)
			{
				if(_slots[j].buffer == pending[i] && _slots[j].inQueue)
				{
					_slots[j].inQueue = NO;
					if(_playing > 0)
						_playing--;
					break;
				}
			}
			playing = _playing;
			[_lock unlock];
		}
	}

	return playing;
}


#pragma mark - 设备开关

- (BOOL)start
{
	AudioStreamBasicDescription  format;
	OSStatus                     status;
	int                          i;

	if(_started)
		return YES;

	memset(&format, 0, sizeof(format));
	format.mSampleRate       = INES_AUDIO_SAMPLE_RATE;
	format.mFormatID         = kAudioFormatLinearPCM;
	format.mFormatFlags      = kLinearPCMFormatFlagIsSignedInteger | kLinearPCMFormatFlagIsPacked;
	format.mFramesPerPacket  = 1;
	format.mChannelsPerFrame = 1;
	format.mBitsPerChannel   = 16;
	format.mBytesPerFrame    = 2;
	format.mBytesPerPacket   = 2;

	status = AudioQueueNewOutput(&format, iNESAudioOutputCallback, (__bridge void*)self,
								 NULL, NULL, 0, &_queue);
	if(status != noErr || _queue == NULL)
	{
		INES_LOG(LOG_ERR, MOD_SYS, ISTR("audio: AudioQueueNewOutput Failed!(status=%d)\n"), (int)status);
		_queue = NULL;
		return NO;
	}

	[_lock lock];
	memset(_slots, 0, sizeof(_slots));
	_playing = 0;
	[_lock unlock];

	for(i = 0; i < INES_AUDIO_BUFFER_NUM; i++)
	{
		status = AudioQueueAllocateBuffer(_queue, INES_AUDIO_BUFFER_SAMPLES * (UInt32)sizeof(SInt16),
										  &_slots[i].buffer);
		if(status != noErr)
		{
			INES_LOG(LOG_ERR, MOD_SYS, ISTR("audio: AudioQueueAllocateBuffer Failed!(status=%d)\n"), (int)status);
			AudioQueueDispose(_queue, true);
			_queue = NULL;
			return NO;
		}
		_slots[i].inQueue = NO;
	}

	// 预填充静音, 建立与 win32 前端相近的初始延迟(约 4 帧)
	{
		UInt32  prerollBytes = (UInt32)(INES_AUDIO_SAMPLE_RATE / 60) * (UInt32)sizeof(SInt16);

		for(i = 0; i < INES_AUDIO_PREROLL_COUNT && i < INES_AUDIO_BUFFER_NUM; i++)
		{
			memset(_slots[i].buffer->mAudioData, 0, prerollBytes);
			_slots[i].buffer->mAudioDataByteSize = prerollBytes;
			_slots[i].inQueue = YES;
			status = AudioQueueEnqueueBuffer(_queue, _slots[i].buffer, 0, NULL);
			if(status != noErr)
			{
				_slots[i].inQueue = NO;
				INES_LOG(LOG_WAR, MOD_SYS, ISTR("audio: preroll enqueue Failed!(status=%d)\n"), (int)status);
				break;
			}
			_playing++;
		}
	}

	status = AudioQueueStart(_queue, NULL);
	if(status != noErr)
	{
		INES_LOG(LOG_ERR, MOD_SYS, ISTR("audio: AudioQueueStart Failed!(status=%d)\n"), (int)status);
		AudioQueueDispose(_queue, true);
		_queue = NULL;
		return NO;
	}

	_started = YES;
	INES_LOG(LOG_NTY, MOD_SYS, ISTR("audio: started, %dHz mono SInt16, %d buffers.\n"),
			 INES_AUDIO_SAMPLE_RATE, INES_AUDIO_BUFFER_NUM);

	return YES;
}

- (void)stop
{
	AudioQueueRef  queue;

	if(!_started && _queue == NULL)
		return;

	// 先置位再释放锁: AudioQueueStop(true) 会等待回调返回, 而回调需要拿 _lock,
	// 如果持锁等待就会死锁。
	_started = NO;
	[_lock lock];
	_playing = 0;
	[_lock unlock];

	queue  = _queue;
	_queue = NULL;

	if(queue != NULL)
	{
		AudioQueueStop(queue, true);
		AudioQueueDispose(queue, true);
	}
}


#pragma mark - 投喂

- (ines_int_t)pushFrame:(const ines_byte_t*)pData length:(ines_int_t)len
{
	ines_int_t           i;
	ines_int_t           slot = -1;
	OSStatus             status;
	AudioQueueBufferRef  buffer = NULL;

	if(pData == NULL || len <= 0)
		return -1;

	if(len > INES_AUDIO_MAX_FRAME_LEN)
		len = INES_AUDIO_MAX_FRAME_LEN;

	// 单帧长度不得超过单个缓冲的容量, 否则 memcpy 会越界写坏 AudioQueue 的缓冲内存
	if(len > INES_AUDIO_BUFFER_SAMPLES)
		len = INES_AUDIO_BUFFER_SAMPLES;

	// 8bit unsigned(0x80 为静音) -> SInt16
	for(i = 0; i < len; i++)
		_convertBuffer[i] = (SInt16)(((int)pData[i] - 128) << 8);

	// ---- 临时诊断(定位"运行一段时间后无声", 结论明确后整块移除) ----
	_diagFrames++;
	if(len > _diagMaxLen)
		_diagMaxLen = len;

	// 队列健康检查: AudioQueue 在系统音频服务重启、输出设备切换等情况下会静默停止,
	// 停止后回调不再触发, 缓冲会永久停留在"在飞"状态, 表现为彻底静音且无法自愈。
	if((_diagFrames % 60) == 0)
	{
		UInt32  running = 0;
		UInt32  size    = (UInt32)sizeof(running);

		if(AudioQueueGetProperty(_queue, kAudioQueueProperty_IsRunning, &running, &size) == noErr && running == 0)
		{
			_diagNotRun++;

			[_lock lock];
			for(i = 0; i < INES_AUDIO_BUFFER_NUM; i++)
				_slots[i].inQueue = NO;
			_playing = 0;
			[_lock unlock];

			INES_LOG(LOG_WAR, MOD_SYS, ISTR("audio: queue NOT running, restart. frames=%d noslot=%d\n"),
					 (int)_diagFrames, (int)_diagNoslot);

			if(AudioQueueStart(_queue, NULL) != noErr)
				INES_LOG(LOG_ERR, MOD_SYS, ISTR("audio: restart Failed!\n"));
		}
	}
	// ---- 临时诊断结束 ----

	[_lock lock];

	if(!_started || _queue == NULL)
	{
		_diagNoStart++;
		[_lock unlock];
		return -1;
	}

	// 上一帧音频已播完(播放出现空档), 说明投喂速度跟不上
	if(_playing <= 0)
		_diagUnderrun++;

	for(i = 0; i < INES_AUDIO_BUFFER_NUM; i++)
	{
		if(!_slots[i].inQueue)
		{
			slot = i;
			break;
		}
	}

	if(slot < 0)
	{
		// 缓冲全部在飞: 丢弃本帧音频(说明帧产出过快)
		_diagNoslot++;
		if(_playing >= INES_AUDIO_BUFFER_NUM)
			_diagBadState++;

		// ---- 临时诊断 ----
		if((_diagNoslot % 600) == 1)
			INES_LOG(LOG_WAR, MOD_SYS, ISTR("audio diag: no free slot. frames=%d playing=%d recycled=%d\n"),
					 (int)_diagFrames, (int)_playing, (int)_diagRecycled);
		// ---- 临时诊断结束 ----

		[_lock unlock];
		return -1;
	}

	{
		UInt32  bytes = (UInt32)len * (UInt32)sizeof(SInt16);

		memcpy(_slots[slot].buffer->mAudioData, _convertBuffer, bytes);
		_slots[slot].buffer->mAudioDataByteSize = bytes;
		_slots[slot].inQueue = YES;
		_playing++;

		buffer = _slots[slot].buffer;
	}

	[_lock unlock];

	// 入队必须在锁外执行(理由见 refillSilence:): 队列饱和时它会阻塞等待缓冲播放完毕,
	// 而回调回收缓冲需要 _lock, 持锁调用会把模拟线程一起卡住。
	status = AudioQueueEnqueueBuffer(_queue, buffer, 0, NULL);
	if(status != noErr)
	{
		[_lock lock];
		_slots[slot].inQueue = NO;
		_playing--;
		[_lock unlock];

		_diagEnqFail++;
		INES_LOG(LOG_WAR, MOD_SYS, ISTR("audio: enqueue Failed!(status=%d)\n"), (int)status);
		return -1;
	}

	// ---- 临时诊断: 每 120 帧(约 2 秒)输出一次状态 ----
	[_lock lock];

	if((_diagFrames % 120) == 0)
	{
		UInt32          running = 0;
		UInt32          rsize   = (UInt32)sizeof(running);
		AudioTimeStamp  ts;

		memset(&ts, 0, sizeof(ts));
		AudioQueueGetProperty(_queue, kAudioQueueProperty_IsRunning, &running, &rsize);
		AudioQueueGetCurrentTime(_queue, NULL, &ts, NULL);

		INES_LOG(LOG_NTY, MOD_SYS,
				 ISTR("audio diag: frames=%d playing=%d noslot=%d underrun=%d recycled=%d unknown=%d ")
				 ISTR("enqfail=%d notrun=%d nostart=%d maxlen=%d running=%d sampleTime=%.0f\n"),
				 (int)_diagFrames, (int)_playing, (int)_diagNoslot, (int)_diagUnderrun,
				 (int)_diagRecycled, (int)_diagUnknown, (int)_diagEnqFail, (int)_diagNotRun,
				 (int)_diagNoStart, (int)_diagMaxLen, (int)running, ts.mSampleTime);
	}
	// ---- 临时诊断结束 ----

	[_lock unlock];

	return 0;
}


#pragma mark - 回调

- (void)bufferDidFinishPlaying:(AudioQueueBufferRef)pBuffer queue:(AudioQueueRef)pQueue
{
	ines_int_t  i;
	BOOL        hit = NO;

	if(pBuffer == NULL)
		return;

	[_lock lock];
	for(i = 0; i < INES_AUDIO_BUFFER_NUM; i++)
	{
		if(_slots[i].buffer == pBuffer && _slots[i].inQueue)
		{
			_slots[i].inQueue = NO;
			if(_playing > 0)
				_playing--;
			hit = YES;
			break;
		}
	}

	// ---- 临时诊断 ----
	if(hit)
		_diagRecycled++;
	else
		_diagUnknown++;
	// ---- 临时诊断结束 ----

	[_lock unlock];
}

@end


static void iNESAudioOutputCallback(void* pUserData, AudioQueueRef pQueue, AudioQueueBufferRef pBuffer)
{
	iNESAudio*  audio = (__bridge iNESAudio*)pUserData;

	[audio bufferDidFinishPlaying:pBuffer queue:pQueue];
}
