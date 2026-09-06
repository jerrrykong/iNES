#ifndef __W_PATTERN_TABLE_H__
#define __W_PATTERN_TABLE_H__


#ifdef __cplusplus
extern "C"
{
#endif


BOOL wPT_Create(HINSTANCE hInstance, HWND hParentWnd);

VOID wPT_Show(BOOL bShow);
BOOL wPT_IsShow();

VOID wPT_SetUpdate();

VOID wPT_Destroy();



#ifdef __cplusplus
};
#endif


#endif


