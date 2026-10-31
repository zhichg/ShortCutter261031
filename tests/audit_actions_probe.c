/* Read-only audit probe. Never injects input, opens apps, touches the INI,
   installs hooks or creates UI. Calls the real key-sequence parser. */
#include "../src/main.h"
static int sent, sends, sleepMs, opened, activated;
static WORD vks[256];
static UINT ProbeSendInput(UINT count, LPINPUT input, int size)
{
    (void)size;
    sends++;
    for (UINT i = 0; i < count && sent < 256; i++) vks[sent++] = input[i].ki.wVk;
    return count;
}
static void ProbeSleep(DWORD ms) { sleepMs += (int)ms; }
static HINSTANCE ProbeFindExecutable(LPCWSTR file, LPCWSTR dir, LPWSTR result)
{ (void)file; (void)dir; wcscpy(result, L"C:\\Windows\\notepad.exe"); return (HINSTANCE)(INT_PTR)33; }
static HANDLE ProbeSnapshot(DWORD flags, DWORD pid)
{ (void)flags; (void)pid; return (HANDLE)(INT_PTR)1; }
static BOOL ProbeFirst(HANDLE snapshot, LPPROCESSENTRY32W entry)
{ (void)snapshot; entry->th32ProcessID = 10; wcscpy(entry->szExeFile, L"notepad.exe"); return TRUE; }
static BOOL ProbeNext(HANDLE snapshot, LPPROCESSENTRY32W entry)
{ (void)snapshot; (void)entry; return FALSE; }
static BOOL ProbeClose(HANDLE handle) { (void)handle; return TRUE; }
static BOOL ProbeEnum(WNDENUMPROC callback, LPARAM context)
{ callback((HWND)(INT_PTR)7, context); return TRUE; }
static DWORD ProbeThread(HWND window, LPDWORD pid)
{ (void)window; if (pid) *pid = 10; return 10; }
static BOOL ProbeVisible(HWND window) { (void)window; return TRUE; }
static BOOL ProbeIconic(HWND window) { (void)window; return FALSE; }
static BOOL ProbeForeground(HWND window) { (void)window; activated++; return TRUE; }
static HINSTANCE ProbeShell(HWND window, LPCWSTR verb, LPCWSTR file, LPCWSTR params, LPCWSTR dir, int show)
{ (void)window; (void)verb; (void)file; (void)params; (void)dir; (void)show; opened++; return (HINSTANCE)(INT_PTR)33; }
#define SendInput ProbeSendInput
#define Sleep ProbeSleep
#define FindExecutableW ProbeFindExecutable
#define CreateToolhelp32Snapshot ProbeSnapshot
#define Process32FirstW ProbeFirst
#define Process32NextW ProbeNext
#define CloseHandle ProbeClose
#define EnumWindows ProbeEnum
#define GetWindowThreadProcessId ProbeThread
#define IsWindowVisible ProbeVisible
#define IsIconic ProbeIconic
#define SetForegroundWindow ProbeForeground
#define ShellExecuteW ProbeShell
#include "../src/actions.c"
#undef SendInput
#undef Sleep
AppConfig g_cfg;
int g_enabled = 1;
void Config_Save(void) { }
void UI_Show(HWND parent) { (void)parent; }
void Dashboard_Show(void) { }
void Dashboard_RecordUsage(int id) { (void)id; }

int main(void)
{
    /* Content after the first terminator is deliberately distinct, so this
       detects crossing the intended C-string boundary without a crash. */
    char seq[64] = "{C} ";
    memcpy(seq + 5, "{D}", 4);
    ExecKeySeq(seq);
    printf("Trailing-space probe: intended C; send count=%d; keys=", sent);
    for (int i = 0; i < sent; i++) printf("%c", vks[i]);
    printf("\n");
    int crossed = sent == 4 && vks[2] == 'D';
    sent = sends = sleepMs = 0;
    ExecKeySeq("{C}{Sleep 5000}{D}");
    printf("Sleep probe: synchronous Sleep(%d), SendInput batches=%d\n", sleepMs, sends);
    g_cfg.commandCount = 1;
    g_cfg.commands[0].id = 1;
    g_cfg.commands[0].type = CMD_RUN;
    strcpy(g_cfg.commands[0].runPath, "D:\\Documents\\new-file.txt");
    Action_ExecuteCommand(1);
    printf("Document probe with associated app already running: activated=%d; ShellExecute calls=%d\n", activated, opened);
    printf("Confirmed issues=%d\n", crossed + (sleepMs == 5000) + (activated == 1 && opened == 0));
    return crossed && sleepMs == 5000 && activated == 1 && opened == 0 ? 0 : 1;
}
