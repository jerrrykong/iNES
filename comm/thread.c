#include "thread.h"
#include "log.h"



int  ines_mutex_init(ines_mutex_t* mutex)
{
	if(mutex == NULL)
		return -1;
#ifdef WIN32
	mutex->h_mutex = CreateMutex(NULL, FALSE, NULL);
	if(mutex->h_mutex == NULL)
		return -1;
#elif defined(INES_POSIX)
	//always returns 0.
	pthread_mutex_init(&mutex->mutex_id, NULL);
#endif
	return 0;
}

int  ines_mutex_fini(ines_mutex_t* mutex)
{
	if(mutex == NULL)
		return -1;

#ifdef WIN32
	CloseHandle(mutex->h_mutex);
#elif defined(INES_POSIX)
	int ret = pthread_mutex_destroy(&mutex->mutex_id);
	if(EBUSY== ret)
	{
		INES_LOG(LOG_WAR, MOD_SYS, ISTR("Warning : the mutex $%p. can not be destoryed, because it currently locked."), mutex);
		return MUTEX_BUSY;
	}
#endif
	return 0;

}

int  ines_mutex_lock(ines_mutex_t* mutex)
{
#ifdef WIN32
	DWORD ret;
#elif defined(INES_POSIX)
	int ret;
#endif
	if(mutex == NULL)
		return -1;

#ifdef WIN32
	ret = WaitForSingleObject(mutex->h_mutex, INFINITE);
	if(ret != WAIT_OBJECT_0)
		return  -1;
#elif defined(INES_POSIX)
	ret = pthread_mutex_lock(&mutex->mutex_id);
	if(0 != ret)
	{
		return -1;
	}
#endif
	return 0;
}

int  ines_mutex_try_lock(ines_mutex_t* mutex)
{
#ifdef WIN32
	DWORD ret;
#elif defined(INES_POSIX)
	int ret;
#endif

	if(mutex == NULL)
		return -1;

#ifdef WIN32
	ret = WaitForSingleObject(mutex->h_mutex, 0);

	switch(ret)
	{
	case WAIT_OBJECT_0: return 0;
	case WAIT_TIMEOUT: return MUTEX_BUSY;
	default:
		return -1;
	}
#elif defined(INES_POSIX)
	ret = pthread_mutex_trylock(&mutex->mutex_id);
	if(0 != ret)
	{
		switch(ret)
		{
		case EBUSY:  return MUTEX_BUSY;
		case EINVAL:
		default : return  -1;
		}
	}
#endif
	return 0;
}

int  ines_mutex_unlock(ines_mutex_t* mutex)
{
	if(mutex == NULL)
		return -1;

#ifdef WIN32
	if(!ReleaseMutex(mutex->h_mutex))
		return -1;
#elif defined(INES_POSIX)
	int ret = pthread_mutex_unlock(&mutex->mutex_id);
	if(0 != ret)
	{
		switch(ret)
		{
		case EPERM:
		case EINVAL:
		default : return -1;
		}
	}
#endif
	return 0;
}

ines_thread_id_t  ines_thread_getcurid()
{
#ifdef WIN32
	return GetCurrentThreadId();
#elif defined(INES_POSIX)
	return pthread_self();
#endif
}

int  ines_thread_init(ines_thread_t* th, ines_thread_func pf, void* ud)
{
	if(th == NULL || pf == NULL)
		return -1;
#ifdef WIN32
	th->h_thread = NULL;
#elif defined(INES_POSIX)
	th->th_id = 0;
#endif
	th->proc = pf;
	th->ud = ud;
	th->auto_detach = 0;
	return 0;
}

int  ines_thread_fini(ines_thread_t* th)
{
	if(th == NULL)
		return -1;
#ifdef WIN32
	if( th->h_thread != NULL)
	{
		CloseHandle(th->h_thread);
		th->h_thread = NULL;
	}
#elif defined(INES_POSIX)
	if( th->th_id != 0)
	{
		
	}
#endif
	th->proc = NULL;
	th->ud  = NULL;
	th->auto_detach = 0;
	return 0;
}


#ifdef INES_POSIX 
#ifdef __ANDROID__ // android not support force cancel thread 
void handle_quit(int signo)
{
	pthread_exit(NULL);

}
#endif
#endif


#ifdef WIN32
static DWORD WINAPI ines_thread_proc(void* param)
#elif defined(INES_POSIX)
static void* ines_thread_proc(void* param)
#endif 
{
	int ret;
	ines_thread_t* self = (ines_thread_t*)param;

#ifdef INES_POSIX 
#if defined(__ANDROID__) // android not support force cancel thread 
	signal(SIGQUIT,handle_quit);
#elif defined(__APPLE__)
	// Darwin 的异步取消(PTHREAD_CANCEL_ASYNCHRONOUS)不可靠, 只允许延迟取消点。
	// 强制终止线程在 macOS 上不被支持(见 ines_thread_terminate), 线程退出统一
	// 走协作式停止标志 + pthread_join。
	pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
	pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);
