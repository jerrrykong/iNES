#ifndef __W_REGISTER_H__
#define __W_REGISTER_H__


#ifdef __cplusplus
extern "C"
{
#endif


BOOL wReg_Create(HINSTANCE hInstance, HWND hParentWnd);

VOID wReg_Show(BOOL bShow);
BOOL wReg_IsShow();

VOID wReg_SetUpdate();

VOID wReg_Destroy();



#ifdef __cplusplus
};
#endif


#endif


