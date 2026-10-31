#include "main.h"
#include <stdlib.h>
#include <wctype.h>
#include <ctype.h>

#define ACTION_QUEUE_MAX 128
#define MACRO_STEP_MAX 2048
#define ACTION_MAX_DELAY 60000
typedef struct { WORD vk; BYTE up, extended; DWORD delay; } MacroStep;
typedef struct { Command command; HWND origin; LONG inputSerial; } ActionJob;
typedef struct {
    Command command;
    HWND replyTo;
    HWND origin;
    LONG generation;
    DWORD inputTime;
    int inputValid;
    LONG inputSerial;
    int foregroundTracked;
    int foregroundStatus;
    int success;
    DWORD error;
    DWORD foregroundError;
    DWORD foregroundStage;
    DWORD workerThread;
    DWORD grantError;
    int grantResult;
    PVOID volatile requestedWindow;
    volatile LONG requestedPid;
    volatile LONG references;
    volatile LONG deliveryFailed;
    /* Registration/window/deadline are owned by the input thread. The worker
       observes only the interlocked state (1 pending, 2 accepted, 3 failed). */
    int hotkeyId, hotkeyAttempted;
    HWND hotkeyWindow;
    DWORD hotkeyPid, hotkeyThread;
    ULONGLONG hotkeyDeadline;
    volatile LONG hotkeyState;
    DWORD hotkeyError;
    UINT hotkeyInjected;
    PVOID volatile activationWindow;
    int wasIconic, restoreResult, restoreCommandResult, stillIconic;
    DWORD restoreError, restoreCommandError;
} ActionRun;
/* Only the input thread sets or clears this pointer. Requests contain handles
   and a generation, never a pointer supplied through a window message. */
static ActionRun *g_activeLaunch;
static void Action_ReleaseRun(ActionRun *run)
{
    if (InterlockedDecrement(&run->references) == 0) HeapFree(GetProcessHeap(),0,run);
}
static ActionJob g_jobs[ACTION_QUEUE_MAX];
static int g_jobHead, g_jobCount, g_actionBusy, g_actionTicking, g_actionStopped;
static volatile LONG g_actionGeneration;
static volatile LONG g_actionInputSerial;
static int g_foregroundHotkeyNext = 0x4000;
static int g_retiredHotkeyId;
static HWND g_retiredHotkeyWindow;
static int g_foregroundKeyReleaseFailed;
static int Action_CleanRetiredHotkey(void)
{
    if (!g_retiredHotkeyId) return 1;
    SetLastError(ERROR_SUCCESS);
    if (!UnregisterHotKey(g_retiredHotkeyWindow,g_retiredHotkeyId) &&
        GetLastError() != ERROR_HOTKEY_NOT_REGISTERED && IsWindow(g_retiredHotkeyWindow)) return 0;
    g_retiredHotkeyId = 0;
    g_retiredHotkeyWindow = NULL;
    return 1;
}
void Action_NoteUserInput(void) { InterlockedIncrement(&g_actionInputSerial); }
static MacroStep g_macro[MACRO_STEP_MAX];
static int g_macroCount, g_macroPosition, g_macroCommand;
static ULONGLONG g_macroDue;
static HWND g_macroOrigin;
static BYTE g_macroOwned[256], g_macroBorrowed[256], g_macroExtended[256];

/* Compile the complete sequence before issuing any input. Invalid syntax never
   executes a valid prefix. Every explicit press must have a matching release. */
static int Macro_Error(wchar_t *error, size_t count, const wchar_t *text)
{
    if (error && count) {
        wcsncpy(error, text, count - 1);
        error[count - 1] = 0;
    }
    return 0;
}
static int Macro_Key(const char *name, WORD *vk, BYTE *extended)
{
    static const struct { const char *name; WORD vk; BYTE extended; } map[] = {
        {"Alt",VK_MENU,0},{"Ctrl",VK_CONTROL,0},{"Shift",VK_SHIFT,0},{"Win",VK_LWIN,1},
        {"Tab",VK_TAB,0},{"Enter",VK_RETURN,0},{"Space",VK_SPACE,0},{"Esc",VK_ESCAPE,0},
        {"Backspace",VK_BACK,0},{"Delete",VK_DELETE,1},{"Insert",VK_INSERT,1},
        {"Home",VK_HOME,1},{"End",VK_END,1},{"PgUp",VK_PRIOR,1},{"PgDn",VK_NEXT,1},
        {"Up",VK_UP,1},{"Down",VK_DOWN,1},{"Left",VK_LEFT,1},{"Right",VK_RIGHT,1},
        {"F1",VK_F1,0},{"F2",VK_F2,0},{"F3",VK_F3,0},{"F4",VK_F4,0},
        {"F5",VK_F5,0},{"F6",VK_F6,0},{"F7",VK_F7,0},{"F8",VK_F8,0},
        {"F9",VK_F9,0},{"F10",VK_F10,0},{"F11",VK_F11,0},{"F12",VK_F12,0},
        {"Apps",VK_APPS,1},{"PrtSc",VK_SNAPSHOT,1}
    };
    if (name[0] && !name[1]) {
        char c = name[0];
        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
            *vk = (WORD)c; *extended = 0; return 1;
        }
    }
    for (size_t i = 0; i < sizeof(map)/sizeof(map[0]); i++) {
        if (!lstrcmpiA(name, map[i].name)) {
            *vk = map[i].vk; *extended = map[i].extended; return 1;
        }
    }
    /* Record physical keys with left/right identity and the extended flag.
       Legacy named tokens keep their original meaning. */
    size_t length=strlen(name);
    if ((length==4 || (length==5 && (name[4]=='E' || name[4]=='e'))) &&
        (name[0]=='v' || name[0]=='V') && (name[1]=='k' || name[1]=='K') &&
        isxdigit((unsigned char)name[2]) && isxdigit((unsigned char)name[3])) {
        char digits[3]={name[2],name[3],0};
        unsigned long value=strtoul(digits,NULL,16);
        if (value>0 && value<256) { *vk=(WORD)value;*extended=length==5;return 1; }
    }
    return 0;
}

