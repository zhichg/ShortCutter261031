/* Real launch/queue code with simulated process, input and window boundaries.
   No applications, hooks, foreground changes or configuration writes. */
#include "../src/main.h"

AppConfig g_cfg;
int g_enabled = 1;
HWND g_hWnd = (HWND)(INT_PTR)7;
static int checks, failures, shells, activations, grants, closes, errors, usage;
static int denyActivation, denyGrant, hungWindow, shellFails, noProcess, cancelAt;
static int toolWindow, ownedForeground, inputUnavailable, changeInputAt, changeFocusAt;
static DWORD inputTime = 42;
static ULONGLONG now, visibleAt;
static HWND foreground;
static HWND pending;
static LPTHREAD_START_ROUTINE queuedWorker;
static void *queuedParam;
static LPARAM postedResult;
static ULONG shellFlags;
static UINT timeoutBudget;
static int inputThreadActive, denyWorkerOnly, brokerCalls, brokerTimeout, cancelInBroker;
static DWORD reportedError;
static wchar_t reportedOperation[1024];
static wchar_t lastDiagnostic[1024];
static int mouseMovementAt;
static int liveAllocations, failResultPost;
static int minimized, restoreDenied, restoreCommandDenied, restoreCommands, targetDuringRestore;
static ULONGLONG restoreAt;
static DWORD restoreDelay;
static LPVOID Allocate(HANDLE heap, DWORD flags, SIZE_T size)
{ LPVOID value = HeapAlloc(heap,flags,size); if (value) liveAllocations++; return value; }
static BOOL Release(HANDLE heap, DWORD flags, LPVOID value)
{ if (value) liveAllocations--; return HeapFree(heap,flags,value); }

