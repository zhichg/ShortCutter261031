/* Private cross-process foreground comparison, auto-exits after 90 seconds.
   F8 is delivered normally to the receiver; it then asks the background broker
   to activate the target. F24 is injected only while both endpoints are ours. */
#include "../src/main.h"
AppConfig g_cfg; int g_enabled = 1; HWND g_hWnd;
void UI_Show(HWND window) { (void)window; }
void Dashboard_Show(void) { }
int Dashboard_IsVisible(void) { return 0; }
void Dashboard_RecordUsage(int id) { (void)id; }
void SC_ReportError(const wchar_t *text, DWORD error) { (void)text; (void)error; }
void SC_LogDiagnostic(const wchar_t *text) { (void)text; }
void SC_SetEnabled(int enabled) { g_enabled = !!enabled; }
#include "../src/actions.c"
static HWND control, receiver, target, label;
static int role, started, attempted, ordinaryF24;
static ActionRun liveRun;
static wchar_t reportPath[32768];
static const wchar_t *classes[] = { L"SC_HotkeyProbe_Control", L"SC_HotkeyProbe_Receiver", L"SC_HotkeyProbe_Target" };
static void Report(const wchar_t *format, ...)
{
    wchar_t line[1024]; va_list args; va_start(args,format);
    vswprintf(line,1024,format,args); va_end(args);
    FILE *file = _wfopen(reportPath,L"ab");
    if (!file) return;
    char text[4096]; int size = WideCharToMultiByte(CP_UTF8,0,line,-1,text,sizeof(text),NULL,NULL);
    if (size > 0) fwrite(text,1,(size_t)size-1,file);
    fclose(file);
}
static DWORD WINAPI Compare(void *unused)
{
    (void)unused;
    DWORD error = 0;
    int result = Action_ActivateWindow(&liveRun,target,500,&error);
    Report(L"production result=%d error=%lu hotkey=%ld injected=%u hotkeyError=%lu fg=%p target=%p\r\n",
        result,(unsigned long)error,liveRun.hotkeyState,liveRun.hotkeyInjected,(unsigned long)liveRun.hotkeyError,
        (void*)GetForegroundWindow(),(void*)target);
    PostMessageW(control,WM_APP+922,result,0);
    return 0;
}
static LRESULT CALLBACK Procedure(HWND window, UINT msg, WPARAM wp, LPARAM lp)
{
    if (!role && (msg == WM_SC_FOREGROUND || msg == WM_SC_FOREGROUNDGRANT))
        return Action_HandleForeground(msg,wp,lp);
    if (role == 1 && (msg == WM_KEYDOWN || msg == WM_KEYUP) && wp == VK_F24) {
        ordinaryF24++; Report(L"receiver ordinary F24 message=%u count=%d\r\n",msg,ordinaryF24);
    }
    if (role == 1 && msg == WM_KEYUP && wp == VK_F8) {
        control = FindWindowW(classes[0],NULL);
        if (control) PostMessageW(control,WM_APP+920,0,0);
        return 0;
    }
    if (!role && msg == WM_APP+920 && !started) { started = 1; SetTimer(window,2,200,NULL); return 0; }
    if (!role && msg == WM_TIMER && wp == 2) {
        KillTimer(window,2); receiver = FindWindowW(classes[1],NULL); target = FindWindowW(classes[2],NULL);
        if (!receiver || !target || GetForegroundWindow() != receiver) { Report(L"aborted: receiver is not foreground\r\n"); return 0; }
        int direct = SetForegroundWindow(target);
        Report(L"direct result=%d fg=%p target=%p receiver=%p\r\n",direct,(void*)GetForegroundWindow(),(void*)target,(void*)receiver);
        if (direct) { SetWindowTextW(label,L"Direct already succeeded; this run did not reproduce denied permission."); return 0; }
        memset(&liveRun,0,sizeof(liveRun)); liveRun.references = 2;
        liveRun.replyTo = window; liveRun.origin = receiver; liveRun.generation = g_actionGeneration;
        liveRun.inputSerial = g_actionInputSerial; g_activeLaunch = &liveRun; g_actionBusy = 2;
        HANDLE worker = CreateThread(NULL,0,Compare,NULL,0,NULL); if (worker) CloseHandle(worker);
        return 0;
    }
    if (!role && msg == WM_HOTKEY) {
        Report(L"system hotkey id=%u modifiers=%u key=%u\r\n",(unsigned)wp,LOWORD(lp),HIWORD(lp));
        Action_HandleForegroundHotkey(wp,lp);
        return 0;
    }
    if (!role && msg == WM_APP+922) {
        attempted = (int)wp;
        SetTimer(window,3,500,NULL);
        return 0;
    }
    if (!role && msg == WM_TIMER && wp == 3) {
        KillTimer(window,3);
        int result = attempted && GetForegroundWindow() == target;
        Report(L"confirmed=%d foreground=%p target=%p\r\n",result,(void*)GetForegroundWindow(),(void*)target);
        SetWindowTextW(label,result ? L"Verified: direct denied; real WM_HOTKEY activation succeeded." : L"Activation was not confirmed. See the local report.");
        SetTimer(window,4,3000,NULL); return 0;
    }
    if (msg == WM_TIMER && (wp == 1 || wp == 4)) { DestroyWindow(window); return 0; }
    if (msg == WM_DESTROY) {
        if (!role) Action_Cancel();
        if (!role) {
            HWND child = FindWindowW(classes[1],NULL); if (child) PostMessageW(child,WM_CLOSE,0,0);
            child = FindWindowW(classes[2],NULL); if (child) PostMessageW(child,WM_CLOSE,0,0);
        }
        PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(window,msg,wp,lp);
}
static void Child(const wchar_t *exe, const wchar_t *argument)
{
    wchar_t command[32768]; swprintf(command,32768,L"\"%ls\" %ls",exe,argument);
    STARTUPINFOW startup = {0}; startup.cb = sizeof(startup); startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_SHOWNOACTIVATE; PROCESS_INFORMATION process = {0};
    if (CreateProcessW(exe,command,NULL,NULL,FALSE,0,NULL,NULL,&startup,&process)) {
        CloseHandle(process.hThread); CloseHandle(process.hProcess);
    }
}
int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous, LPSTR arguments, int show)
{
    (void)previous; (void)show;
    role = strstr(arguments,"--receiver") ? 1 : strstr(arguments,"--target") ? 2 : 0;
    wchar_t exe[32768]; GetModuleFileNameW(NULL,exe,32768); lstrcpynW(reportPath,exe,32768);
    wchar_t *slash = wcsrchr(reportPath,L'\\'); if (slash) lstrcpyW(slash+1,L"foreground-hotkey-probe.log");
    WNDCLASSW cls = {0}; cls.hInstance = instance; cls.lpfnWndProc = Procedure;
    cls.hCursor = LoadCursorW(NULL,MAKEINTRESOURCEW(32512)); cls.hbrBackground = (HBRUSH)(COLOR_WINDOW+1);
    cls.lpszClassName = classes[role]; RegisterClassW(&cls);
    HWND window = CreateWindowExW(role == 2 ? WS_EX_TOOLWINDOW : 0,classes[role],
        role == 1 ? L"SC Hotkey Probe Receiver - press F8" : role == 2 ? L"SC Hotkey Probe Target" : L"SC Hotkey Probe Control",
        WS_OVERLAPPEDWINDOW,100+role*400,120+role*100,390,190,NULL,NULL,instance,NULL);
    label = CreateWindowW(L"STATIC",role == 1 ? L"Press F8 here. The key is delivered normally to this private receiver." : role == 2 ? L"Private activation target." : L"Waiting for F8 in the private receiver. Auto-closes after 90 seconds.",
        WS_CHILD|WS_VISIBLE,15,30,350,90,window,NULL,instance,NULL);
    SetTimer(window,1,90000,NULL); ShowWindow(window,SW_SHOWNOACTIVATE);
    if (!role) { g_hWnd = window; control = window; Report(L"begin production integration\r\n"); Child(exe,L"--target"); Child(exe,L"--receiver"); }
    MSG message; while (GetMessageW(&message,NULL,0,0)>0) { TranslateMessage(&message); DispatchMessageW(&message); }
    return 0;
}