/* Also accepts the unfinished held-key prefix shown during recording. */
void Action_FormatKeySeq(const char *sequence, wchar_t *out, size_t capacity)
{
    if (!capacity) return;
    out[0]=0;size_t used=0;
    for (const char *p=sequence;*p;) {
        while (*p==' ' || *p=='\r' || *p=='\n' || *p=='\t') p++;
        if (!*p) break;
        const char *end=strchr(p,'}');
        if (*p!='{' || !end || end-p>=64) { SC_WideFromUtf8(out,capacity,sequence);return; }
        char token[64],name[32]="",action[32]="";
        memcpy(token,p+1,(size_t)(end-p-1));token[end-p-1]=0;
        sscanf(token,"%31s %31s",name,action);
        wchar_t item[128];WORD vk;BYTE extended;
        if (!lstrcmpiA(name,"Sleep")) {
            unsigned long delay=strtoul(action,NULL,10);
            swprintf(item,128,delay>=200?L"(%lu ms)\r\n":L"(%lu ms)   ",delay);
        } else if (Macro_Key(name,&vk,&extended)) {
            wchar_t keyName[64]=L"";
            UINT scan=MapVirtualKeyW(vk,MAPVK_VK_TO_VSC);
            GetKeyNameTextW((LONG)((scan<<16)|(extended?1u<<24:0)),keyName,64);
            if (!keyName[0]) SC_WideFromUtf8(keyName,64,name);
            swprintf(item,128,L"%ls %ls   ",keyName,
                !lstrcmpiA(action,"down")?L"↓":!lstrcmpiA(action,"up")?L"↑":L"↕");
        } else { SC_WideFromUtf8(out,capacity,sequence);return; }
        size_t length=wcslen(item);
        if (used+length>=capacity) break;
        memcpy(out+used,item,(length+1)*sizeof(wchar_t));used+=length;p=end+1;
    }
}
static int Macro_Parse(const char *seq, MacroStep *steps, int *stepCount,
                       wchar_t *error, size_t errorCount)
{
    const wchar_t *syntax = SC_Text(
        L"Use {Key}, {Key down}, {Key up} or {Sleep 1-5000}.",
        L"请使用 {Key}、{Key down}、{Key up} 或 {Sleep 1-5000}。");
    if (error && errorCount) error[0] = 0;
    if (!seq) return Macro_Error(error,errorCount,syntax);
    size_t length = 0;
    while (length < SC_KEYS_CAP && seq[length]) length++;
    if (!length || length == SC_KEYS_CAP)
        return Macro_Error(error,errorCount,SC_Text(L"Key sequence is empty or too long.",L"按键序列为空或过长。"));
    BYTE held[256] = {0};
    int count = 0;
    DWORD totalDelay = 0;
    const char *p = seq, *end = seq + length;
    while (p < end) {
        while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) p++;
        if (p == end) break;
        if (*p++ != '{') return Macro_Error(error,errorCount,syntax);
        char token[64];
        size_t ti = 0;
        while (p < end && *p != '}') {
            if (*p == '{' || ti == sizeof(token)-1) return Macro_Error(error,errorCount,syntax);
            token[ti++] = *p++;
        }
        if (p == end) return Macro_Error(error,errorCount,syntax);
        p++;
        token[ti] = 0;
        while (ti && (token[ti-1] == ' ' || token[ti-1] == '\t')) token[--ti] = 0;
        char *name = token;
        while (*name == ' ' || *name == '\t') name++;
        char *action = name;
        while (*action && *action != ' ' && *action != '\t') action++;
        if (*action) *action++ = 0;
        while (*action == ' ' || *action == '\t') action++;
        if (!lstrcmpiA(name,"Sleep")) {
            char *numberEnd = NULL;
            unsigned long delay = strtoul(action,&numberEnd,10);
            if (!*action || *action == '-' || *numberEnd || delay < 1 || delay > 5000 ||
                totalDelay + delay > ACTION_MAX_DELAY) return Macro_Error(error,errorCount,syntax);
            if (count >= MACRO_STEP_MAX) return Macro_Error(error,errorCount,syntax);
            if (steps) steps[count] = (MacroStep){0,0,0,(DWORD)delay};
            count++; totalDelay += (DWORD)delay;
            continue;
        }
        WORD vk;
        BYTE extended;
        if (!Macro_Key(name,&vk,&extended))
            return Macro_Error(error,errorCount,SC_Text(L"Unknown key name.",L"无法识别的按键名称。"));
        if (!lstrcmpiA(action,"down")) {
            if (held[vk]) return Macro_Error(error,errorCount,SC_Text(L"A key is pressed twice without being released.",L"同一个按键被连续按下，缺少释放。"));
            held[vk] = 1;
            if (count >= MACRO_STEP_MAX) return Macro_Error(error,errorCount,syntax);
            if (steps) steps[count] = (MacroStep){vk,0,extended,0};
            count++;
        } else if (!lstrcmpiA(action,"up")) {
            if (!held[vk]) return Macro_Error(error,errorCount,SC_Text(L"Key release has no matching press.",L"按键释放没有对应的按下。"));
            held[vk] = 0;
            if (count >= MACRO_STEP_MAX) return Macro_Error(error,errorCount,syntax);
            if (steps) steps[count] = (MacroStep){vk,1,extended,0};
            count++;
        } else if (!*action) {
            if (held[vk]) return Macro_Error(error,errorCount,SC_Text(L"A held key cannot also be tapped.",L"已保持按下的按键不能再次单击。"));
            if (count + 2 > MACRO_STEP_MAX) return Macro_Error(error,errorCount,syntax);
            if (steps) {
                steps[count] = (MacroStep){vk,0,extended,0};
                steps[count+1] = (MacroStep){vk,1,extended,0};
            }
            count += 2;
        } else return Macro_Error(error,errorCount,syntax);
    }
    if (!count) return Macro_Error(error,errorCount,syntax);
    for (int i = 0; i < 256; i++)
        if (held[i]) return Macro_Error(error,errorCount,SC_Text(L"Every held key needs a matching {Key up}.",L"每个保持按下的按键都需要对应的 {Key up}。"));
    if (stepCount) *stepCount = count;
    return 1;
}
int Action_ValidateKeySeq(const char *seq, wchar_t *error, size_t errorCount)
{ return Macro_Parse(seq,NULL,NULL,error,errorCount); }

