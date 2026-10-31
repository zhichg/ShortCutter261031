/* Real input-thread dispatch and target message queues. Foreground permission
   is modeled: only the designated input thread may issue activation or grant.
   No global hooks, injected input, real foreground changes or user INI writes. */
#include "../src/main.h"

AppConfig g_cfg;
int g_enabled = 1;
HWND g_hWnd;
static HWND target;
static HANDLE targetReady;
static DWORD inputThread;
static volatile LONG targetActivated, activationCalls, grantCalls;
static int checks, failures, cancelBeforeDispatch, activatedOnInput, grantedOnInput;
static int workerSuccess, workerGrant;
static ULONGLONG workerElapsed;

void UI_Show(HWND window) { (void)window; }
void Dashboard_Show(void) { }
int Dashboard_IsVisible(void) { return FALSE; }
void Dashboard_RecordUsage(int id) { (void)id; }
void SC_ReportError(const wchar_t *operation, DWORD error)
{ (void)operation; (void)error; failures++; }
void SC_LogDiagnostic(const wchar_t *message) { (void)message; }
void SC_SetEnabled(int enabled) { g_enabled = !!enabled; }
static BOOL TargetOnly(WNDENUMPROC callback, LPARAM parameter)
{ callback(target,parameter); return TRUE; }
static HWND ObservedForeground(void)
{ return InterlockedCompareExchange(&targetActivated,0,0) ? target : g_hWnd; }
static BOOL InputState(PLASTINPUTINFO info) { info->dwTime = 42; return TRUE; }
static BOOL ActivateOnInput(HWND window)
{
    InterlockedIncrement(&activationCalls);
    activatedOnInput = GetCurrentThreadId() == inputThread;
    if (!activatedOnInput) return FALSE;
    return SendNotifyMessageW(window,WM_APP+910,0,0);
}
static BOOL GrantOnInput(DWORD pid)
{
    InterlockedIncrement(&grantCalls);
    grantedOnInput = GetCurrentThreadId() == inputThread && pid == GetCurrentProcessId();
    return grantedOnInput;
}
#define EnumWindows TargetOnly
#define GetForegroundWindow ObservedForeground
#define GetLastInputInfo InputState
#define SetForegroundWindow ActivateOnInput
#define AllowSetForegroundWindow GrantOnInput
#include "../src/actions.c"
#undef EnumWindows
#undef GetForegroundWindow
#undef GetLastInputInfo
#undef SetForegroundWindow
#undef AllowSetForegroundWindow

