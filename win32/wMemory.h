#ifndef __W_MEMORY_H__
#define __W_MEMORY_H__


#ifdef __cplusplus
extern "C"
{
#endif


BOOL wMemory_Create(HINSTANCE hInstance, HWND hParentWnd);

VOID wMemory_Show(BOOL bShow);
BOOL wMemory_IsShow();

VOID wMemory_SetUpdate();

VOID wMemory_Destroy();



#ifdef __cplusplus
};
#endif


#endif


