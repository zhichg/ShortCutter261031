/* Exercise foreground hotkey recovery against simulated OS boundaries only.
   No windows, hooks, real hotkeys, injected input or user configuration. */
#include "../src/main.h"

AppConfig g_cfg;
int g_enabled = 1;
HWND g_hWnd = (HWND)(INT_PTR)7;
static int checks, failures, registered, registeredId, registrations, unregistrations;
static int activationCalls, inputCalls, releaseCalls, insideHotkey;
static int conflict, heldKey, denyHotkey, deliverHotkey, cancelDuringWait, directAllowed;
static int switchDuringWait, keyDuringWait, validWindow;
static int unregisterFailures, releaseReturn, injectedF24Down, timerCalls;
static DWORD targetPid, targetThread;
static wchar_t lastDiagnostic[512];
static UINT inserted;
static ULONGLONG clockValue;
static HWND foreground;
static LPARAM hotkeyKey;
static HWND Target(void) { return (HWND)(INT_PTR)200; }
static HWND Origin(void) { return (HWND)(INT_PTR)1; }
static void Check(int condition, const char *description)
{ checks++; printf("%s: %s\n",condition ? "PASS" : "FAIL",description); if (!condition) failures++; }
static ULONGLONG Clock(void) { return clockValue; }
static HWND Foreground(void) { return foreground; }
static HWND Ancestor(HWND window, UINT flags) { (void)flags; return window; }
static DWORD WindowThread(HWND window, LPDWORD pid)
{ if (pid) *pid = window == Target() ? targetPid : 7; return window == Target() ? targetThread : 17; }
static SHORT PhysicalState(int key)
{ return key == heldKey || (key == VK_F24 && injectedF24Down) ? (SHORT)0x8000 : 0; }
static BOOL WindowValid(HWND window) { return window == g_hWnd || (validWindow && window == Target()); }
static BOOL Register(HWND window, int id, UINT modifiers, UINT key)
{
    registrations++;
    if (window != g_hWnd || modifiers != MOD_NOREPEAT || key != VK_F24 || registered) failures++;
    if (conflict) { SetLastError(ERROR_HOTKEY_ALREADY_REGISTERED); return FALSE; }
    registered = 1; registeredId = id; return TRUE;
}
static BOOL Unregister(HWND window, int id)
{
    unregistrations++;
    if (window != g_hWnd || !registered || id != registeredId) failures++;
    if (unregisterFailures) {
        if (unregisterFailures > 0) unregisterFailures--;
        SetLastError(ERROR_ACCESS_DENIED); return FALSE;
    }
    registered = 0; return TRUE;
}
static UINT Input(UINT count, LPINPUT input, int size)
{
    if (size != sizeof(INPUT)) failures++;
    if (count == 1) {
        releaseCalls++;
        if (input[0].type != INPUT_KEYBOARD || input[0].ki.wVk != VK_F24 ||
            input[0].ki.dwFlags != KEYEVENTF_KEYUP || !registered) failures++;
        if (releaseReturn) injectedF24Down = 0;
        else SetLastError(ERROR_ACCESS_DENIED);
        return (UINT)releaseReturn;
    }
    inputCalls++;
    if (!registered || count != 2 || input[0].type != INPUT_KEYBOARD || input[1].type != INPUT_KEYBOARD ||
        input[0].ki.wVk != VK_F24 || input[1].ki.wVk != VK_F24 || input[0].ki.dwFlags ||
        input[1].ki.dwFlags != KEYEVENTF_KEYUP) failures++;
    if (inserted != 2) SetLastError(ERROR_ACCESS_DENIED);
    if (inserted == 1) injectedF24Down = 1;
    return inserted;
}
static BOOL Activate(HWND window)
{
    activationCalls++;
    if (window != Target()) failures++;
    if ((!insideHotkey && !directAllowed) || denyHotkey) return FALSE;
    foreground = window; return TRUE;
}
static void Pause(DWORD duration)
{
    clockValue += duration;
    if (cancelDuringWait) { cancelDuringWait = 0; Action_Cancel(); }
    if (switchDuringWait) { switchDuringWait = 0; foreground = (HWND)(INT_PTR)300; }
    if (keyDuringWait) { keyDuringWait = 0; Action_NoteUserInput(); }
    if (deliverHotkey && registered) {
        deliverHotkey = 0; insideHotkey = 1;
        Action_HandleForegroundHotkey((WPARAM)registeredId,hotkeyKey);
        insideHotkey = 0;
    }
}
static LRESULT Send(HWND window, UINT message, WPARAM wp, LPARAM lp, UINT flags, UINT timeout, PDWORD_PTR result)
{
    (void)flags; (void)timeout;
    if (window != g_hWnd || message != WM_SC_FOREGROUND) { failures++; return 0; }
    *result = (DWORD_PTR)Action_HandleForeground(message,wp,lp); return 1;
}
static BOOL Kill(HWND window, UINT_PTR id) { (void)window; (void)id; return TRUE; }
static UINT_PTR Timer(HWND window, UINT_PTR id, UINT interval, TIMERPROC procedure)
{ (void)interval; (void)procedure; if (window != g_hWnd || id != SC_ACTION_TIMER) failures++; timerCalls++; return id; }
static BOOL Iconic(HWND window) { (void)window; return FALSE; }
static BOOL Restore(HWND window, int show) { (void)window; (void)show; failures++; return FALSE; }
static BOOL Peek(LPMSG message, HWND window, UINT first, UINT last, UINT remove)
{ (void)message; (void)window; (void)first; (void)last; (void)remove; return FALSE; }