static void Macro_Release(void)
{
    for (int vk = 0; vk < 256; vk++) {
        if (g_macroOwned[vk]) {
            INPUT input = {0};
            input.type = INPUT_KEYBOARD;
            input.ki.wVk = (WORD)vk;
            input.ki.dwFlags = KEYEVENTF_KEYUP | (g_macroExtended[vk] ? KEYEVENTF_EXTENDEDKEY : 0);
            if (SendInput(1,&input,sizeof(input)) != 1)
                SC_ReportError(SC_Text(L"Release macro keys",L"释放宏按键"),GetLastError() ? GetLastError() : ERROR_ACCESS_DENIED);
        }
    }
    memset(g_macroOwned,0,sizeof(g_macroOwned));
    memset(g_macroBorrowed,0,sizeof(g_macroBorrowed));
    memset(g_macroExtended,0,sizeof(g_macroExtended));
    g_macroCount = g_macroPosition = 0;
    g_macroDue = 0;
    g_macroOrigin = NULL;
}
/* Never release a physical key that was already held when the macro pressed it. */
static int Macro_Send(const MacroStep *step)
{
    WORD vk = step->vk;
    if (!step->up && (GetAsyncKeyState(vk) & 0x8000)) {
        g_macroBorrowed[vk] = 1;
        return 1;
    }
    if (step->up && g_macroBorrowed[vk]) {
        g_macroBorrowed[vk] = 0;
        return 1;
    }
    INPUT input = {0};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = vk;
    input.ki.dwFlags = (step->up ? KEYEVENTF_KEYUP : 0) |
                      (step->extended ? KEYEVENTF_EXTENDEDKEY : 0);
    if (SendInput(1,&input,sizeof(input)) != 1) return 0;
    g_macroOwned[vk] = !step->up;
    g_macroExtended[vk] = step->extended;
    return 1;
}

/* Only executable files may reuse an existing window. Shell documents,
   directories, links and URLs always retain their normal open semantics. */
