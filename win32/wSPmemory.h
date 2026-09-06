#ifndef __W_SPMEMORY_H__
#define __W_SPMEMORY_H__


#ifdef __cplusplus
extern "C"
{
#endif


BOOL wSPMemory_Create(HINSTANCE hInstance, HWND hParentWnd);

VOID wSPMemory_Show(BOOL bShow);
BOOL wSPMemory_IsShow();

VOID wSPMemory_SetUpdate();

VOID wSPMemory_Destroy();



#ifdef __cplusplus
};
#endif


#endif