#define GetTickCount64 Clock
#define GetForegroundWindow Foreground
#define GetAncestor Ancestor
#define GetWindowThreadProcessId WindowThread
#define GetAsyncKeyState PhysicalState
#define IsWindow WindowValid
#define RegisterHotKey Register
#define UnregisterHotKey Unregister
#define SendInput Input
#define SetForegroundWindow Activate
#define Sleep Pause
#define SendMessageTimeoutW Send
#define KillTimer Kill
#define SetTimer Timer
#define IsIconic Iconic
#define ShowWindowAsync Restore
#define PeekMessageW Peek
#include "../src/actions.c"

void UI_Show(HWND window) { (void)window; }
void Dashboard_Show(void) { }
int Dashboard_IsVisible(void) { return FALSE; }
void Dashboard_RecordUsage(int id) { (void)id; }
void SC_ReportError(const wchar_t *operation, DWORD error) { (void)operation; (void)error; failures++; }
void SC_LogDiagnostic(const wchar_t *message) { lstrcpynW(lastDiagnostic,message,512); }
void SC_SetEnabled(int enabled) { g_enabled = !!enabled; }

static void Prepare(ActionRun *run)
{
    memset(run,0,sizeof(*run));
    run->replyTo = g_hWnd; run->origin = foreground; run->references = 2;
    run->generation = g_actionGeneration; run->inputSerial = g_actionInputSerial;
    run->requestedWindow = Target(); g_activeLaunch = run; g_actionBusy = 2; g_actionStopped = 0;
}
static void Reset(ActionRun *run)
{
    if (g_activeLaunch) Action_Cancel();
    Check(!registered,"previous request leaves no registered hotkey");
    registrations = unregistrations = activationCalls = inputCalls = releaseCalls = 0;
    conflict = heldKey = denyHotkey = deliverHotkey = cancelDuringWait = directAllowed = 0;
    switchDuringWait = keyDuringWait = insideHotkey = 0;
    unregisterFailures = injectedF24Down = timerCalls = 0;
    releaseReturn = 1; targetPid = 42; targetThread = 99; lastDiagnostic[0] = 0;
    inserted = 2; clockValue = 1000; foreground = Origin(); validWindow = 1;
    hotkeyKey = MAKELPARAM(0,VK_F24);
    Prepare(run);
}
static LRESULT Begin(ActionRun *run)
{ return Action_HandleForeground(WM_SC_FOREGROUND,(WPARAM)Target(),run->generation); }
static void Deliver(ActionRun *run)
{
    insideHotkey = 1; Action_HandleForegroundHotkey((WPARAM)run->hotkeyId,hotkeyKey); insideHotkey = 0;
}
int main(void)
{
    ActionRun run;
    Reset(&run);
    Check(Begin(&run) == 3 && activationCalls == 1 && registered && inputCalls == 1,
        "denied direct activation starts one asynchronous F24 recovery");
    Check(Begin(&run) == 3 && activationCalls == 1 && inputCalls == 1 && registrations == 1,
        "repeated request while pending does not register or inject twice");
    Deliver(&run);
    Check(foreground == Target() && run.hotkeyState == 2 && activationCalls == 2 &&
        !registered && unregistrations == 1,"system hotkey handler activates only the pending target and unregisters");
    int oldId = registeredId;
    Action_HandleForegroundHotkey((WPARAM)oldId,hotkeyKey);
    Check(activationCalls == 2,"completed hotkey messages cannot repeat activation");

    Reset(&run); directAllowed = 1;
    Check(Begin(&run) == 2 && !registrations && !inputCalls && foreground == Target(),
        "ordinary foreground success does not register or inject a recovery hotkey");

    Reset(&run); conflict = 1;
    Check(Begin(&run) == 1 && registrations == 1 && !inputCalls && !registered &&
        run.hotkeyError == ERROR_HOTKEY_ALREADY_REGISTERED,"registration conflict does not inject any key");
    Reset(&run); heldKey = 'A';
    Check(Begin(&run) == 1 && !registrations && !inputCalls && !releaseCalls && run.hotkeyError == ERROR_BUSY,
        "a held ordinary physical key prevents recovery and is never released");
    Reset(&run); heldKey = VK_CONTROL;
    Check(Begin(&run) == 1 && !registrations && !inputCalls && !releaseCalls,
        "a held modifier prevents recovery without changing its state");
    Reset(&run); heldKey = VK_F24;
    Check(Begin(&run) == 1 && !registrations && !inputCalls && !releaseCalls,
        "physical F24 is never borrowed or released");
    Reset(&run); inserted = 0;
    Check(Begin(&run) == 1 && inputCalls == 1 && !releaseCalls && !registered && unregistrations == 1,
        "zero injected events clean up registration without sending a spurious release");
    Reset(&run); inserted = 1;
    Check(Begin(&run) == 1 && inputCalls == 1 && releaseCalls == 1 && !registered && unregistrations == 1,
        "partial injection repairs only its own F24 release and cleans up");

    Reset(&run); Begin(&run); oldId = run.hotkeyId; Action_Cancel();
    Action_HandleForegroundHotkey((WPARAM)oldId,hotkeyKey);
    Check(!registered && unregistrations == 1 && activationCalls == 1 && !g_activeLaunch,
        "cancel unregisters pending recovery and rejects the late hotkey");
    Reset(&run); Begin(&run);
    Action_HandleForegroundHotkey((WPARAM)oldId,hotkeyKey);
    Check(registered && run.hotkeyState == 1 && activationCalls == 1,
        "a previous request ID cannot activate or cancel a newer request");
    Deliver(&run); Check(run.hotkeyState == 2 && foreground == Target(),"new request remains independently usable");

    Reset(&run); run.requestedWindow = NULL;
    Check(Begin(&run) == 0 && !activationCalls && !registrations && !inputCalls,
        "unsolicited target messages without a registered worker request are rejected");
    Reset(&run);
    Check(Action_HandleForeground(WM_SC_FOREGROUND,(WPARAM)(INT_PTR)300,run.generation) == 0 && !activationCalls,
        "a message cannot replace the worker's pre-registered target");
    Check(Action_HandleForeground(WM_SC_FOREGROUND,(WPARAM)Target(),run.generation+1) == 0 && !activationCalls,
        "stale generations cannot start activation or recovery");

    Reset(&run); Begin(&run); Action_NoteUserInput(); Deliver(&run);
    Check(!registered && run.hotkeyState == 3 && activationCalls == 1 && foreground == Origin(),
        "new deliberate input cancels recovery before foreground activation");
    Reset(&run); Begin(&run); foreground = (HWND)(INT_PTR)300; Deliver(&run);
    Check(!registered && run.hotkeyState == 3 && activationCalls == 1,
        "switching foreground applications cancels recovery before activation");
    Reset(&run); Begin(&run); run.requestedWindow = NULL; Deliver(&run);
    Check(!registered && run.hotkeyState == 3 && activationCalls == 1,
        "withdrawn worker request cannot be activated by an arriving hotkey");
    Reset(&run); Begin(&run); clockValue = run.hotkeyDeadline+1; Deliver(&run);
    Check(!registered && run.hotkeyState == 3 && activationCalls == 1,
        "expired system hotkey is discarded and unregistered");
    Reset(&run); Begin(&run); clockValue = run.hotkeyDeadline+1; Action_Tick();
    Check(!registered && run.hotkeyState == 3 && unregistrations == 1,
        "input-thread timer releases a registration when no hotkey arrives");
    Reset(&run); Begin(&run); validWindow = 0; Deliver(&run);
    Check(!registered && run.hotkeyState == 3 && activationCalls == 1,
        "destroyed target window is rejected before foreground activation");
    Reset(&run); Begin(&run); targetPid++; Deliver(&run);
    Check(!registered && run.hotkeyState == 3 && activationCalls == 1,
        "reused window handle with a changed process is rejected");
    Reset(&run); Begin(&run); targetThread++; Deliver(&run);
    Check(!registered && run.hotkeyState == 3 && activationCalls == 1,
        "reused window handle with a changed thread is rejected");
    Reset(&run); Begin(&run);
    Action_HandleForegroundHotkey((WPARAM)run.hotkeyId,MAKELPARAM(MOD_CONTROL,VK_F24));
    Action_HandleForegroundHotkey((WPARAM)run.hotkeyId,MAKELPARAM(0,VK_F23));
    Check(registered && run.hotkeyState == 1 && activationCalls == 1,
        "mismatched key or modifiers cannot activate the pending target");
    Deliver(&run);
    Reset(&run); Begin(&run); denyHotkey = 1; Deliver(&run);
    Check(!registered && run.hotkeyState == 3 && activationCalls == 2 && inputCalls == 1,
        "a denied recovery does not loop or retain a registration");

    Reset(&run); deliverHotkey = 1; DWORD error = 0;
    Check(Action_RequestInputThread(&run,Target(),0,200,&error) && error == ERROR_SUCCESS &&
        clockValue < 1200 && !registered && !run.requestedWindow,
        "worker observes successful asynchronous recovery and clears its request");
    Reset(&run); error = 0;
    Check(!Action_RequestInputThread(&run,Target(),0,200,&error) && clockValue == 1200 && !run.requestedWindow,
        "missing hotkey cannot leave the worker waiting indefinitely");
    Action_Tick(); Check(!registered && unregistrations == 1,"timer removes registration after worker timeout");
    Reset(&run); cancelDuringWait = 1; deliverHotkey = 1;
    Check(!Action_RequestInputThread(&run,Target(),0,200,&error) && !registered && activationCalls == 1,
        "cancellation during worker wait prevents recovery activation");
    Reset(&run); switchDuringWait = 1; deliverHotkey = 1;
    Check(!Action_RequestInputThread(&run,Target(),0,200,&error) && !registered && activationCalls == 1,
        "foreground change during worker wait prevents recovery activation");
    Reset(&run); keyDuringWait = 1; deliverHotkey = 1;
    Check(!Action_RequestInputThread(&run,Target(),0,200,&error) && !registered && activationCalls == 1,
        "new physical input during worker wait prevents recovery activation");
    Check(!Action_HandleForegroundHotkey(HKID_TOGGLE,hotkeyKey),"ordinary application hotkeys are left to their own handler");

    Reset(&run); Begin(&run); oldId = run.hotkeyId; unregisterFailures = 1; Deliver(&run);
    Check(registered && g_retiredHotkeyId == oldId && run.hotkeyId == 0 && run.hotkeyState == 2,
        "failed unregister preserves the retired registration independently of completed request state");
    Action_Tick();
    Check(!registered && !g_retiredHotkeyId && unregistrations == 2,
        "timer retries a one-time unregister failure and releases the retired hotkey");

    Reset(&run); Begin(&run); oldId = run.hotkeyId; unregisterFailures = -1; Deliver(&run);
    Check(registered && g_retiredHotkeyId == oldId,"persistent unregister failure retains the exact registration ID");
    Action_Cancel();
    Check(registered && g_retiredHotkeyId == oldId && timerCalls > 0,
        "cancellation retains failed cleanup and schedules a retry without losing ownership");
    foreground = Origin(); Prepare(&run);
    Check(Begin(&run) == 1 && registrations == 1 && inputCalls == 1 && run.hotkeyError == ERROR_BUSY,
        "next recovery is blocked while an earlier registration cannot be released");
    unregisterFailures = 0; Action_Tick();
    Check(!registered && !g_retiredHotkeyId,"timer eventually releases persistently retired registration when OS permits");
    Action_Cancel(); foreground = Origin(); Prepare(&run);
    Check(Begin(&run) == 3 && registrations == 2 && inputCalls == 2,
        "a fresh recovery becomes usable after retired cleanup succeeds");
    Deliver(&run);

    Reset(&run); inserted = 1; releaseReturn = 0;
    Check(Begin(&run) == 1 && inputCalls == 1 && releaseCalls == 3 && !registered &&
        injectedF24Down && g_foregroundKeyReleaseFailed && wcsstr(lastDiagnostic,L"F24 release failed"),
        "three failed repair releases stop retrying, log the failure and block further injection");
    Action_Cancel(); foreground = Origin(); Prepare(&run); inserted = 2; releaseReturn = 1;
    Check(Begin(&run) == 1 && registrations == 1 && inputCalls == 1 && releaseCalls == 3 &&
        g_foregroundKeyReleaseFailed,"next recovery stays blocked while unreleased injected F24 remains down");
    injectedF24Down = 0; Action_Cancel(); foreground = Origin(); Prepare(&run);
    Check(Begin(&run) == 3 && !g_foregroundKeyReleaseFailed && registrations == 2 && inputCalls == 2,
        "observed F24 release clears the injection block for a fresh recovery");
    Deliver(&run);

    Reset(&run); Begin(&run); Action_Shutdown();
    Check(!registered && unregistrations == 1 && !g_activeLaunch,
        "shutdown unregisters pending foreground recovery");
    Action_Cancel(); Check(!registered,"fixture ends without any simulated registered hotkey");
    printf("Checks: %d; Failures: %d\n",checks,failures);
    return failures ? 1 : 0;
}
