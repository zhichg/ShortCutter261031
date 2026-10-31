/* Temporary read-only observer for a user's real gesture. Counts input events
   without recording key values or clipboard contents; logs only window/process
   state for ShortCutter and Clipboard++. Waits up to 3 minutes for a gesture,
   then exits 45 seconds after the physical middle-button release. */
#define UNICODE
#define _UNICODE
#include "../src/main.h"
static FILE *logFile;
static HHOOK mouseHook, keyHook;
static volatile LONG moves, buttons, wheels, keys, releases;
static HWND observer, label;
static ULONGLONG began;
static volatile LONG triggerTick;
static DWORD previousInput;
static HWND previousForeground, previousClipboard;
static LONG previousMoves, previousButtons, previousWheels, previousKeys, previousReleases;
static int previousVisible = -1;
static LONG_PTR previousStyle;
static HWND clipboard;
static unsigned zIndex, clipboardZ;
static wchar_t clipboardPath[32768];
static DWORD clipboardPid;
static void Log(const wchar_t *line)
{
    if (!logFile) return;
    char utf8[8192]; int count = WideCharToMultiByte(CP_UTF8,0,line,-1,utf8,sizeof(utf8),NULL,NULL);
    if (count > 0) fwrite(utf8,1,(size_t)count-1,logFile);
    fflush(logFile);
}
static DWORD Integrity(DWORD pid)
{
    DWORD level = 0, needed = 0;
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid), token = NULL;
    if (process && OpenProcessToken(process,TOKEN_QUERY,&token)) {
        GetTokenInformation(token,TokenIntegrityLevel,NULL,0,&needed);
        BYTE *data = needed ? malloc(needed) : NULL;
        if (data && GetTokenInformation(token,TokenIntegrityLevel,data,needed,&needed)) {
            PSID sid = ((TOKEN_MANDATORY_LABEL*)data)->Label.Sid;
            level = *GetSidSubAuthority(sid,*GetSidSubAuthorityCount(sid)-1);
        }
        free(data); CloseHandle(token);
    }
    if (process) CloseHandle(process);
    return level;
}
static void Image(DWORD pid, wchar_t *path, DWORD capacity)
{
    path[0] = 0;
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);
    if (process) { QueryFullProcessImageNameW(process,0,path,&capacity); CloseHandle(process); }
}
static BOOL CALLBACK FindClipboard(HWND window, LPARAM parameter)
{
    (void)parameter; zIndex++;
    wchar_t title[128]; GetWindowTextW(window,title,128);
    if (wcscmp(title,L"Clipboard++")) return TRUE;
    DWORD pid = 0; GetWindowThreadProcessId(window,&pid);
    wchar_t path[32768]; Image(pid,path,32768);
    if (!wcsstr(path,L"\\Clipboard++.exe")) return TRUE;
    clipboard = window; clipboardPid = pid; clipboardZ = zIndex;
    lstrcpynW(clipboardPath,path,32768);
    return FALSE;
}
static LRESULT CALLBACK Mouse(int code, WPARAM message, LPARAM parameter)
{
    if (code == HC_ACTION) {
        MSLLHOOKSTRUCT *event = (MSLLHOOKSTRUCT*)parameter;
        if (!(event->flags & LLMHF_INJECTED)) {
            if (message == WM_MOUSEMOVE) InterlockedIncrement(&moves);
            else if (message == WM_LBUTTONDOWN || message == WM_RBUTTONDOWN || message == WM_MBUTTONDOWN || message == WM_XBUTTONDOWN) InterlockedIncrement(&buttons);
            else if (message == WM_LBUTTONUP || message == WM_RBUTTONUP || message == WM_MBUTTONUP || message == WM_XBUTTONUP) {
                InterlockedIncrement(&releases);
                if (message == WM_MBUTTONUP) InterlockedCompareExchange(&triggerTick,(LONG)GetTickCount(),0);
            }
            else if (message == WM_MOUSEWHEEL || message == WM_MOUSEHWHEEL) InterlockedIncrement(&wheels);
        }
    }
    return CallNextHookEx(mouseHook,code,message,parameter);
}
static LRESULT CALLBACK Keyboard(int code, WPARAM message, LPARAM parameter)
{
    if (code == HC_ACTION && (message == WM_KEYDOWN || message == WM_SYSKEYDOWN) &&
        !(((KBDLLHOOKSTRUCT*)parameter)->flags & LLKHF_INJECTED)) InterlockedIncrement(&keys);
    return CallNextHookEx(keyHook,code,message,parameter);
}
static void Snapshot(void)
{
    LASTINPUTINFO input = {sizeof(input),0}; GetLastInputInfo(&input);
    HWND foreground = GetForegroundWindow();
    clipboard = NULL; clipboardPid = 0; zIndex = clipboardZ = 0;
    EnumWindows(FindClipboard,0);
    int visible = clipboard && IsWindowVisible(clipboard);
    LONG_PTR style = clipboard ? GetWindowLongPtrW(clipboard,GWL_EXSTYLE) : 0;
    if (foreground != previousForeground || clipboard != previousClipboard || input.dwTime != previousInput ||
        visible != previousVisible || style != previousStyle || moves != previousMoves || buttons != previousButtons ||
        keys != previousKeys || wheels != previousWheels || releases != previousReleases) {
        wchar_t line[4096]; DWORD foregroundPid = 0;
        GetWindowThreadProcessId(foreground,&foregroundPid);
        wchar_t foregroundPath[32768]; Image(foregroundPid,foregroundPath,32768);
        const wchar_t *foregroundName = wcsrchr(foregroundPath,L'\\');
        foregroundName = foregroundName ? foregroundName+1 : foregroundPath;
        swprintf(line,4096,L"t=%llu input=%lu move=%ld down=%ld up=%ld wheel=%ld key=%ld fg=%p fgPid=%lu fgExe=%ls fgIL=%lu fgEx=0x%llx clip=%p clipPid=%lu clipIL=%lu visible=%d iconic=%d clipEx=0x%llx owner=%p root=%p z=%u\r\n",
            (unsigned long long)(GetTickCount64()-began),(unsigned long)input.dwTime,moves,buttons,releases,wheels,keys,
            (void*)foreground,(unsigned long)foregroundPid,foregroundName,(unsigned long)Integrity(foregroundPid),
            (unsigned long long)GetWindowLongPtrW(foreground,GWL_EXSTYLE),(void*)clipboard,(unsigned long)clipboardPid,
            (unsigned long)(clipboard ? Integrity(clipboardPid) : 0),visible,clipboard ? IsIconic(clipboard) : 0,
            (unsigned long long)style,(void*)GetWindow(clipboard,GW_OWNER),(void*)GetAncestor(clipboard,GA_ROOTOWNER),clipboardZ);
        Log(line);
    }
    previousInput = input.dwTime; previousForeground = foreground; previousClipboard = clipboard;
    previousVisible = visible; previousStyle = style; previousMoves = moves; previousButtons = buttons;
    previousKeys = keys; previousWheels = wheels; previousReleases = releases;
}
static LRESULT CALLBACK ObserverProc(HWND window, UINT message, WPARAM wp, LPARAM lp)
{
    if (message == WM_TIMER) {
        Snapshot();
        DWORD trigger = (DWORD)InterlockedCompareExchange(&triggerTick,0,0);
        ULONGLONG elapsed = trigger ? (DWORD)(GetTickCount()-trigger) : GetTickCount64()-began;
        unsigned duration = trigger ? 45000 : 180000;
        if (elapsed >= duration) DestroyWindow(window);
        else {
            wchar_t text[256]; swprintf(text,256,L"Read-only observation: %u seconds remain.\nDraw C once over your usual application, then avoid extra clicks.\nNo key values or clipboard contents are recorded.",(unsigned)((duration-elapsed)/1000));
            SetWindowTextW(label,text);
        }
        return 0;
    }
    if (message == WM_DESTROY) {
        if (mouseHook) UnhookWindowsHookEx(mouseHook);
        if (keyHook) UnhookWindowsHookEx(keyHook);
        Log(L"observer stopped\r\n"); if (logFile) fclose(logFile);
        PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(window,message,wp,lp);
}
int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous, LPSTR arguments, int show)
{
    (void)previous; (void)arguments; (void)show;
    wchar_t path[32768]; GetModuleFileNameW(NULL,path,32768);
    wchar_t *slash = wcsrchr(path,L'\\'); if (slash) lstrcpyW(slash+1,L"foreground-observation.log");
    logFile = _wfopen(path,L"wb"); began = GetTickCount64();
    WNDCLASSW cls = {0}; cls.hInstance = instance; cls.lpfnWndProc = ObserverProc;
    cls.lpszClassName = L"SC_ReadOnlyForegroundObserver"; cls.hbrBackground = (HBRUSH)(COLOR_WINDOW+1);
    RegisterClassW(&cls);
    observer = CreateWindowExW(WS_EX_NOACTIVATE,cls.lpszClassName,L"SC Read-only Foreground Observer",
        WS_OVERLAPPEDWINDOW,80,80,500,180,NULL,NULL,instance,NULL);
    label = CreateWindowW(L"STATIC",L"Read-only observer preparing...",WS_CHILD|WS_VISIBLE,20,25,450,95,observer,NULL,instance,NULL);
    mouseHook = SetWindowsHookExW(WH_MOUSE_LL,Mouse,instance,0);
    keyHook = SetWindowsHookExW(WH_KEYBOARD_LL,Keyboard,instance,0);
    wchar_t line[128]; swprintf(line,128,L"observer started mouseHook=%d keyHook=%d\r\n",mouseHook != NULL,keyHook != NULL); Log(line);
    ShowWindow(observer,SW_SHOWNOACTIVATE); SetTimer(observer,1,60,NULL);
    MSG message;
    while (GetMessageW(&message,NULL,0,0) > 0) { TranslateMessage(&message); DispatchMessageW(&message); }
    return 0;
}
