/* Real cross-thread window queues, with a deliberately busy target. Foreground
   permission and notification are modeled with SendNotifyMessage; the bounded
   WM_NULL handshake and receiving message loop are real. No foreground changes,
   hooks, input injection, Shell launches or user configuration writes. */
#include "../src/main.h"
AppConfig g_cfg;
int g_enabled=1;
HWND g_hWnd;
static HWND target;
static HANDLE targetReady;
static LONG targetActivated;
static int checks,failures;
void UI_Show(HWND w){(void)w;}
void Dashboard_Show(void){}
int Dashboard_IsVisible(void){return FALSE;}
void Dashboard_RecordUsage(int id){(void)id;}
void SC_ReportError(const wchar_t *op,DWORD error){(void)op;(void)error;failures++;}
void SC_LogDiagnostic(const wchar_t *message){(void)message;}
void SC_SetEnabled(int enabled){g_enabled=!!enabled;}
/* Restrict discovery to the fixture's own target; other OS APIs remain real. */
static BOOL TargetOnly(WNDENUMPROC callback,LPARAM param)
{callback(target,param);return TRUE;}
static BOOL RequestActivation(HWND window)
{return SendNotifyMessageW(window,WM_APP+910,0,0);}
static HWND ObservedForeground(void)
{return InterlockedCompareExchange(&targetActivated,0,0)?target:g_hWnd;}
#define EnumWindows TargetOnly
#define SetForegroundWindow RequestActivation
#define GetForegroundWindow ObservedForeground
#ifdef SC_ACTIVATION_BEFORE
#include "../design-review/gesture-dispatch-fix-2026-10-09/before-actions.c"
#else
#include "../src/actions.c"
#endif
#undef EnumWindows
#undef SetForegroundWindow
#undef GetForegroundWindow
static void Check(int condition,const char *text)
{checks++;printf("%s: %s\n",condition?"PASS":"FAIL",text);if(!condition)failures++;}
static LRESULT CALLBACK TargetProc(HWND window,UINT message,WPARAM wp,LPARAM lp)
{
    if(message==WM_APP+910){InterlockedExchange(&targetActivated,1);return 0;}
    if(message==WM_DESTROY){PostQuitMessage(0);return 0;}
    return DefWindowProcW(window,message,wp,lp);
}
static DWORD WINAPI TargetThread(void *unused)
{
    (void)unused;HINSTANCE instance=GetModuleHandleW(NULL);
    WNDCLASSW cls={0};cls.lpfnWndProc=TargetProc;cls.hInstance=instance;
    cls.lpszClassName=L"SC_ActivationTargetFixture";RegisterClassW(&cls);
    target=CreateWindowExW(WS_EX_TOOLWINDOW,cls.lpszClassName,L"Activation target",
        WS_POPUP,-10000,-10000,30,30,NULL,NULL,instance,NULL);
    if(!target){SetEvent(targetReady);return 1;}
    ShowWindow(target,SW_SHOWNOACTIVATE);
    SetEvent(targetReady);
    /* Accepting activation must not mean completion while this queue is busy. */
    Sleep(120);
    MSG message;
    while(GetMessageW(&message,NULL,0,0)>0){TranslateMessage(&message);DispatchMessageW(&message);}
    return 0;
}
static void PumpUntilThreadExits(HANDLE thread)
{
    ULONGLONG deadline=GetTickCount64()+2000;
    while(WaitForSingleObject(thread,0)==WAIT_TIMEOUT && GetTickCount64()<deadline){
        MSG message;while(PeekMessageW(&message,NULL,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}
        Sleep(2);
    }
}
int main(void)
{
    HINSTANCE instance=GetModuleHandleW(NULL);
    g_hWnd=CreateWindowExW(WS_EX_TOOLWINDOW,L"STATIC",L"Activation caller",WS_POPUP,
        -10000,-10000,30,30,NULL,NULL,instance,NULL);
    Check(IsWindow(g_hWnd),"fixture caller window exists without changing foreground");
    targetReady=CreateEventW(NULL,TRUE,FALSE,NULL);
    HANDLE thread=CreateThread(NULL,0,TargetThread,NULL,0,NULL);
    Check(thread && WaitForSingleObject(targetReady,2000)==WAIT_OBJECT_0 && IsWindow(target) &&
        GetWindowThreadProcessId(target,NULL)!=GetCurrentThreadId(),
        "target window exists on a separate native message queue");
    if(!thread || !target || failures)goto cleanup;
    wchar_t path[32768];GetModuleFileNameW(NULL,path,32768);
    ULONGLONG start=GetTickCount64();
#ifdef SC_ACTIVATION_BEFORE
    int success=Action_ActivateExe(path);
#else
    int success=Action_ActivateExe(path,NULL);
#endif
    ULONGLONG elapsed=GetTickCount64()-start;
    Check(success && InterlockedCompareExchange(&targetActivated,0,0),
        "activation returns success only after the native target handles the modeled notification, without another click");
    Check(elapsed<1000,"target confirmation has a bounded wait");
    printf("Native queue confirmation latency=%llu ms\n",(unsigned long long)elapsed);
cleanup:
    if(target)PostMessageW(target,WM_CLOSE,0,0);
    if(thread){PumpUntilThreadExits(thread);Check(WaitForSingleObject(thread,0)==WAIT_OBJECT_0,
        "fixture target thread exits cleanly");CloseHandle(thread);}
    if(targetReady)CloseHandle(targetReady);
    if(g_hWnd)DestroyWindow(g_hWnd);
    printf("Checks: %d; Failures: %d\n",checks,failures);
    return failures?1:0;
}
