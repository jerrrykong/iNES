#ifndef __THREAD_H__
#define __THREAD_H__


#include "idef.h"


#ifdef WIN32
#include <windows.h>
#elif defined (linux)
#include <pthread.h>
#endif


#ifdef __cplusplus
extern "C"
{
#endif



#ifdef WIN32
typedef  DWORD     ines_thread_id_t;
#elif defined(linux)
typedef  pthread_t  ines_thread_id_t;
#endif

typedef struct _ines_mutex_  ines_mutex_t;
typedef struct _ines_thread_  ines_thread_t;


struct _ines_mutex_ 
{
#ifdef WIN32
	HANDLE  h_mutex;
#elif defined(linux)
	pthread_mutex_t  mutex_id;
#endif
};

typedef  int (*ines_thread_func)(void* ud); 

struct _ines_thread_ 
{
#ifdef WIN32
	HANDLE  h_thread;
#elif defined(linux)
	pthread_t  th_id;
#endif
	ines_thread_func  proc;
	void* ud;
	int   auto_detach;
};

#define MUTEX_BUSY      101


#ifdef WIN32
#define ines_thread_exit()  ExitThread(0)
#elif defined(linux)
#define ines_thread_exit()   pthread_exit(NULL)
#endif



int  ines_mutex_init(ines_mutex_t* mutex);
int  ines_mutex_fini(ines_mutex_t* mutex);

int  ines_mutex_lock(ines_mutex_t* mutex);
int  ines_mutex_try_lock(ines_mutex_t* mutex);

int  ines_mutex_unlock(ines_mutex_t* mutex);


ines_thread_id_t  ines_thread_getcurid();

int  ines_thread_init(ines_thread_t* th, ines_thread_func pf, void* ud);
int  ines_thread_fini(ines_thread_t* th);
int  ines_thread_start(ines_thread_t* th);
int  ines_thread_terminate(ines_thread_t* th);
int  ines_thread_wait(ines_thread_t* th);
int  ines_thread_set_auto_detach(ines_thread_t* th);

#ifdef __cplusplus
};
#endif



#endif

