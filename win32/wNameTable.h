#ifndef __W_NAME_TABLE_H__
#define __W_NAME_TABLE_H__


#ifdef __cplusplus
extern "C"
{
#endif


BOOL wNT_Create(HINSTANCE hInstance, HWND hParentWnd);

VOID wNT_Show(BOOL bShow);
BOOL wNT_IsShow();

VOID wNT_SetUpdate();

VOID wNT_Destroy();



#ifdef __cplusplus
};
#endif


#endif