static void Check(int condition, const char *description)
{ checks++; printf("%s: %s\n",condition ? "PASS" : "FAIL",description); if (!condition) failures++; }
static LRESULT CALLBACK InputProc(HWND window, UINT message, WPARAM wp, LPARAM lp)
{
    if (message == WM_SC_FOREGROUND || message == WM_SC_FOREGROUNDGRANT) {
        if (cancelBeforeDispatch) { cancelBeforeDispatch = 0; Action_Cancel(); }
        return Action_HandleForeground(message,wp,lp);
    }
    return DefWindowProcW(window,message,wp,lp);
}
static LRESULT CALLBACK TargetProc(HWND window, UINT message, WPARAM wp, LPARAM lp)
{
    if (message == WM_APP+910) { InterlockedExchange(&targetActivated,1); return 0; }
    if (message == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(window,message,wp,lp);
}
static DWORD WINAPI TargetThread(void *unused)
{
    (void)unused;
    HINSTANCE instance = GetModuleHandleW(NULL);
    WNDCLASSW cls = {0}; cls.hInstance = instance; cls.lpfnWndProc = TargetProc;
    cls.lpszClassName = L"SC_InputThreadTargetFixture"; RegisterClassW(&cls);
    target = CreateWindowExW(WS_EX_TOOLWINDOW,cls.lpszClassName,L"Input queue target",
        WS_POPUP,-10000,-10000,30,30,NULL,NULL,instance,NULL);
    ShowWindow(target,SW_SHOWNOACTIVATE); SetEvent(targetReady);
    Sleep(120);
    MSG message;
    while (GetMessageW(&message,NULL,0,0) > 0) { TranslateMessage(&message); DispatchMessageW(&message); }
    return 0;
}
static DWORD WINAPI LaunchWorker(void *parameter)
{
    ActionRun *run = parameter;
    DWORD error;
    ULONGLONG start = GetTickCount64();
    workerGrant = Action_RequestInputThread(run,NULL,GetCurrentProcessId(),200,&error);
    wchar_t path[32768]; GetModuleFileNameW(NULL,path,32768);
    workerSuccess = Action_ActivateExe(path,run);
    workerElapsed = GetTickCount64()-start;
    return 0;
}
static int WaitWithPump(HANDLE thread)
{
    ULONGLONG deadline = GetTickCount64()+3000;
    while (WaitForSingleObject(thread,0) == WAIT_TIMEOUT && GetTickCount64() < deadline) {
        MSG message;
        while (PeekMessageW(&message,NULL,0,0,PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
        Sleep(2);
    }
    return WaitForSingleObject(thread,0) == WAIT_OBJECT_0;
}
static void PrepareRun(ActionRun *run)
{
    memset(run,0,sizeof(*run)); run->replyTo = g_hWnd; run->origin = g_hWnd;
    run->references = 2; /* Fixture borrows stack storage, not a launch allocation. */
    run->generation = InterlockedCompareExchange(&g_actionGeneration,0,0);
    run->inputValid = 1; run->inputTime = 42;
    g_actionBusy = 2; g_activeLaunch = run;
}
int main(void)
{
    inputThread = GetCurrentThreadId();
    HINSTANCE instance = GetModuleHandleW(NULL);
    WNDCLASSW cls = {0}; cls.hInstance = instance; cls.lpfnWndProc = InputProc;
    cls.lpszClassName = L"SC_InputThreadCallerFixture"; RegisterClassW(&cls);
    g_hWnd = CreateWindowExW(WS_EX_TOOLWINDOW,cls.lpszClassName,L"Input queue caller",
        WS_POPUP,-10000,-10000,30,30,NULL,NULL,instance,NULL);
    targetReady = CreateEventW(NULL,TRUE,FALSE,NULL);
    HANDLE receiver = CreateThread(NULL,0,TargetThread,NULL,0,NULL);
    Check(g_hWnd && receiver && WaitForSingleObject(targetReady,2000) == WAIT_OBJECT_0 && IsWindow(target),
        "private input and target windows exist on native message queues");
    if (failures) goto cleanup;
    ActionRun run; PrepareRun(&run);
    DWORD workerThread;
    HANDLE worker = CreateThread(NULL,0,LaunchWorker,&run,0,&workerThread);
    Check(worker && workerThread != inputThread && WaitWithPump(worker),
        "launch worker completes while the input thread pumps real requests");
    if (worker) CloseHandle(worker);
    Check(workerGrant && grantedOnInput && grantCalls == 1,"foreground grant runs on the input thread");
    Check(workerSuccess && activatedOnInput && activationCalls == 1,"window activation runs on the input thread, not the worker");
    Check(targetActivated && workerElapsed >= 70 && workerElapsed < 1000,
        "worker waits for the real busy target queue without blocking the input thread");
    Check(Action_HandleForeground(WM_SC_FOREGROUND,(WPARAM)target,(LPARAM)run.generation) == 0 && activationCalls == 1,
        "a repeated message without a pending worker request cannot activate a window");
    InterlockedExchange(&targetActivated,0); PrepareRun(&run); cancelBeforeDispatch = 1;
    worker = CreateThread(NULL,0,LaunchWorker,&run,0,NULL);
    Check(worker && WaitWithPump(worker),"cancelled native dispatch completes without a deadlock");
    if (worker) CloseHandle(worker);
    Check(!workerGrant && !workerSuccess && activationCalls == 1 && grantCalls == 1,
        "cancellation rejects both stale grant and activation requests before calling either API");
cleanup:
    Action_Cancel();
    if (target) PostMessageW(target,WM_CLOSE,0,0);
    if (receiver) { Check(WaitWithPump(receiver),"target message thread exits cleanly"); CloseHandle(receiver); }
    if (targetReady) CloseHandle(targetReady);
    if (g_hWnd) DestroyWindow(g_hWnd);
    printf("Checks: %d; Failures: %d\n",checks,failures);
    return failures ? 1 : 0;
}
