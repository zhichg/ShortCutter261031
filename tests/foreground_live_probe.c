/* Isolated real-foreground experiment. Three private windows/processes; hook
   consumes middle-clicks only over the private receiver. Automatic 45s exit.
   No clipboard, user configuration, registry, injected input or external apps. */
#include "../src/main.h"
AppConfig g_cfg; int g_enabled = 1; HWND g_hWnd;
void UI_Show(HWND window) { (void)window; }
void Dashboard_Show(void) { }
int Dashboard_IsVisible(void) { return FALSE; }
void Dashboard_RecordUsage(int id) { (void)id; }
void SC_ReportError(const wchar_t *operation, DWORD error) { (void)operation; (void)error; }
void SC_LogDiagnostic(const wchar_t *message) { (void)message; }
void SC_SetEnabled(int enabled) { g_enabled = !!enabled; }
#include "../src/actions.c"

static HWND receiver, target, status;
static HHOOK mouseHook;
static HHOOK keyboardHook;
static ActionRun liveRun;
static HANDLE liveWorker;
static int triggered;
static wchar_t reportPath[32768];
static const wchar_t *parentClass = L"SC_LiveForegroundParent";
static const wchar_t *receiverClass = L"SC_LiveForegroundReceiver";
static const wchar_t *targetClass = L"SC_LiveForegroundTarget";
static void Report(const wchar_t *text)
{
    FILE *file = _wfopen(reportPath,L"ab");
    if (file) {
        char utf8[4096]; int count = WideCharToMultiByte(CP_UTF8,0,text,-1,utf8,sizeof(utf8),NULL,NULL);
        if (count > 0) fwrite(utf8,1,(size_t)count-1,file);
        fclose(file);
    }
}
static DWORD WINAPI Compare(void *unused)
{
    (void)unused;
    wchar_t line[1024]; DWORD error = 0;
    DWORD worker = GetCurrentThreadId(), input = GetWindowThreadProcessId(g_hWnd,NULL);
    int oldResult = Action_ActivateWindow(NULL,target,500,&error);
    swprintf(line,1024,L"worker: result=%d error=%lu tid=%lu inputTid=%lu fg=%p target=%p origin=%p\r\n",
        oldResult,(unsigned long)error,(unsigned long)worker,(unsigned long)input,
        (void*)GetForegroundWindow(),(void*)target,(void*)receiver); Report(line);
    if (oldResult) { SetForegroundWindow(receiver); Sleep(100); }
    LASTINPUTINFO last = {sizeof(last),0};
    liveRun.inputValid = GetLastInputInfo(&last); liveRun.inputTime = last.dwTime;
    liveRun.origin = GetForegroundWindow();
    int newResult = Action_ActivateWindow(&liveRun,target,500,&error);
    swprintf(line,1024,L"input: result=%d error=%lu stage=%lu fg=%p target=%p origin=%p\r\n",
        newResult,(unsigned long)error,(unsigned long)liveRun.foregroundStage,
        (void*)GetForegroundWindow(),(void*)target,(void*)liveRun.origin); Report(line);
    PostMessageW(g_hWnd,WM_APP+950,newResult,oldResult);
    return 0;
}
static LRESULT CALLBACK ProbeHook(int code, WPARAM message, LPARAM parameter)
{
    if (code == HC_ACTION && !triggered && receiver &&
        (message == WM_MBUTTONDOWN || message == WM_MBUTTONUP)) {
        MSLLHOOKSTRUCT *event = (MSLLHOOKSTRUCT*)parameter;
        HWND at = WindowFromPoint(event->pt);
        if (GetAncestor(at,GA_ROOT) == receiver) {
            if (message == WM_MBUTTONUP) { triggered = 1; PostMessageW(g_hWnd,WM_APP+949,0,0); }
            return 1;
        }
    }
    return CallNextHookEx(mouseHook,code,message,parameter);
}
static LRESULT CALLBACK ProbeKeyHook(int code, WPARAM message, LPARAM parameter)
{
    if (code == HC_ACTION && !triggered && receiver && GetForegroundWindow() == receiver) {
        KBDLLHOOKSTRUCT *key = (KBDLLHOOKSTRUCT*)parameter;
        if (key->vkCode == VK_F8) {
            if (message == WM_KEYUP) { triggered = 1; PostMessageW(g_hWnd,WM_APP+949,0,0); }
            return 1;
        }
    }
    return CallNextHookEx(keyboardHook,code,message,parameter);
}
static LRESULT CALLBACK ProbeProc(HWND window, UINT message, WPARAM wp, LPARAM lp)
{
    if (window == g_hWnd) {
        if (message == WM_SC_FOREGROUND || message == WM_SC_FOREGROUNDGRANT)
            return Action_HandleForeground(message,wp,lp);
        if (message == WM_APP+949) {
            memset(&liveRun,0,sizeof(liveRun)); liveRun.replyTo = g_hWnd;
            liveRun.references = 2;
            liveRun.generation = g_actionGeneration; g_actionBusy = 2; g_activeLaunch = &liveRun;
            liveWorker = CreateThread(NULL,0,Compare,NULL,0,NULL);
            return 0;
        }
        if (message == WM_APP+950) {
            wchar_t text[256]; swprintf(text,256,L"Real foreground comparison: worker=%d, input=%d. Auto close in 3 seconds.",(int)lp,(int)wp);
            SetWindowTextW(status,text); SetTimer(g_hWnd,2,3000,NULL); return 0;
        }
        if (message == WM_TIMER && wp == 1) {
            receiver = FindWindowW(receiverClass,NULL); target = FindWindowW(targetClass,NULL);
            if (receiver && target && !mouseHook) {
                mouseHook = SetWindowsHookExW(WH_MOUSE_LL,ProbeHook,GetModuleHandleW(NULL),0);
                keyboardHook = SetWindowsHookExW(WH_KEYBOARD_LL,ProbeKeyHook,GetModuleHandleW(NULL),0);
                Report(mouseHook ? L"ready\r\n" : L"hook installation failed\r\n");
                SetWindowTextW(status,L"Ready. Middle-click once or press F8 inside the private receiver window.");
            }
            return 0;
        }
        if (message == WM_TIMER && wp == 2) { DestroyWindow(window); return 0; }
        if (message == WM_DESTROY) {
            if (mouseHook) UnhookWindowsHookEx(mouseHook);
            if (keyboardHook) UnhookWindowsHookEx(keyboardHook);
            if (receiver) PostMessageW(receiver,WM_CLOSE,0,0);
            if (target) PostMessageW(target,WM_CLOSE,0,0);
            if (liveWorker && WaitForSingleObject(liveWorker,0) == WAIT_OBJECT_0) CloseHandle(liveWorker);
            PostQuitMessage(0); return 0;
        }
    }
    if (message == WM_TIMER && wp == 3) { DestroyWindow(window); return 0; }
    if (message == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(window,message,wp,lp);
}
static void LaunchChild(const wchar_t *exe, const wchar_t *argument)
{
    wchar_t command[32768]; swprintf(command,32768,L"\"%ls\" %ls",exe,argument);
    STARTUPINFOW startup = {0}; startup.cb = sizeof(startup); startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_SHOWNOACTIVATE;
    PROCESS_INFORMATION process = {0};
    if (CreateProcessW(exe,command,NULL,NULL,FALSE,0,NULL,NULL,&startup,&process)) {
        CloseHandle(process.hThread); CloseHandle(process.hProcess);
    } else Report(L"child launch failed\r\n");
}
int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous, LPSTR arguments, int show)
{
    (void)previous; (void)show;
    int isReceiver = strstr(arguments,"--receiver") != NULL;
    int isTarget = strstr(arguments,"--target") != NULL;
    wchar_t exe[32768]; GetModuleFileNameW(NULL,exe,32768);
    lstrcpynW(reportPath,exe,32768); wchar_t *slash = wcsrchr(reportPath,L'\\');
    if (slash) lstrcpyW(slash+1,L"foreground-live-probe.log");
    WNDCLASSW cls = {0}; cls.hInstance = instance; cls.lpfnWndProc = ProbeProc;
    cls.hCursor = LoadCursorW(NULL,MAKEINTRESOURCEW(32512)); cls.hbrBackground = (HBRUSH)(COLOR_WINDOW+1);
    cls.lpszClassName = isReceiver ? receiverClass : isTarget ? targetClass : parentClass;
    RegisterClassW(&cls);
    HWND window = CreateWindowExW(isTarget ? WS_EX_TOOLWINDOW : 0,cls.lpszClassName,
        isReceiver ? L"SC Foreground Probe - Receiver" : isTarget ? L"SC Foreground Probe - Target" : L"SC Foreground Probe - Control",
        WS_OVERLAPPEDWINDOW,isReceiver ? 340 : isTarget ? 800 : 100,isReceiver ? 280 : 120,430,180,
        NULL,NULL,instance,NULL);
    HWND label = CreateWindowW(L"STATIC",isReceiver ? L"Middle-click or press F8 inside this private window once.\nThe probe compares actual worker and input-thread activation." : isTarget ? L"Private activation target. No clipboard or user files." : L"Preparing private foreground experiment...",
        WS_CHILD|WS_VISIBLE,20,30,385,95,window,NULL,instance,NULL);
    SetTimer(window,3,45000,NULL);
    ShowWindow(window,SW_SHOWNOACTIVATE);
    if (!isReceiver && !isTarget) {
        g_hWnd = window; status = label; Report(L"begin\r\n");
        LaunchChild(exe,L"--target"); LaunchChild(exe,L"--receiver"); SetTimer(window,1,100,NULL);
    }
    MSG message;
    while (GetMessageW(&message,NULL,0,0) > 0) { TranslateMessage(&message); DispatchMessageW(&message); }
    return 0;
}