static wchar_t *Action_Expand(const wchar_t *text)
{
    DWORD count = ExpandEnvironmentStringsW(text,NULL,0);
    if (!count || count > 32768) return NULL;
    wchar_t *value = HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,(size_t)count*sizeof(wchar_t));
    if (!value) return NULL;
    if (!ExpandEnvironmentStringsW(text,value,count)) {
        HeapFree(GetProcessHeap(),0,value);
        return NULL;
    }
    return value;
}
static int Action_IsExe(const wchar_t *path)
{
    const wchar_t *dot = wcsrchr(path,L'.');
    return dot && !lstrcmpiW(dot,L".exe");
}
static int Action_IsExplorer(const wchar_t *path)
{
    const wchar_t *name = path;
    for (const wchar_t *p = path; *p; p++) if (*p == L'\\' || *p == L'/') name = p + 1;
    return !lstrcmpiW(name,L"explorer.exe");
}
static wchar_t *Action_ResolveExecutable(const wchar_t *path, const wchar_t *workingDir)
{
    if (!Action_IsExe(path) || (wcschr(path,L':') && path[1] != L':')) return NULL;
    wchar_t *resolved = HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,32768*sizeof(wchar_t));
    if (!resolved) return NULL;
    DWORD length;
    if (!wcschr(path,L'\\') && !wcschr(path,L'/'))
        length = SearchPathW(workingDir && *workingDir ? workingDir : NULL,path,NULL,32768,resolved,NULL);
    else if (path[0] != L'\\' && !(path[0] && path[1] == L':') && workingDir && *workingDir) {
        size_t capacity = wcslen(workingDir)+wcslen(path)+2;
        wchar_t *combined = HeapAlloc(GetProcessHeap(),0,capacity*sizeof(wchar_t));
        if (!combined) { HeapFree(GetProcessHeap(),0,resolved); return NULL; }
        swprintf(combined,capacity,L"%ls\\%ls",workingDir,path);
        length = GetFullPathNameW(combined,32768,resolved,NULL);
        HeapFree(GetProcessHeap(),0,combined);
    } else length = GetFullPathNameW(path,32768,resolved,NULL);
    DWORD attributes = length && length < 32768 ? GetFileAttributesW(resolved) : INVALID_FILE_ATTRIBUTES;
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY)) {
        HeapFree(GetProcessHeap(),0,resolved);
        return NULL;
    }
    return resolved;
}
typedef struct { const wchar_t *path; HWND found; } ExeWindowContext;
static BOOL CALLBACK Action_FindExeWindow(HWND window, LPARAM param)
{
    ExeWindowContext *context = (ExeWindowContext*)param;
    if (!IsWindowVisible(window) || GetWindow(window,GW_OWNER) ||
        (GetWindowLongPtrW(window,GWL_EXSTYLE) & WS_EX_NOACTIVATE)) return TRUE;
    DWORD pid = 0;
    GetWindowThreadProcessId(window,&pid);
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);
    if (!process) return TRUE;
    wchar_t path[32768];
    DWORD length = 32768;
    BOOL same = QueryFullProcessImageNameW(process,0,path,&length) && !lstrcmpiW(path,context->path);
    CloseHandle(process);
    if (!same) return TRUE;
    context->found = window;
    return FALSE;
}
static int Action_IsForeground(HWND window)
{
    HWND foreground = GetForegroundWindow();
    return foreground && (foreground == window || GetAncestor(foreground,GA_ROOTOWNER) == window);
}
static int Action_HasLaunchAttention(ActionRun *run);
static void Action_EndForegroundHotkey(ActionRun *run, LONG state)
{
    if (run->hotkeyId) {
        g_retiredHotkeyId = run->hotkeyId;
        g_retiredHotkeyWindow = run->replyTo;
        Action_CleanRetiredHotkey();
    }
    run->hotkeyId = 0;
    run->hotkeyWindow = NULL;
    InterlockedExchange(&run->hotkeyState,state);
}
static int Action_BeginForegroundHotkey(ActionRun *run, HWND window)
{
    if (run->hotkeyAttempted) return InterlockedCompareExchange(&run->hotkeyState,0,0) == 1;
    run->hotkeyAttempted = 1;
    if (!Action_CleanRetiredHotkey()) { run->hotkeyError = ERROR_BUSY; return 0; }
    if (g_foregroundKeyReleaseFailed) {
        if (GetAsyncKeyState(VK_F24) & 0x8000) { run->hotkeyError = ERROR_BUSY; return 0; }
        g_foregroundKeyReleaseFailed = 0;
    }
    /* Never alter a physical key or inject into an existing key combination. */
    for (int key = 1; key < 256; key++) {
        if (GetAsyncKeyState(key) & 0x8000) { run->hotkeyError = ERROR_BUSY; return 0; }
    }
    /* Each registration gets a fresh ID. Late WM_HOTKEY messages from a
       completed/cancelled request cannot activate the next command. */
    if (g_foregroundHotkeyNext > 0xbfff) { run->hotkeyError = ERROR_TOO_MANY_CMDS; return 0; }
    int id = g_foregroundHotkeyNext++;
    if (!RegisterHotKey(run->replyTo,id,MOD_NOREPEAT,VK_F24)) {
        run->hotkeyError = GetLastError(); return 0;
    }
    run->hotkeyId = id;
    run->hotkeyWindow = window;
    run->hotkeyThread = GetWindowThreadProcessId(window,&run->hotkeyPid);
    run->hotkeyDeadline = GetTickCount64()+200;
    InterlockedExchange(&run->hotkeyState,1);
    if (!Action_HasLaunchAttention(run) ||
        InterlockedCompareExchangePointer(&run->requestedWindow,NULL,NULL) != window) {
        Action_EndForegroundHotkey(run,3); return 0;
    }
    INPUT keys[2] = {0}; keys[0].type = keys[1].type = INPUT_KEYBOARD;
    keys[0].ki.wVk = keys[1].ki.wVk = VK_F24;
    keys[1].ki.dwFlags = KEYEVENTF_KEYUP;
    SetLastError(ERROR_SUCCESS);
    run->hotkeyInjected = SendInput(2,keys,sizeof(INPUT));
    if (run->hotkeyInjected != 2) {
        run->hotkeyError = GetLastError();
        if (run->hotkeyInjected == 1) {
            /* Release only the down event we inserted. A cleanup failure is
               diagnosed and blocks further injection while F24 stays down. */
            int released = 0;
            for (int attempt = 0; attempt < 3 && !released; attempt++)
                released = SendInput(1,&keys[1],sizeof(INPUT)) == 1;
            if (!released) {
                g_foregroundKeyReleaseFailed = 1;
                SC_LogDiagnostic(L"Foreground F24 release failed after partial SendInput; further foreground injection is blocked until the key is released.");
            }
        }
        Action_EndForegroundHotkey(run,3); return 0;
    }
    return 1;
}
int Action_HandleForegroundHotkey(WPARAM id, LPARAM key)
{
    if (id < 0x4000 || id > 0xbfff) return 0;
    ActionRun *run = g_activeLaunch;
    if (!run || run->hotkeyId != (int)id || HIWORD(key) != VK_F24 ||
        LOWORD(key) != 0 || InterlockedCompareExchange(&run->hotkeyState,0,0) != 1) return 1;
    HWND window = run->hotkeyWindow;
    DWORD pid = 0, thread = GetWindowThreadProcessId(window,&pid);
    int accepted = !g_actionStopped && g_actionBusy == 2 &&
        GetTickCount64() <= run->hotkeyDeadline && Action_HasLaunchAttention(run) &&
        InterlockedCompareExchangePointer(&run->requestedWindow,NULL,NULL) == window &&
        thread && thread == run->hotkeyThread && pid == run->hotkeyPid &&
        IsWindow(window) && SetForegroundWindow(window);
    Action_EndForegroundHotkey(run,accepted ? 2 : 3);
    return 1;
}
LRESULT Action_HandleForeground(UINT message, WPARAM target, LPARAM generation)
{
    ActionRun *run = g_activeLaunch;
    if (g_actionStopped || g_actionBusy != 2 || !run || run->generation != (LONG)generation ||
        !Action_HasLaunchAttention(run)) return 0;
    if (message == WM_SC_FOREGROUND && target &&
        InterlockedCompareExchangePointer(&run->requestedWindow,NULL,NULL) == (PVOID)target) {
        if (InterlockedCompareExchange(&run->hotkeyState,0,0) == 1) return 3;
        if (SetForegroundWindow((HWND)target)) return 2;
        /* A low-level hook callback is not an input message for this process.
           A system-generated WM_HOTKEY supplies foreground permission. Keep
           this fallback asynchronous; never wait inside the input thread. */
        return Action_BeginForegroundHotkey(run,(HWND)target) ? 3 : 1;
    }
    if (message == WM_SC_FOREGROUNDGRANT && target &&
        InterlockedCompareExchange(&run->requestedPid,0,(LONG)target) == (LONG)target) {
        SetLastError(ERROR_SUCCESS);
        if (AllowSetForegroundWindow((DWORD)target)) return 2;
        return (LRESULT)(((DWORD_PTR)GetLastError() << 2) | 1);
    }
    return 0;
}
static int Action_RequestInputThread(ActionRun *run, HWND window, DWORD pid, UINT timeout, DWORD *error)
{
    UINT message = window ? WM_SC_FOREGROUND : WM_SC_FOREGROUNDGRANT;
    WPARAM target = window ? (WPARAM)window : (WPARAM)pid;
    DWORD_PTR result = 0;
    ULONGLONG deadline = GetTickCount64()+timeout;
    if (window) InterlockedExchangePointer(&run->requestedWindow,window);
    else InterlockedExchange(&run->requestedPid,(LONG)pid);
    SetLastError(ERROR_SUCCESS);
    LRESULT sent = SendMessageTimeoutW(run->replyTo,message,target,(LPARAM)run->generation,
        SMTO_ABORTIFHUNG|SMTO_BLOCK|SMTO_ERRORONEXIT,timeout,&result);
    DWORD sendError = sent ? ERROR_SUCCESS : GetLastError();
    if (sent && result == 3 && window) {
        while (InterlockedCompareExchange(&run->hotkeyState,0,0) == 1 &&
            GetTickCount64() < deadline && !Action_IsForeground(window) && Action_HasLaunchAttention(run)) Sleep(2);
        if (InterlockedCompareExchange(&run->hotkeyState,0,0) == 2 || Action_IsForeground(window)) result = 2;
        else { result = 1; run->hotkeyError = run->hotkeyError ? run->hotkeyError : ERROR_TIMEOUT; }
    }
    if (window) InterlockedCompareExchangePointer(&run->requestedWindow,NULL,window);
    else InterlockedCompareExchange(&run->requestedPid,0,(LONG)pid);
    if (!sent) *error = sendError ? sendError : ERROR_TIMEOUT;
    else if (!result) *error = ERROR_CANCELLED;
    /* SetForegroundWindow documents no extended error. A zero result must not
       be relabeled as ERROR_ACCESS_DENIED. Grant failures do report LastError. */
    else *error = result == 2 ? ERROR_SUCCESS : window ? ERROR_FUNCTION_FAILED : (DWORD)(result >> 2);
    return sent && result == 2;
}
static int Action_ActivateWindow(ActionRun *run, HWND window, UINT timeout, DWORD *error)
{
    *error = ERROR_FUNCTION_FAILED;
    ULONGLONG deadline = GetTickCount64() + timeout;
    if (run) InterlockedExchangePointer(&run->activationWindow,window);
    if (IsIconic(window)) {
        if (run) run->wasIconic = 1;
        SetLastError(ERROR_SUCCESS);
        int restored = ShowWindowAsync(window,SW_RESTORE);
        DWORD restoreError = restored ? ERROR_SUCCESS : GetLastError();
        if (run) { run->restoreResult = restored; run->restoreError = restoreError; }
        /* A restore event can be denied across integrity levels, or merely
           queued. If it was not queued, use the standard system command with
           a bounded wait on this worker; never block an input hook or UI. */
        if (!restored && IsIconic(window)) {
            DWORD_PTR result = 0;
            ULONGLONG now = GetTickCount64();
            if (now >= deadline) { *error = ERROR_TIMEOUT; return 0; }
            UINT remaining = (UINT)(deadline-now);
            SetLastError(ERROR_SUCCESS);
            int command = !!SendMessageTimeoutW(window,WM_SYSCOMMAND,SC_RESTORE,0,
                SMTO_ABORTIFHUNG|SMTO_BLOCK|SMTO_ERRORONEXIT,remaining < 200 ? remaining : 200,&result);
            DWORD commandError = command ? ERROR_SUCCESS : GetLastError();
            if (run) { run->restoreCommandResult = command; run->restoreCommandError = commandError; }
        }
        /* SetForegroundWindow may accept a still-minimized HWND. That is not
           a visible result. Wait for restoration before transferring focus. */
        while (IsIconic(window)) {
            if (run && !Action_HasLaunchAttention(run)) { *error = ERROR_CANCELLED; return 0; }
            ULONGLONG now = GetTickCount64();
            if (now >= deadline) {
                if (run) { run->stillIconic = 1; run->foregroundStatus = 6; run->foregroundStage = 5; }
                *error = run && run->restoreCommandError ? run->restoreCommandError :
                    restoreError ? restoreError : ERROR_TIMEOUT;
                return 0;
            }
            Sleep(deadline-now < 10 ? (DWORD)(deadline-now) : 10);
        }
    }
    if (run && !Action_HasLaunchAttention(run)) { *error = ERROR_CANCELLED; return 0; }
    ULONGLONG activationNow = GetTickCount64();
    if (activationNow >= deadline) { *error = ERROR_TIMEOUT; return 0; }
    timeout = (UINT)(deadline-activationNow);
    int accepted;
    if (run) {
        run->foregroundStage = 1;
        accepted = Action_RequestInputThread(run,window,0,timeout < 200 ? timeout : 200,error);
        if (!accepted && *error == ERROR_FUNCTION_FAILED) run->foregroundStage = 2;
    } else accepted = SetForegroundWindow(window);
    if (accepted) {
        /* A different input queue receives foreground activation asynchronously.
           Let it process that notification before treating reuse as complete.
           This bounded wait runs only on the launch worker, never in a hook. */
        DWORD_PTR result = 0;
        ULONGLONG now = GetTickCount64();
        if (now >= deadline) { *error = ERROR_TIMEOUT; return 0; }
        timeout = (UINT)(deadline - now);
        if (run) run->foregroundStage = 3;
        SetLastError(ERROR_SUCCESS);
        if (!SendMessageTimeoutW(window,WM_NULL,0,0,
                SMTO_ABORTIFHUNG|SMTO_BLOCK|SMTO_ERRORONEXIT,timeout,&result)) {
            *error = GetLastError() ? GetLastError() : ERROR_TIMEOUT;
        } else if (Action_IsForeground(window) && !IsIconic(window)) {
            *error = ERROR_SUCCESS;
            if (run) run->foregroundStatus = 1;
            return 1;
        } else {
            if (run) run->foregroundStage = 4;
            *error = ERROR_NOT_READY;
        }
    }
    if (run) run->foregroundStatus = 6;
    return 0;
}
static int Action_ActivateExe(const wchar_t *path, ActionRun *run)
{
    ExeWindowContext context = {path,NULL};
    EnumWindows(Action_FindExeWindow,(LPARAM)&context);
    if (!context.found) return 0;
    DWORD error;
    if (Action_ActivateWindow(run,context.found,500,&error)) return 1;
    FLASHWINFO flash = {sizeof(flash),context.found,FLASHW_TRAY,2,0};
    FlashWindowEx(&flash);
    return 0;
}
static int Action_HasLaunchAttention(ActionRun *run)
{
    if (InterlockedCompareExchange(&g_actionGeneration,0,0) != run->generation) {
        run->foregroundStatus = 4; return 0;
    }
    if (InterlockedCompareExchange(&g_actionInputSerial,0,0) != run->inputSerial) {
        run->foregroundStatus = 2; return 0;
    }
    HWND foreground = GetForegroundWindow();
    HWND expected = InterlockedCompareExchangePointer(&run->activationWindow,NULL,NULL);
    if (foreground && foreground != run->origin &&
        !(expected && (foreground == expected || GetAncestor(foreground,GA_ROOTOWNER) == expected))) {
        run->foregroundStatus = 3; return 0;
    }
    return 1;
}
/* A launcher can return before a resident process creates its UI. Watch the
   exact executable, even after the launch process exits. Never carry a delayed
   foreground request across a new key/button action, another application or
   cancellation. GetLastInputInfo includes harmless mouse movement, so it is
   diagnostic only; physical input hooks maintain the cancellation serial. */