static void Check(int condition, const char *description)
{ checks++; printf("%s: %s\n", condition ? "PASS" : "FAIL", description); if (!condition) failures++; }
static HWND Window(int value) { return (HWND)(INT_PTR)value; }
static ULONGLONG Clock(void) { return now; }
static void Pause(DWORD duration)
{
    now += duration;
    if (restoreAt && now >= restoreAt) { minimized = 0; restoreAt = 0; }
    if (changeInputAt && now >= (ULONGLONG)changeInputAt) { inputTime++; Action_NoteUserInput(); }
    if (mouseMovementAt && now >= (ULONGLONG)mouseMovementAt) inputTime++;
    if (changeFocusAt && now >= (ULONGLONG)changeFocusAt) foreground = Window(300);
    if (cancelAt && now >= (ULONGLONG)cancelAt) { cancelAt = 0; Action_Cancel(); }
}
static BOOL LastInput(PLASTINPUTINFO info)
{ info->dwTime = inputTime; return !inputUnavailable; }
static HWND Foreground(void) { return foreground; }
static HWND Ancestor(HWND window, UINT flags)
{ (void)flags; return ownedForeground && window == Window(201) ? Window(200) : window; }
static BOOL Enum(WNDENUMPROC callback, LPARAM parameter)
{
    if (!callback(Window(100), parameter)) return TRUE;
    if (now >= visibleAt) callback(Window(200), parameter);
    return TRUE;
}
static BOOL Visible(HWND window) { (void)window; return TRUE; }
static HWND Owner(HWND window, UINT command) { (void)window; (void)command; return NULL; }
static LONG_PTR WindowStyle(HWND window, int index)
{ (void)index; return toolWindow && window == Window(200) ? WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW : 0; }
static DWORD WindowPid(HWND window, LPDWORD pid)
{ if (pid) *pid = (DWORD)(INT_PTR)window; return 1; }
static HANDLE Open(DWORD access, BOOL inherit, DWORD pid)
{ (void)access; (void)inherit; return (HANDLE)(INT_PTR)pid; }
static BOOL Image(HANDLE process, DWORD flags, LPWSTR path, PDWORD length)
{
    (void)flags;
    lstrcpynW(path, process == (HANDLE)(INT_PTR)200 ? L"D:\\Apps\\app.exe" : L"D:\\Other\\app.exe", (int)*length);
    *length = (DWORD)wcslen(path); return TRUE;
}
static BOOL Close(HANDLE process) { if (process == (HANDLE)(INT_PTR)72) closes++; return TRUE; }
static DWORD Pid(HANDLE process) { return process == (HANDLE)(INT_PTR)72 ? 72 : 0; }
static BOOL Grant(DWORD pid)
{
    if (pid != 72) failures++;
    grants++;
    if (denyGrant || (denyWorkerOnly && !inputThreadActive)) { SetLastError(ERROR_ACCESS_DENIED); return FALSE; }
    return TRUE;
}
static BOOL Iconic(HWND window) { return window == Window(200) && minimized; }
static BOOL Restore(HWND window, int show)
{
    if (window != Window(200) || show != SW_RESTORE) failures++;
    if (restoreDenied) { SetLastError(ERROR_ACCESS_DENIED); return FALSE; }
    if (targetDuringRestore) foreground = Window(200);
    if (restoreDelay) restoreAt = now+restoreDelay; else minimized = 0;
    return TRUE;
}
static BOOL Activate(HWND window)
{ activations++; pending = window; return !denyActivation && (!denyWorkerOnly || inputThreadActive); }
static LRESULT Handshake(HWND window, UINT message, WPARAM wp, LPARAM lp, UINT flags, UINT timeout, PDWORD_PTR result)
{
    (void)window; (void)wp; (void)lp; (void)flags;
    if (message == WM_SYSCOMMAND) {
        restoreCommands++;
        if (wp != SC_RESTORE || lp != 0 || timeout > 200) failures++;
        if (restoreCommandDenied) { SetLastError(ERROR_ACCESS_DENIED); return 0; }
        minimized = 0;
        if (result) *result = 0;
        return 1;
    }
    if (message == WM_SC_FOREGROUND || message == WM_SC_FOREGROUNDGRANT) {
        brokerCalls++;
        if (brokerTimeout) { Pause(timeout); SetLastError(ERROR_TIMEOUT); return 0; }
        if (cancelInBroker) { cancelInBroker = 0; Action_Cancel(); }
        inputThreadActive = 1;
        if (result) *result = (DWORD_PTR)Action_HandleForeground(message,wp,lp);
        inputThreadActive = 0;
        return 1;
    }
    if (message != WM_NULL) failures++;
    timeoutBudget = timeout;
    if (hungWindow) { Pause(timeout); SetLastError(ERROR_TIMEOUT); return 0; }
    foreground = ownedForeground ? Window(201) : pending;
    if (result) *result = 0;
    return 1;
}
static BOOL Flash(PFLASHWINFO info) { (void)info; return TRUE; }
static BOOL LaunchShell(SHELLEXECUTEINFOW *info)
{
    shells++; shellFlags = info->fMask;
    info->hProcess = noProcess || shellFails ? NULL : (HANDLE)(INT_PTR)72;
    if (shellFails) SetLastError(ERROR_FILE_NOT_FOUND);
    return !shellFails;
}
static DWORD Attributes(LPCWSTR path)
{ return wcsstr(path, L".exe") ? FILE_ATTRIBUTE_NORMAL : INVALID_FILE_ATTRIBUTES; }
static DWORD Search(LPCWSTR directory, LPCWSTR path, LPCWSTR extension, DWORD capacity, LPWSTR value, LPWSTR *part)
{ (void)directory; (void)path; (void)extension; (void)part; lstrcpynW(value, L"D:\\Apps\\app.exe", (int)capacity); return (DWORD)wcslen(value); }
static DWORD Full(LPCWSTR path, DWORD capacity, LPWSTR value, LPWSTR *part)
{ (void)part; lstrcpynW(value, path, (int)capacity); return (DWORD)wcslen(value); }
static HANDLE Thread(LPSECURITY_ATTRIBUTES security, SIZE_T stack, LPTHREAD_START_ROUTINE function, LPVOID parameter, DWORD flags, LPDWORD id)
{ (void)security; (void)stack; (void)flags; (void)id; queuedWorker = function; queuedParam = parameter; return (HANDLE)(INT_PTR)11; }
static BOOL Post(HWND window, UINT message, WPARAM wp, LPARAM lp)
{
    (void)window; (void)wp;
    if (message == WM_SC_ACTIONRESULT) {
        if (failResultPost) { SetLastError(ERROR_NOT_ENOUGH_QUOTA); return FALSE; }
        postedResult = lp;
    }
    return TRUE;
}
static UINT_PTR Timer(HWND window, UINT_PTR id, UINT interval, TIMERPROC procedure)
{ (void)window; (void)interval; (void)procedure; return id; }
static BOOL Kill(HWND window, UINT_PTR id) { (void)window; (void)id; return TRUE; }
static HRESULT Com(void *reserved, DWORD flags) { (void)reserved; (void)flags; return S_OK; }
static void Uncom(void) { }
static UINT Input(UINT count, LPINPUT input, int size)
{ (void)count; (void)input; (void)size; failures++; return 0; }
static SHORT KeyState(int key) { (void)key; return 0; }
static BOOL Register(HWND window, int id, UINT mods, UINT key)
{ (void)window; (void)id; (void)mods; (void)key; SetLastError(ERROR_HOTKEY_ALREADY_REGISTERED); return FALSE; }
static BOOL Unregister(HWND window, int id) { (void)window; (void)id; failures++; return TRUE; }

