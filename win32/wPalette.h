#ifndef __W_PALETTE_H__
#define __W_PALETTE_H__


#ifdef __cplusplus
extern "C"
{
#endif


BOOL wPal_Create(HINSTANCE hInstance, HWND hParentWnd);

VOID wPal_Show(BOOL bShow);
BOOL wPal_IsShow();

VOID wPal_SetUpdate();

VOID wPal_Destroy();



#ifdef __cplusplus
};
#endif


#endif