static void Action_ConfirmLaunch(ActionRun *run, const wchar_t *path)
{
    ULONGLONG deadline = GetTickCount64() + 2000;
    for (;;) {
        ExeWindowContext context = {path,NULL};
        EnumWindows(Action_FindExeWindow,(LPARAM)&context);
        if (context.found && Action_IsForeground(context.found) && !IsIconic(context.found)) {
            run->foregroundStatus = 1; return;
        }
        if (!Action_HasLaunchAttention(run)) return;
        if (context.found) {
            ULONGLONG now = GetTickCount64();
            if (now >= deadline || !Action_HasLaunchAttention(run)) return;
            UINT timeout = deadline - now < 500 ? (UINT)(deadline - now) : 500;
            DWORD error;
            if (!Action_ActivateWindow(run,context.found,timeout,&error) && Action_HasLaunchAttention(run))
                run->foregroundError = error;
            return;
        }
        ULONGLONG now = GetTickCount64();
        if (now >= deadline) { run->foregroundStatus = 5; return; }
        Sleep(deadline - now < 50 ? (DWORD)(deadline - now) : 50);
    }
}
static DWORD WINAPI Action_RunWorker(void *param)
{
    ActionRun *run = (ActionRun*)param;
    run->workerThread = GetCurrentThreadId();
    run->grantResult = -1;
    Command *command = &run->command;
    wchar_t path[SC_PATH_CAP], arguments[SC_PATH_CAP], workingDir[SC_PATH_CAP];
    wchar_t *expanded = NULL, *directory = NULL, *params = NULL, *exe = NULL;
    LASTINPUTINFO input = {sizeof(input),0};
    run->inputValid = GetLastInputInfo(&input);
    run->inputTime = input.dwTime;
    HRESULT com = CoInitializeEx(NULL,COINIT_APARTMENTTHREADED);
    run->error = ERROR_INVALID_DATA;
    if (!SC_WideFromUtf8(path,SC_PATH_CAP,command->runPath) ||
        !SC_WideFromUtf8(arguments,SC_PATH_CAP,command->arguments) ||
        !SC_WideFromUtf8(workingDir,SC_PATH_CAP,command->workingDir) || !path[0]) goto done;
    expanded = Action_Expand(!lstrcmpiW(path,L"explorer") ? L"explorer.exe" : path);
    directory = Action_Expand(workingDir);
    if (!expanded || !directory) { run->error = ERROR_NOT_ENOUGH_MEMORY; goto done; }
    if (directory[0]) {
        DWORD attributes = GetFileAttributesW(directory);
        if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY)) {
            run->error = ERROR_PATH_NOT_FOUND; goto done;
        }
    }
    if (command->type == CMD_RUN && command->activateExisting && !command->runAsAdmin &&
        !arguments[0] && !Action_IsExplorer(expanded)) {
        exe = Action_ResolveExecutable(expanded,directory);
        if (exe) {
            run->foregroundTracked = 1;
            run->success = Action_ActivateExe(exe,run);
            if (run->success) goto done;
        }
    }
    int show = SW_SHOW;
    const wchar_t *file = expanded;
    const wchar_t *parameters = arguments[0] ? arguments : NULL;
    if (command->type == CMD_SCRIPT) {
        const wchar_t *dot = wcsrchr(expanded,L'.');
        int showScript = dot && dot-expanded >= 5 && !wcsncmp(dot-5,L"_Show",5);
        show = showScript ? SW_SHOW : SW_HIDE;
        if (dot && !lstrcmpiW(dot,L".ps1")) {
            if (wcschr(expanded,L'"')) { run->error = ERROR_INVALID_NAME; goto done; }
            const wchar_t *prefix = showScript ? L"-ExecutionPolicy Bypass -File" :
                L"-WindowStyle Hidden -ExecutionPolicy Bypass -File";
            size_t capacity = wcslen(prefix)+wcslen(expanded)+wcslen(arguments)+8;
            params = HeapAlloc(GetProcessHeap(),0,capacity*sizeof(wchar_t));
            if (!params) { run->error = ERROR_NOT_ENOUGH_MEMORY; goto done; }
            swprintf(params,capacity,L"%ls \"%ls\"%ls%ls",prefix,expanded,arguments[0] ? L" " : L"",arguments);
            file = L"powershell.exe";
            parameters = params;
        }
    }
    SHELLEXECUTEINFOW info = {0};
    info.cbSize = sizeof(info);
    info.fMask = SEE_MASK_FLAG_NO_UI | SEE_MASK_NOASYNC | SEE_MASK_NOCLOSEPROCESS;
    info.lpVerb = command->runAsAdmin ? L"runas" : L"open";
    info.lpFile = file;
    info.lpParameters = parameters;
    info.lpDirectory = directory[0] ? directory : NULL;
    info.nShow = show;
    run->success = ShellExecuteExW(&info);
    run->error = run->success ? ERROR_SUCCESS : GetLastError();
    if (!run->success && !run->error) run->error = ERROR_GEN_FAILURE;
    if (run->success && exe && Action_HasLaunchAttention(run)) {
        if (info.hProcess) {
            DWORD pid = GetProcessId(info.hProcess);
            if (pid) run->grantResult = Action_RequestInputThread(run,NULL,pid,200,&run->grantError);
        }
        Action_ConfirmLaunch(run,exe);
    }
    if (info.hProcess) CloseHandle(info.hProcess);