#define GetTickCount64 Clock
#define Sleep Pause
#define GetLastInputInfo LastInput
#define GetForegroundWindow Foreground
#define GetAncestor Ancestor
#define EnumWindows Enum
#define IsWindowVisible Visible
#define GetWindow Owner
#define GetWindowLongPtrW WindowStyle
#define GetWindowThreadProcessId WindowPid
#define OpenProcess Open
#define QueryFullProcessImageNameW Image
#define CloseHandle Close
#define GetProcessId Pid
#define AllowSetForegroundWindow Grant
#define IsIconic Iconic
#define ShowWindowAsync Restore
#define SetForegroundWindow Activate
#define SendMessageTimeoutW Handshake
#define FlashWindowEx Flash
#define ShellExecuteExW LaunchShell
#define GetFileAttributesW Attributes
#define SearchPathW Search
#define GetFullPathNameW Full
#define CreateThread Thread
#define PostMessageW Post
#define SetTimer Timer
#define KillTimer Kill
#define CoInitializeEx Com
#define CoUninitialize Uncom
#define SendInput Input
#define GetAsyncKeyState KeyState
#define RegisterHotKey Register
#define UnregisterHotKey Unregister
#define HeapAlloc Allocate
#define HeapFree Release
#ifdef SC_FOREGROUND_BEFORE
#include "../design-review/foreground-activation-2026-10-25/before/actions.c"
#elif defined(SC_INPUT_THREAD_BEFORE)
#include "../design-review/foreground-input-thread-2026-10-25/before/actions.c"
#elif defined(SC_INPUT_CANCEL_BEFORE)
#include "../design-review/foreground-input-cancel-2026-10-25/before/actions.c"
#else
#include "../src/actions.c"
#endif
#if defined(SC_FOREGROUND_BEFORE) || defined(SC_INPUT_THREAD_BEFORE) || defined(SC_INPUT_CANCEL_BEFORE)
void Action_NoteUserInput(void) { }
#endif
#if defined(SC_FOREGROUND_BEFORE) || defined(SC_INPUT_THREAD_BEFORE)
LRESULT Action_HandleForeground(UINT message, WPARAM target, LPARAM generation)
{ (void)message; (void)target; (void)generation; return 0; }
#endif

void UI_Show(HWND parent) { (void)parent; }
void Dashboard_Show(void) { }
int Dashboard_IsVisible(void) { return FALSE; }
void Dashboard_RecordUsage(int id) { (void)id; usage++; }
void SC_ReportError(const wchar_t *operation, DWORD error)
{ lstrcpynW(reportedOperation,operation,1024); reportedError = error; errors++; }
void SC_LogDiagnostic(const wchar_t *message) { lstrcpynW(lastDiagnostic,message,1024); }
void SC_SetEnabled(int enabled) { g_enabled = !!enabled; }

