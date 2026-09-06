#ifndef __W_VMEMORY_H__
#define __W_VMEMORY_H__


#ifdef __cplusplus
extern "C"
{
#endif


BOOL wVMemory_Create(HINSTANCE hInstance, HWND hParentWnd);

VOID wVMemory_Show(BOOL bShow);
BOOL wVMemory_IsShow();

VOID wVMemory_SetUpdate();

VOID wVMemory_Destroy();



#ifdef __cplusplus
};
#endif


#endif