done:
    if (exe) HeapFree(GetProcessHeap(),0,exe);
    if (params) HeapFree(GetProcessHeap(),0,params);
    if (directory) HeapFree(GetProcessHeap(),0,directory);
    if (expanded) HeapFree(GetProcessHeap(),0,expanded);
    if (SUCCEEDED(com)) CoUninitialize();
    /* The active input-thread slot, worker and queued result each own a
       reference. Cancellation and a timed-out request cannot free live state. */
    InterlockedIncrement(&run->references);
    if (!PostMessageW(run->replyTo,WM_SC_ACTIONRESULT,0,(LPARAM)run)) {
        Action_ReleaseRun(run);
        InterlockedExchange(&run->deliveryFailed,1);
    }
    Action_ReleaseRun(run);
    return 0;
}
void Action_HandleResult(LPARAM result)
{
    ActionRun *run = (ActionRun*)result;
    if (!run) return;
    if (run->foregroundTracked) {
        wchar_t name[SC_NAME_CAP], diagnostic[SC_NAME_CAP+760];
        SC_WideFromUtf8(name,SC_NAME_CAP,run->command.name);
        LASTINPUTINFO input = {sizeof(input),0}; GetLastInputInfo(&input);
        swprintf(diagnostic,sizeof(diagnostic)/sizeof(diagnostic[0]),
            L"Foreground command=%ls status=%d stage=%lu inputSerial=%ld currentSerial=%ld idleBefore=%lu idleNow=%lu grant=%d grantError=%lu fg=%p origin=%p hotkey=%ld injected=%u hotkeyError=%lu target=%p wasIconic=%d restore=%d restoreError=%lu restoreCommand=%d restoreCommandError=%lu stillIconic=%d",
            name,run->foregroundStatus,(unsigned long)run->foregroundStage,run->inputSerial,
            InterlockedCompareExchange(&g_actionInputSerial,0,0),(unsigned long)run->inputTime,
            (unsigned long)input.dwTime,run->grantResult,(unsigned long)run->grantError,
            (void*)GetForegroundWindow(),(void*)run->origin,
            InterlockedCompareExchange(&run->hotkeyState,0,0),run->hotkeyInjected,(unsigned long)run->hotkeyError,
            InterlockedCompareExchangePointer(&run->activationWindow,NULL,NULL),run->wasIconic,run->restoreResult,
            (unsigned long)run->restoreError,run->restoreCommandResult,(unsigned long)run->restoreCommandError,run->stillIconic);
        SC_LogDiagnostic(diagnostic);
    }
    if (g_activeLaunch == run) {
        if (run->hotkeyId) Action_EndForegroundHotkey(run,3);
        g_activeLaunch = NULL; Action_ReleaseRun(run);
    }
    if (!g_actionStopped && run->generation == g_actionGeneration && g_actionBusy == 2) {
        g_actionBusy = 0;
        if (run->success) {
            if (run->command.id > 0) Dashboard_RecordUsage(run->command.id);
            if (run->foregroundError) {
                wchar_t name[SC_NAME_CAP], operation[SC_NAME_CAP+280];
                SC_WideFromUtf8(name,SC_NAME_CAP,run->command.name);
                swprintf(operation,sizeof(operation)/sizeof(operation[0]),
                    SC_Text(L"Window stayed in background: %ls",L"窗口未能切到前台：%ls"),name);
                size_t used = wcslen(operation);
                swprintf(operation+used,sizeof(operation)/sizeof(operation[0])-used,
                    L"\n[stage=%lu workerTid=%lu inputTid=%lu grant=%d grantError=%lu fg=%p]",
                    (unsigned long)run->foregroundStage,(unsigned long)run->workerThread,
                    (unsigned long)GetWindowThreadProcessId(run->replyTo,NULL),
                    run->grantResult,(unsigned long)run->grantError,(void*)GetForegroundWindow());
                if (run->foregroundStage == 2) {
                    used = wcslen(operation);
                    swprintf(operation+used,sizeof(operation)/sizeof(operation[0])-used,
                        L" (SetForegroundWindow=0; extended error unavailable)");
                }
                SC_ReportError(operation,run->foregroundError);
            }
        } else {
            wchar_t name[SC_NAME_CAP], operation[SC_NAME_CAP+64];
            SC_WideFromUtf8(name,SC_NAME_CAP,run->command.name);
            swprintf(operation,sizeof(operation)/sizeof(operation[0]),
                SC_Text(L"Open command: %ls",L"打开命令：%ls"),name);
            SC_ReportError(operation,run->error);
        }
    }
    Action_ReleaseRun(run);
}
static void Action_Failed(const wchar_t *operation, DWORD error)
{
    Macro_Release();
    g_actionBusy = 0;
    SC_ReportError(operation,error ? error : ERROR_GEN_FAILURE);
}
static int Macro_OnlyReleasesRemain(void)
{
    for(int i=g_macroPosition;i<g_macroCount;i++)
        if(!g_macro[i].delay && !g_macro[i].up)return 0;
    return 1;
}
void Action_Tick(void)
{
    if (g_actionStopped || g_actionTicking) return;
    g_actionTicking = 1;
    Action_CleanRetiredHotkey();
    if (g_actionBusy == 2) {
        if (g_activeLaunch && g_activeLaunch->hotkeyId &&
            (GetTickCount64() > g_activeLaunch->hotkeyDeadline ||
             !InterlockedCompareExchangePointer(&g_activeLaunch->requestedWindow,NULL,NULL) ||
             !Action_HasLaunchAttention(g_activeLaunch))) Action_EndForegroundHotkey(g_activeLaunch,3);
        if (g_activeLaunch && InterlockedCompareExchange(&g_activeLaunch->deliveryFailed,0,0)) {
            ActionRun *run = g_activeLaunch;
            InterlockedIncrement(&run->references);
            Action_HandleResult((LPARAM)run);
        }
        goto done;
    }
    if (g_actionBusy == 1) {
        ULONGLONG now = GetTickCount64();
        if (g_macroDue && now < g_macroDue) goto done;
        /* Window-switching shortcuts can change focus before their recorded
           key-up events. Finish that release-only tail without a false error. */
        if (GetForegroundWindow() != g_macroOrigin && !Macro_OnlyReleasesRemain()) {
            Action_Failed(SC_Text(L"Macro cancelled after focus changed",L"焦点变化，已取消按键宏"),ERROR_CANCELLED);
            goto done;
        }
        g_macroDue = 0;
        for (int budget = 0; budget < 64 && g_macroPosition < g_macroCount; budget++) {
            MacroStep *step = &g_macro[g_macroPosition++];
            if (step->delay) {
                g_macroDue = now + step->delay;
                goto done;
            }
            /* Focus can also change after SendInput within this same tick. */
            if (!step->up && GetForegroundWindow() != g_macroOrigin) {
                Action_Failed(SC_Text(L"Macro cancelled after focus changed",L"焦点变化，已取消按键宏"),ERROR_CANCELLED);
                goto done;
            }
            if (!Macro_Send(step)) {
                Action_Failed(SC_Text(L"Send macro keys",L"发送宏按键"),GetLastError() ? GetLastError() : ERROR_ACCESS_DENIED);
                goto done;
            }
        }
        if (g_macroPosition == g_macroCount) {
            Macro_Release();
            g_actionBusy = 0;
            Dashboard_RecordUsage(g_macroCommand);
        }
        goto done;
    }
    if (!g_jobCount) {
        if (!g_retiredHotkeyId) KillTimer(g_hWnd,SC_ACTION_TIMER);
        goto done;
    }
    ActionJob job = g_jobs[g_jobHead];
    g_jobHead = (g_jobHead + 1) % ACTION_QUEUE_MAX;
    g_jobCount--;
    LONG generation = InterlockedCompareExchange(&g_actionGeneration,0,0);
    g_actionBusy = 3; /* Confirmation can run a nested message loop. */
    if (job.command.confirmBeforeRun) {
        wchar_t name[SC_NAME_CAP], question[SC_NAME_CAP+128];
        SC_WideFromUtf8(name,SC_NAME_CAP,job.command.name);
        swprintf(question,sizeof(question)/sizeof(question[0]),SC_Text(L"Run \"%ls\"?",L"运行“%ls”？"),name);
        if (MessageBoxW(g_hWnd,question,SC_Text(L"Confirm command",L"确认运行"),MB_YESNO|MB_ICONQUESTION) != IDYES ||
            generation != g_actionGeneration || g_actionStopped) {
            if (generation == g_actionGeneration) g_actionBusy = 0;
            goto done;
        }
    }
    if (job.command.type == CMD_KEYS) {
        if(GetForegroundWindow()!=job.origin && (!IsWindow(job.origin) || !SetForegroundWindow(job.origin))) {
            g_actionBusy=0;SC_ReportError(SC_Text(L"Macro target window is unavailable",L"按键宏的目标窗口不可用"),ERROR_INVALID_WINDOW_HANDLE);goto done;
        }
        wchar_t error[256];
        if (!Macro_Parse(job.command.keySeq,g_macro,&g_macroCount,error,256)) {
            g_actionBusy = 0;
            SC_ReportError(error,ERROR_INVALID_DATA);
            goto done;
        }
        g_macroPosition = 0;
        g_macroDue = 0;
        g_macroCommand = job.command.id;
        g_macroOrigin = job.origin;
        g_actionBusy = 1;
    } else {
        ActionRun *run = HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof(*run));
        if (!run) { g_actionBusy = 0; SC_ReportError(SC_Text(L"Queue command",L"排队运行命令"),ERROR_NOT_ENOUGH_MEMORY); goto done; }
        run->command = job.command;
        run->replyTo = g_hWnd;
        run->origin = job.origin;
        run->inputSerial = job.inputSerial;
        run->generation = generation;
        run->references = 2;
        g_actionBusy = 2;
        g_activeLaunch = run;
        HANDLE thread = CreateThread(NULL,0,Action_RunWorker,run,0,NULL);
        if (thread) CloseHandle(thread);
        else {
            DWORD error = GetLastError();
            if (g_activeLaunch == run) g_activeLaunch = NULL;
            HeapFree(GetProcessHeap(),0,run);
            g_actionBusy = 0;
            SC_ReportError(SC_Text(L"Start command worker",L"启动命令任务"),error);
        }
    }