static Command *Reset(void)
{
    Action_Cancel(); memset(&g_cfg, 0, sizeof(g_cfg));
    shells = activations = grants = closes = errors = usage = 0;
    denyActivation = denyGrant = hungWindow = shellFails = noProcess = cancelAt = 0;
    toolWindow = ownedForeground = inputUnavailable = changeInputAt = changeFocusAt = 0;
    now = 1000; visibleAt = 1300; inputTime = 42; foreground = Window(1); pending = NULL;
    queuedWorker = NULL; queuedParam = NULL; postedResult = 0; timeoutBudget = 0; shellFlags = 0;
    inputThreadActive = denyWorkerOnly = brokerCalls = brokerTimeout = cancelInBroker = 0;
    reportedError = 0; reportedOperation[0] = 0;
    failResultPost = 0;
    mouseMovementAt = 0; lastDiagnostic[0] = 0;
    minimized = restoreDenied = restoreCommandDenied = restoreCommands = targetDuringRestore = 0;
    restoreAt = 0; restoreDelay = 0;
    g_enabled = 1; g_actionStopped = 0;
    g_cfg.commandCount = 1;
    Command *command = &g_cfg.commands[0];
    command->id = 91; command->type = CMD_RUN; command->activateExisting = 1;
    SC_CopyUtf8(command->name, sizeof(command->name), "Foreground fixture");
    SC_CopyUtf8(command->runPath, sizeof(command->runPath), "app.exe");
    return command;
}
static void Run(void)
{
    Action_ExecuteCommand(91); Action_Tick();
    if (queuedWorker) queuedWorker(queuedParam);
    if (postedResult) { LPARAM result = postedResult; postedResult = 0; Action_HandleResult(result); }
}
int main(void)
{
    Reset(); Run();
    Check(shells == 1 && activations == 1 && foreground == Window(200), "delayed main window is activated after its launcher returns");
    Check(now >= visibleAt && now <= 3500, "window discovery has a bounded wait on the launch worker");
    Check((shellFlags & SEE_MASK_NOCLOSEPROCESS) && grants == 1 && closes == 1, "launch process receives explicit foreground permission and its handle is closed");
    Check(usage == 1 && !errors, "successful delayed activation records usage once");
    Reset(); noProcess = 1; Run();
    Check(shells == 1 && foreground == Window(200) && !grants && !closes, "single-instance Shell launch without a process handle still discovers the UI");
    Reset(); changeFocusAt = 1100; Run();
    Check(!activations && foreground == Window(300), "switching applications cancels delayed activation");
    Reset(); changeInputAt = 1100; Run();
    Check(!activations && foreground == Window(1), "new user input cancels delayed activation even in the same application");
    Reset(); cancelAt = 1100; Run();
    Check(!activations && !usage && !errors && closes == 1, "cancelling the command prevents later focus changes and releases the process handle");
    Reset(); visibleAt = 100000; Run();
    Check(!activations && now <= 3500 && usage == 1 && !errors && closes == 1, "a headless or different-executable launcher expires quietly without failing the launch");
    Reset(); denyActivation = 1; Run();
    Check(activations == 1 && errors == 1 && shells == 1 && closes == 1, "denied activation is reported without repeatedly launching the application");
    Reset(); hungWindow = 1; Run();
    Check(activations == 1 && now <= 3500 && timeoutBudget <= 500 && errors == 1, "an unresponsive UI cannot turn foreground confirmation into an unbounded wait");
    Reset(); toolWindow = 1; Run();
    Check(!activations && usage == 1 && !errors, "no-activate helper windows are never mistaken for an application UI");
    Reset(); ownedForeground = 1; Run();
    Check(activations == 1 && foreground == Window(201) && !errors, "an owned foreground dialog counts as successful application activation");
    Reset(); denyGrant = 1; Run();
    Check(grants == 1 && foreground == Window(200) && !errors, "a rejected launch grant does not suppress direct window activation");
    Reset(); inputUnavailable = 1; Run();
    Check(foreground == Window(200) && shells == 1 && closes == 1,
        "unavailable idle timestamp does not suppress activation when physical-input serial is unchanged");
    Command *command = Reset(); command->activateExisting = 0; Run();
    Check(shells == 1 && !activations && now == 1000, "explicit new-instance commands retain their original launch behavior");
    command = Reset(); SC_CopyUtf8(command->arguments, sizeof(command->arguments), "--background"); Run();
    Check(shells == 1 && !activations && !grants && now == 1000, "commands with arguments retain their original activation semantics");
    command = Reset(); command->runAsAdmin = 1; Run();
    Check(shells == 1 && !activations && !grants && now == 1000, "administrator launches are not followed by delayed foreground requests");
    command = Reset(); SC_CopyUtf8(command->runPath, sizeof(command->runPath), "D:\\Windows\\explorer.exe"); Run();
    Check(shells == 1 && !activations && !grants && now == 1000, "Explorer keeps its Shell launch semantics even with a full executable path");
    Reset(); shellFails = 1; Run();
    Check(shells == 1 && !activations && !grants && errors == 1 && !usage && now == 1000, "failed launches do not start a foreground watcher");
    Reset(); visibleAt = 1000; Run();
    Check(shells == 0 && activations == 1 && foreground == Window(200) && usage == 1, "an existing window still activates directly without launching another instance");
    Reset(); denyWorkerOnly = 1; Run();
    Check(foreground == Window(200) && !errors && brokerCalls == 2,
        "launch grant and delayed activation execute on the input thread when the worker has no foreground permission");
    Reset(); denyWorkerOnly = 1; visibleAt = 1000; Run();
    Check(foreground == Window(200) && !shells && brokerCalls == 1,
        "an existing window is activated by the input thread without launching another instance");
    Reset(); denyActivation = 1; Run();
    Check(errors == 1 && reportedError != ERROR_ACCESS_DENIED && wcsstr(reportedOperation,L"SetForegroundWindow=0"),
        "a zero foreground return is reported without inventing an access-denied LastError");
    Reset(); brokerTimeout = 1; Run();
    Check(!activations && now <= 3500 && errors == 1 && reportedError == ERROR_TIMEOUT,
        "an unresponsive input thread cannot leave an unbounded activation request");
    Reset(); cancelInBroker = 1; Run();
    Check(!activations && !grants && !usage && !errors && closes == 1,
        "cancellation before dispatch prevents both the grant and foreground operation");
    Check(Action_HandleForeground(WM_SC_FOREGROUND,(WPARAM)Window(300),0) == 0 && !activations,
        "unsolicited foreground messages cannot supply an arbitrary active request");
    Check(!liveAllocations,"all completed and cancelled requests release their launch allocations");
    Reset(); failResultPost = 1; Run(); Action_Tick();
    Check(usage == 1 && !liveAllocations && !g_actionBusy,
        "a failed result PostMessage is recovered by the input-thread timer without leaking active request state");
    Reset(); failResultPost = 1; Run(); Action_Cancel();
    Check(!usage && !liveAllocations,"cancellation also releases state after result delivery fails");
    Reset(); mouseMovementAt = 1100; Run();
    Check(foreground == Window(200) && usage == 1 && !errors,
        "continuous pointer movement after releasing a gesture does not cancel activation of its delayed UI");
    Check(wcsstr(lastDiagnostic,L"status=1") && wcsstr(lastDiagnostic,L"idleBefore=42") && !wcsstr(lastDiagnostic,L"idleNow=42"),
        "diagnostic records verified foreground despite the changing mouse idle timestamp");
    Reset(); mouseMovementAt = 1100; changeInputAt = 1200; Run();
    Check(!activations && wcsstr(lastDiagnostic,L"status=2") && !errors,
        "a deliberate key or button action still cancels activation and records the cancellation reason");
    Reset(); visibleAt = 1000; minimized = 1; restoreDelay = 120; Run();
    Check(!minimized && foreground == Window(200) && !shells && !errors && now >= 1120,
        "existing minimized UI is confirmed restored before completing foreground activation");
    Reset(); visibleAt = 1000; minimized = 1; restoreDenied = 1; Run();
    Check(!minimized && restoreCommands == 1 && foreground == Window(200) && !shells && !errors,
        "a rejected asynchronous restore uses one bounded standard restore command");
    Reset(); visibleAt = 1000; minimized = 1; restoreDelay = 120; targetDuringRestore = 1; Run();
    Check(!minimized && foreground == Window(200) && !shells && !errors && now >= 1120 && wcsstr(lastDiagnostic,L"status=1"),
        "expected target foreground while restoration is pending is not mistaken for user cancellation");
    Reset(); visibleAt = 1000; minimized = 1; restoreDenied = restoreCommandDenied = 1; Run();
    Check(minimized && !activations && errors == 1 && now <= 3500 && wcsstr(lastDiagnostic,L"stillIconic=1") &&
        wcsstr(lastDiagnostic,L"restoreCommandError=5"),
        "denied restoration remains bounded and is diagnosed instead of falsely claiming foreground success");
    Reset(); visibleAt = 1000; minimized = 1; restoreDelay = 300; changeInputAt = 1100; Run();
    Check(!activations && wcsstr(lastDiagnostic,L"status=2"),
        "new deliberate input during restoration cancels later foreground changes");
    Reset(); visibleAt = 1000; minimized = 1; restoreDelay = 300; changeFocusAt = 1100; Run();
    Check(!activations && foreground == Window(300) && wcsstr(lastDiagnostic,L"status=3"),
        "an unrelated foreground change during restoration still cancels activation");
    printf("Checks: %d; Failures: %d\n", checks, failures);
    return failures ? 1 : 0;
}