#else
	// set the thread can be canceled.
	pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);

	// set the thread can be canceled at any time.
	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
#endif
#endif
	
	ret = (*self->proc)(self->ud);

	if(self->auto_detach) 
	{
#ifdef WIN32
		self->h_thread = NULL;
#elif defined(INES_POSIX)
		self->th_id = 0;
#endif
		self->auto_detach = 0;
	}

	(void)ret;

#ifdef WIN32
	return 0;
#elif defined(INES_POSIX)
	return NULL;
#endif

}

int  ines_thread_start(ines_thread_t* th)
{
#ifdef WIN32
	DWORD  thread_id;
#elif defined(INES_POSIX)
	int ret;
#endif

	if(th == NULL || th->proc == NULL)
		return -1;
#ifdef WIN32
	th->h_thread = CreateThread(NULL, 0,  ines_thread_proc, (void*)th  , 0, &thread_id);
	if(th->h_thread  == 0) {
		return -1;
	}
#elif defined(INES_POSIX)
	ret = pthread_create(&th->th_id, NULL, ines_thread_proc,   (void*)th);
	if(ret != 0)
	{
		INES_LOG(LOG_ERR, MOD_SYS, ISTR("pthread_create failed: %d\n"), ret);
		switch(ret)
		{
		case EAGAIN: 
		default: return -1;
		}
	}
#endif
	return 0;
}

int  ines_thread_terminate(ines_thread_t* th)
{
	int ret;
	if(th == NULL)
		return -1;
#ifdef WIN32
	if(th->h_thread == NULL)
		return -1; // not in running
	if(!TerminateThread(th->h_thread, 256))
		return -1;  // thread error
	CloseHandle(th->h_thread);
	th->h_thread = NULL;
#elif defined(INES_POSIX)
	if(th->th_id == 0)
		return -1;
#if defined(__APPLE__)
	// Darwin 不支持可靠的线程强制终止: pthread_cancel 只在取消点生效, 无法打断
	// 纯计算循环, 强行使用会掩盖资源释放逻辑。这里直接拒绝, 由调用方改用
	// 协作式停止标志(例如 libinescore.c 的 g_stop_flag) + ines_thread_wait()。
	INES_LOG(LOG_ERR, MOD_SYS, ISTR("ines_thread_terminate: not supported on macOS, "
		"use a cooperative stop flag + ines_thread_wait() instead.\n"));
	return -1;
#elif defined(__ANDROID__) // android not support force cancel thread 
	ret = pthread_kill(th->th_id, SIGQUIT);
#else
	ret = pthread_cancel(th->th_id);
#endif
	switch(ret)
	{
	case ESRCH: return -1;
	}
	if(!th->auto_detach)
		ines_thread_wait(th);
	th->th_id = 0;
#endif
	th->auto_detach = 0;
	ret = 0;

	return ret;
}

int  ines_thread_wait(ines_thread_t* th)
{
#ifdef WIN32
	DWORD ret;
#elif defined(INES_POSIX)
	int ret;
#endif

	if(th == NULL) 
		return -1;

#ifdef WIN32
	if (th->h_thread == NULL)
		return -1; // not in running
	ret = WaitForSingleObject(th->h_thread, INFINITE);
	if(ret != WAIT_OBJECT_0)
	{
		switch(ret)
		{
		case WAIT_FAILED: // throw exception::SystemException();
			return -1;
		}
	}
#elif defined(INES_POSIX)
	ret = pthread_join(th->th_id, NULL);
	if(ret != 0)
	{
		INES_LOG(LOG_ERR, MOD_SYS, ISTR("pthread_join failed: %d\n"), ret);
		switch(ret)
		{
		case ESRCH:  //  throw ThreadInvalid();
		case EINVAL:  // throw ThreadDetached();
		case EDEADLK: // throw ThreadWaitSelf();
			return -1;
		}
	}
#endif
	th->auto_detach = 0;
	
	return 0;
}

int  ines_thread_set_auto_detach(ines_thread_t* th)
{
#if defined(INES_POSIX)
	int ret ;
	if(th == NULL)
		return -1;
	if(th->th_id == 0) // throw ThreadIsNotRunning();
		return -1;
	ret = pthread_detach(th->th_id);
	if(ret != 0)
	{
		INES_LOG(LOG_ERR, MOD_SYS, ISTR("pthread_detach failed: %d\n"), ret);
		switch(ret)
		{
		case ESRCH:  // throw ThreadInvalid();
			return -1;
		}
	}
	th->auto_detach = 1;
#endif
	return 0;
}