done:
    g_actionTicking = 0;
}
static void Action_Queue(const Command *command)
{
    if (g_actionStopped) return;
    if (g_jobCount == ACTION_QUEUE_MAX) {
        SC_ReportError(SC_Text(L"Command queue is full",L"命令队列已满"),ERROR_NOT_ENOUGH_QUOTA);
        return;
    }
    int index = (g_jobHead + g_jobCount) % ACTION_QUEUE_MAX;
    g_jobs[index].command = *command;
    g_jobs[index].origin = GetForegroundWindow();
    g_jobs[index].inputSerial = InterlockedCompareExchange(&g_actionInputSerial,0,0);
    g_jobCount++;
    if (!SetTimer(g_hWnd,SC_ACTION_TIMER,15,NULL)) {
        g_jobCount--;
        SC_ReportError(SC_Text(L"Start command timer",L"启动命令计时器"),GetLastError());
    }
}
void Action_Cancel(void)
{
    InterlockedIncrement(&g_actionGeneration);
    ActionRun *run = g_activeLaunch;
    if (run && run->hotkeyId) Action_EndForegroundHotkey(run,3);
    g_activeLaunch = NULL;
    if (run) Action_ReleaseRun(run);
    g_jobCount = g_jobHead = 0;
    Macro_Release();
    g_actionBusy = 0;
    if (g_retiredHotkeyId && !g_actionStopped) SetTimer(g_hWnd,SC_ACTION_TIMER,15,NULL);
    else KillTimer(g_hWnd,SC_ACTION_TIMER);
}
void Action_Shutdown(void)
{
    g_actionStopped = 1;
    Action_Cancel();
    MSG message;
    while (PeekMessageW(&message,g_hWnd,WM_SC_ACTIONRESULT,WM_SC_ACTIONRESULT,PM_REMOVE))
        Action_HandleResult(message.lParam);
}
void Action_ExecuteCommand(int cmdId)
{
    if (cmdId == CMDID_NONE || g_actionStopped) return;
    if (cmdId == CMDID_SETTINGS) { UI_Show(NULL); return; }
    if (cmdId == CMDID_TOGGLE) { SC_SetEnabled(!g_enabled); return; }
    if (cmdId == CMDID_DASHBOARD) { Dashboard_Show(); return; }
    if (cmdId == CMDID_CMDPROMPT || cmdId == CMDID_POWERSHELL) {
        Command command = {0};
        command.id = cmdId;
        command.type = CMD_RUN;
        command.runAsAdmin = 1;
        SC_CopyUtf8(command.name,sizeof(command.name),cmdId == CMDID_CMDPROMPT ? "Command Prompt" : "PowerShell");
        SC_CopyUtf8(command.runPath,sizeof(command.runPath),cmdId == CMDID_CMDPROMPT ? "cmd.exe" : "powershell.exe");
        Action_Queue(&command);
        return;
    }
    for (int i = 0; i < g_cfg.commandCount && i < MAX_COMMANDS; i++) {
        Command *command = &g_cfg.commands[i];
        if (command->id != cmdId) continue;
        if (command->type == CMD_RUN && !lstrcmpiA(command->runPath,"sc:dashboard")) {
            Dashboard_Show();
            if (Dashboard_IsVisible()) Dashboard_RecordUsage(cmdId);
            return;
        }
        if (command->type != CMD_RUN && command->type != CMD_SCRIPT && command->type != CMD_KEYS) {
            SC_ReportError(SC_Text(L"Invalid command type",L"命令类型无效"),ERROR_INVALID_DATA);
            return;
        }
        if (command->type == CMD_KEYS) {
            wchar_t error[256];
            if (!Action_ValidateKeySeq(command->keySeq,error,256)) {
                SC_ReportError(error,ERROR_INVALID_DATA);
                return;
            }
        }
        Action_Queue(command);
        return;
    }
    SC_ReportError(SC_Text(L"Command no longer exists",L"命令已不存在"),ERROR_NOT_FOUND);
}
void Action_ExecuteCustom(const GestureDef *gesture)
{
    if (gesture && gesture->enabled && g_enabled) Action_ExecuteCommand(gesture->commandId);
}
