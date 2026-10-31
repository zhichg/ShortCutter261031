/* Production persistence tests confined to a private project-local fixture.
   No UI, hooks, autorun registration or script scanning is executed. */
#include "../src/main.h"
#include <stdlib.h>
#include <limits.h>
#include <taskschd.h>

AppConfig g_cfg;
int g_enabled = 1;
HINSTANCE g_hInst;
HWND g_hWnd;
HMENU g_hTrayMenu;
int g_dpiScale = 96;
wchar_t g_exeDir[SC_PATH_CAP];
static wchar_t fixture[SC_PATH_CAP];
static int positions[MAX_DOCK_ITEMS];
static int failures;
static int checks;
const wchar_t *GetExeDir(void) { return g_exeDir; }
const wchar_t *GetIniPath(void) { return fixture; }
int Dashboard_GetHistCount(void) { return 0; }
const char *Dashboard_GetHistItem(int i) { (void)i; return NULL; }
void Dashboard_ClearHist(void) { }
void Dashboard_AddHistItem(const char *s) { (void)s; }
int Dashboard_GetDockPos(int i) { return positions[i]; }
void Dashboard_SetDockPos(int i, int p) { positions[i] = p; }
static int startupComCalls;
static HRESULT startupComResult=E_ACCESSDENIED;
static HRESULT taskLookupResult=HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND),taskDeleteResult=S_OK;
static int taskLookups,taskDeletes,elevationCalls,elevationCloses,elevationValid;
static DWORD elevationError,elevationExitCode;
static HRESULT elevationWaitResult=S_OK;
static int elevationExitQueryFail;
static int registryCalls,registryExists,registryWriteFail;
static DWORD registryType,registrySize;
static BYTE registryData[8192];

/* Every registry entry point used by production code is replaced. */
static LSTATUS WINAPI Fixture_RegOpen(HKEY key,LPCWSTR path,DWORD options,REGSAM access,PHKEY out)
{
    (void)key;(void)path;(void)options;(void)access;registryCalls++;
    *out=(HKEY)(UINT_PTR)123;return ERROR_SUCCESS;
}
static LSTATUS WINAPI Fixture_RegCreate(HKEY key,LPCWSTR path,DWORD reserved,LPWSTR kind,DWORD options,REGSAM access,const LPSECURITY_ATTRIBUTES security,PHKEY out,LPDWORD disposition)
{
    (void)reserved;(void)kind;(void)security;(void)disposition;
    return Fixture_RegOpen(key,path,options,access,out);
}
static LSTATUS WINAPI Fixture_RegQuery(HKEY key,LPCWSTR name,LPDWORD reserved,LPDWORD type,LPBYTE data,LPDWORD bytes)
{
    (void)key;(void)name;(void)reserved;registryCalls++;
    if(!registryExists)return ERROR_FILE_NOT_FOUND;
    if(type)*type=registryType;
    if(data && *bytes<registrySize){*bytes=registrySize;return ERROR_MORE_DATA;}
    if(data)memcpy(data,registryData,registrySize);
    *bytes=registrySize;return ERROR_SUCCESS;
}
static LSTATUS WINAPI Fixture_RegSet(HKEY key,LPCWSTR name,DWORD reserved,DWORD type,const BYTE *data,DWORD bytes)
{
    (void)key;(void)name;(void)reserved;registryCalls++;
    if(registryWriteFail)return ERROR_ACCESS_DENIED;
    if(bytes>sizeof(registryData))return ERROR_MORE_DATA;
    memcpy(registryData,data,bytes);registrySize=bytes;registryType=type;registryExists=1;return ERROR_SUCCESS;
}
static LSTATUS WINAPI Fixture_RegDelete(HKEY key,LPCWSTR name)
{
    (void)key;(void)name;registryCalls++;
    if(registryWriteFail)return ERROR_ACCESS_DENIED;
    if(!registryExists)return ERROR_FILE_NOT_FOUND;
    registryExists=0;return ERROR_SUCCESS;
}
static LSTATUS WINAPI Fixture_RegClose(HKEY key){(void)key;registryCalls++;return ERROR_SUCCESS;}
static HRESULT WINAPI Fixture_ComInit(LPVOID reserved, DWORD mode)
{ (void)reserved; (void)mode; return S_OK; }
static void WINAPI Fixture_ComUninit(void) { }
static ULONG WINAPI Fixture_TaskRelease(IRegisteredTask *task){(void)task;return 0;}
static ULONG WINAPI Fixture_FolderRelease(ITaskFolder *folder){(void)folder;return 0;}
static ULONG WINAPI Fixture_ServiceRelease(ITaskService *service){(void)service;return 0;}
static IRegisteredTaskVtbl fixtureTaskVtbl={.Release=Fixture_TaskRelease};
static IRegisteredTask fixtureTask={.lpVtbl=&fixtureTaskVtbl};
static HRESULT WINAPI Fixture_GetTask(ITaskFolder *folder,BSTR name,IRegisteredTask **task)
{
    (void)folder;taskLookups++;
    if(wcscmp(name,L"ShortCutter"))return E_INVALIDARG;
    *task=SUCCEEDED(taskLookupResult)?&fixtureTask:NULL;return taskLookupResult;
}
static HRESULT WINAPI Fixture_DeleteTask(ITaskFolder *folder,BSTR name,LONG flags)
{
    (void)folder;taskDeletes++;
    return wcscmp(name,L"ShortCutter") || flags?E_INVALIDARG:taskDeleteResult;
}
static ITaskFolderVtbl fixtureFolderVtbl={.Release=Fixture_FolderRelease,.GetTask=Fixture_GetTask,.DeleteTask=Fixture_DeleteTask};
static ITaskFolder fixtureFolder={.lpVtbl=&fixtureFolderVtbl};
static HRESULT WINAPI Fixture_GetFolder(ITaskService *service,BSTR path,ITaskFolder **folder)
{
    (void)service;
    if(wcscmp(path,L"\\"))return E_INVALIDARG;
    *folder=&fixtureFolder;return S_OK;
}
static HRESULT WINAPI Fixture_Connect(ITaskService *service,VARIANT server,VARIANT user,VARIANT domain,VARIANT password)
{(void)service;(void)server;(void)user;(void)domain;(void)password;return S_OK;}
static ITaskServiceVtbl fixtureServiceVtbl={.Release=Fixture_ServiceRelease,.GetFolder=Fixture_GetFolder,.Connect=Fixture_Connect};
static ITaskService fixtureService={.lpVtbl=&fixtureServiceVtbl};
static HRESULT WINAPI Fixture_ComCreate(REFCLSID clsid, LPUNKNOWN outer, DWORD context, REFIID iid, LPVOID *out)
{
    (void)clsid; (void)outer; (void)context; (void)iid;
    *out = SUCCEEDED(startupComResult)?&fixtureService:NULL;
    startupComCalls++;
    return startupComResult;
}
static UINT WINAPI Fixture_SystemDirectory(LPWSTR path,UINT count)
{lstrcpynW(path,L"C:\\Windows\\System32",count);return (UINT)wcslen(path);}
static BOOL WINAPI Fixture_Elevate(SHELLEXECUTEINFOW *execute)
{
    elevationCalls++;
    elevationValid=!wcscmp(execute->lpFile,L"C:\\Windows\\System32\\schtasks.exe") &&
        !wcscmp(execute->lpVerb,L"runas") && !wcscmp(execute->lpParameters,L"/Delete /TN \"\\ShortCutter\" /F") &&
        execute->nShow==SW_HIDE && (execute->fMask&SEE_MASK_NOCLOSEPROCESS) && (execute->fMask&SEE_MASK_FLAG_NO_UI);
    if(elevationError){SetLastError(elevationError);return FALSE;}
    execute->hProcess=(HANDLE)(UINT_PTR)456;return TRUE;
}
static HRESULT WINAPI Fixture_Wait(DWORD flags,DWORD timeout,ULONG count,LPHANDLE handles,LPDWORD index)
{
    if(flags || timeout!=INFINITE || count!=1 || handles[0]!=(HANDLE)(UINT_PTR)456)return E_INVALIDARG;
    *index=0;return elevationWaitResult;
}
static BOOL WINAPI Fixture_ExitCode(HANDLE process,LPDWORD code)
{
    (void)process;
    if(elevationExitQueryFail){SetLastError(ERROR_INVALID_HANDLE);return FALSE;}
    *code=elevationExitCode;return TRUE;
}
static BOOL WINAPI Fixture_CloseHandle(HANDLE process)
{(void)process;elevationCloses++;return TRUE;}
#define Config_AutoStart Fixture_DoNotCallAutoStart
#define Scripts_ScanAndSync Fixture_DoNotCallScriptsScan
#define CoInitializeEx Fixture_ComInit
#define CoUninitialize Fixture_ComUninit
#define CoCreateInstance Fixture_ComCreate
#define RegOpenKeyExW Fixture_RegOpen
#define RegCreateKeyExW Fixture_RegCreate
#define RegQueryValueExW Fixture_RegQuery
#define RegSetValueExW Fixture_RegSet
#define RegDeleteValueW Fixture_RegDelete
#define RegCloseKey Fixture_RegClose
#define GetSystemDirectoryW Fixture_SystemDirectory
#define ShellExecuteExW Fixture_Elevate
#define CoWaitForMultipleHandles Fixture_Wait
#define GetExitCodeProcess Fixture_ExitCode
#define CloseHandle Fixture_CloseHandle
#include "../src/config.c"
#undef Config_AutoStart
#undef Scripts_ScanAndSync
#undef CoInitializeEx
#undef CoUninitialize
#undef CoCreateInstance
#undef RegOpenKeyExW
#undef RegCreateKeyExW
#undef RegQueryValueExW
#undef RegSetValueExW
#undef RegDeleteValueW
#undef RegCloseKey
#undef GetSystemDirectoryW
#undef ShellExecuteExW
#undef CoWaitForMultipleHandles
#undef GetExitCodeProcess
#undef CloseHandle

static void Check(int condition, const char *message)
{
    checks++;
    printf("%s: %s\n", condition ? "PASS" : "FAIL", message);
    if (!condition) failures++;
}

static Command *Find(int id)
{
    for (int i = 0; i < g_cfg.commandCount; i++)
        if (g_cfg.commands[i].id == id) return &g_cfg.commands[i];
    return NULL;
}

static Command *Add(const char *name)
{
    Command *c = &g_cfg.commands[g_cfg.commandCount++];
    memset(c, 0, sizeof(*c));
    c->id = Config_AllocateCommandId(&g_cfg);
    SC_CopyUtf8(c->name, sizeof(c->name), name);
    SC_CopyUtf8(c->group, sizeof(c->group), "Safety tests");
    SC_CopyUtf8(c->runPath, sizeof(c->runPath), "unused.exe");
    c->activateExisting = 1;
    return c;
}

static void Reload(void)
{
    WritePrivateProfileStringW(NULL, NULL, NULL, fixture);
    Config_Load();
}

static unsigned char *ReadFixture(size_t *length)
{
    FILE *f = _wfopen(fixture, L"rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    *length = (size_t)ftell(f);
    rewind(f);
    unsigned char *data = malloc(*length ? *length : 1);
    if (data && fread(data, 1, *length, f) != *length) { free(data); data = NULL; }
    fclose(f);
    return data;
}

static void TestIdAndBindings(void)
{
    Config_SetDefaults(); Config_CreateBuiltins();
    int builtinCount = g_cfg.commandCount;
    /* Renaming a builtin does not create another copy on the next load. */
    SC_CopyUtf8(g_cfg.commands[0].name, sizeof(g_cfg.commands[0].name), "Renamed builtin");
    Command *c = Add("Old target");
    int oldId = c->id;
    g_cfg.hotkeyCount = 1;
    g_cfg.hotkeys[0].vk = 'Z'; g_cfg.hotkeys[0].enabled = 1; g_cfg.hotkeys[0].commandId = oldId;
    g_cfg.gestureCount = 1;
    g_cfg.gestures[0].enabled = 1; g_cfg.gestures[0].commandId = oldId;
    g_cfg.gestures[0].chainCount = 1; strcpy(g_cfg.gestures[0].chains[0], "03");
    strcpy(g_cfg.gestures[0].name, "Old gesture");
    Check(Config_Save(), "initial save succeeds");
    g_cfg.commandCount--; /* Same deletion semantics as the old UI. */
    Check(Config_Save(), "save after highest ID deletion succeeds");
    Reload();
    Check(g_cfg.commandCount == builtinCount, "renamed builtin is recognized by its action, not duplicated");
    Check(!Find(oldId) && g_cfg.nextCmdId > oldId, "deleted highest ID remains below persistent allocator");
    Check(g_cfg.hotkeys[0].commandId == CMDID_NONE && !g_cfg.hotkeys[0].enabled &&
        g_cfg.gestures[0].commandId == CMDID_NONE && !g_cfg.gestures[0].enabled,
        "orphaned hotkey and gesture are unbound and disabled");
    c = Add("Replacement");
    Check(c->id != oldId, "replacement never receives deleted ID");
    Config_UnbindCommand(&g_cfg, c->id);
    g_cfg.nextCmdId = INT_MAX;
    Check(!Config_AllocateCommandId(&g_cfg), "ID exhaustion is rejected without integer overflow");
}

static void TestRoundTrip(void)
{
    Config_SetDefaults(); Config_CreateBuiltins();
    Command *c = Add("Unicode and multiline");
    int id = c->id;
    wchar_t name[81];
    for (int i = 0; i < 80; i++) name[i] = 0x4E2D;
    name[80] = 0;
    Check(SC_Utf8FromWide(c->name, sizeof(c->name), name), "80 Chinese characters fit enlarged UTF-8 name capacity");
    char expectedName[SC_NAME_CAP]; strcpy(expectedName, c->name);
    SC_CopyUtf8(c->keySeq, sizeof(c->keySeq), "{Ctrl down}\r\n{A}\n{Ctrl up}\\literal\tend");
    SC_CopyUtf8(c->arguments, sizeof(c->arguments), "\"C:\\folder with spaces\\input.txt\" /flag");
    SC_CopyUtf8(c->workingDir, sizeof(c->workingDir), "C:\\folder with spaces");
    SC_CopyUtf8(c->runPath, sizeof(c->runPath), "C:\\folder\\new\\runner.exe");
    c->activateExisting = 0; c->confirmBeforeRun = c->runAsAdmin = 1;
    g_cfg.dashboardCompact = 1;
    g_cfg.settingsWidth=650;g_cfg.settingsHeight=500;
    g_cfg.settingsColumnWidths[0][0]=321;
    g_cfg.settingsColumnWidths[0][4]=1;
    g_cfg.settingsColumnWidths[1][5]=501;
    g_cfg.settingsColumnWidths[2][3]=281;
    Check(Config_Save(), "unicode, multiline and launch metadata serialize atomically");
    Reload(); c = Find(id);
    Check(c && !strcmp(c->name, expectedName), "Chinese name round-trips without truncation");
    Check(c && !strcmp(c->keySeq, "{Ctrl down}\r\n{A}\n{Ctrl up}\\literal\tend"), "CRLF, LF, tab and literal backslash round-trip exactly");
    Check(c && !strcmp(c->arguments, "\"C:\\folder with spaces\\input.txt\" /flag") &&
        !strcmp(c->workingDir, "C:\\folder with spaces") && !strcmp(c->runPath, "C:\\folder\\new\\runner.exe"),
        "quoted arguments, working directory and Windows paths round-trip exactly");
    Check(c && !c->activateExisting && c->confirmBeforeRun && c->runAsAdmin && g_cfg.dashboardCompact,
        "launch policies and compact Dashboard setting persist");
    Check(g_cfg.settingsWidth==650 && g_cfg.settingsHeight==500,"settings window dimensions survive configuration round-trip");
    Check(g_cfg.settingsColumnWidths[0][0]==321 && g_cfg.settingsColumnWidths[1][5]==501 &&
        g_cfg.settingsColumnWidths[2][3]==281,"command, gesture and hotkey column widths round-trip independently");
    Check(g_cfg.settingsColumnWidths[0][4]==1 && g_cfg.settingsColumnWidths[0][1]==0,
        "hidden zero-width columns remain distinct from unset default columns");
    wchar_t backup[SC_PATH_CAP]; swprintf(backup, SC_PATH_CAP, L"%ls.bak", fixture);
    Check(GetFileAttributesW(backup) != INVALID_FILE_ATTRIBUTES, "successful replacement retains previous file as .bak");
    char shortBuffer[64]; memset(shortBuffer, 'X', sizeof(shortBuffer));
    Check(!SC_Utf8FromWide(shortBuffer, sizeof(shortBuffer), name) && shortBuffer[0] == 0,
        "checked conversion refuses small capacity and clears partial output");
    wchar_t wideShort[3];
    Check(!SC_WideFromUtf8(wideShort, 3, expectedName) && !wideShort[0], "reverse conversion refuses small capacity");
}

static void TestSaveFailure(void)
{
    size_t beforeLength = 0, afterLength = 0;
    unsigned char *before = ReadFixture(&beforeLength);
    HANDLE lock = CreateFileW(fixture, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    Check(lock != INVALID_HANDLE_VALUE, "fixture opened without delete sharing for replacement failure test");
    g_cfg.matchThreshold = 93;
    Check(!Config_Save() && Config_GetLastError() != 0 && Config_GetLastErrorText()[0],
        "atomic replacement failure reports an explicit Windows error");
    if (lock != INVALID_HANDLE_VALUE) CloseHandle(lock);
    unsigned char *after = ReadFixture(&afterLength);
    Check(before && after && beforeLength == afterLength && !memcmp(before, after, beforeLength),
        "failed replacement leaves original INI byte-for-byte intact");
    free(after);
    Command *c = &g_cfg.commands[0];
    c->name[0] = (char)0xFF; c->name[1] = 0;
    Check(!Config_Save() && Config_GetLastError() == ERROR_NO_UNICODE_TRANSLATION,
        "invalid UTF-8 prevents persistence instead of saving corrupt text");
    after = ReadFixture(&afterLength);
    Check(before && after && beforeLength == afterLength && !memcmp(before, after, beforeLength),
        "serialization failure also preserves previous INI");
    free(before); free(after);
}

static void TestLegacy(void)
{
    FILE *file = _wfopen(fixture, L"wb");
    wchar_t bom = 0xFEFF; fwrite(&bom, sizeof(bom), 1, file);
    WrLine(file, L"[Commands]\r\nCount=1\r\nId0=500\r\nName0=Legacy\r\nType0=0\r\nPath0=C:\\new\\runner.exe\r\nKeys0={A}\r\nGroup0=Old\r\n");
    WrLine(file, L"[Hotkeys]\r\nCount=1\r\nVk0=65\r\nCmdId0=999\r\nEn0=1\r\n");
    fclose(file);
    Reload(); Command *c = Find(500);
    Check(g_cfg.settingsWidth==SC_SETTINGS_MIN_WIDTH && g_cfg.settingsHeight==SC_SETTINGS_MIN_HEIGHT,
        "legacy INI without window dimensions keeps the default settings size");
    Check(!g_cfg.settingsColumnWidths[0][0] && !g_cfg.settingsColumnWidths[1][5] && !g_cfg.settingsColumnWidths[2][3],
        "legacy INI without column widths retains each list's default layout");
    Check(c && !strcmp(c->runPath, "C:\\new\\runner.exe") && c->activateExisting,
        "old INI path backslashes stay literal and activate-existing defaults on");
    Check(g_cfg.nextCmdId > 999 && !g_cfg.hotkeys[0].enabled && !g_cfg.hotkeys[0].commandId,
        "old orphaned reference reserves its ID before cleanup");
}

static void TestStartupMocks(void)
{
    g_cfg.settingsColumnWidths[0][0]=INT_MAX;g_cfg.settingsColumnWidths[1][0]=-1;
    g_cfg.settingsColumnWidths[2][0]=1;Config_Validate(&g_cfg);
    Check(!g_cfg.settingsColumnWidths[0][0] && !g_cfg.settingsColumnWidths[1][0] && g_cfg.settingsColumnWidths[2][0]==1,
        "invalid column widths reset to defaults while intentional hidden columns remain valid");
    g_cfg.settingsWidth=INT_MAX;g_cfg.settingsHeight=-1;Config_Validate(&g_cfg);
    Check(g_cfg.settingsWidth==SC_SETTINGS_MIN_WIDTH && g_cfg.settingsHeight==SC_SETTINGS_MIN_HEIGHT,
        "corrupt window dimensions recover safely to defaults");
    /* Config_Load set applied=0. Both registry and scheduler are private mocks. */
    int original = g_cfg.autoStart;
    Check(Fixture_DoNotCallAutoStart(original) && startupComCalls == 0 && registryCalls==0,
        "unchanged startup preference is idempotent without any COM/system operation");
    Check(!Fixture_DoNotCallAutoStart(!original) && startupComCalls == 1 &&
        Config_GetLastError() == ERROR_ACCESS_DENIED && Config_GetLastErrorText()[0],
        "startup migration denial returns readable failure through mocked COM boundary");
    Check(g_cfg.autoStart == original && g_autoStartApplied == original,
        "failed startup change preserves both configured and applied state");
    Check(!registryExists,"migration denial reverses a newly-created startup value");
    const wchar_t oldValue[]=L"%USERPROFILE%\\legacy.exe";
    memcpy(registryData,oldValue,sizeof(oldValue));registrySize=sizeof(oldValue);registryType=REG_EXPAND_SZ;registryExists=1;
    Check(!Fixture_DoNotCallAutoStart(1) && registryExists && registryType==REG_EXPAND_SZ &&
        registrySize==sizeof(oldValue) && !memcmp(registryData,oldValue,sizeof(oldValue)),
        "migration denial restores the exact existing value and registry type");
    int comBefore=startupComCalls;registryWriteFail=1;
    Check(!Fixture_DoNotCallAutoStart(1) && startupComCalls==comBefore &&
        !memcmp(registryData,oldValue,sizeof(oldValue)),
        "failed registry write never removes the previous scheduler entry");registryWriteFail=0;
    startupComResult=S_OK;
    Check(Fixture_DoNotCallAutoStart(1) && g_cfg.autoStart==1 && g_autoStartApplied==1 &&
        registryExists && registryType==REG_SZ && ((wchar_t*)registryData)[0]==L'"',
        "successful mocked migration records an explicitly quoted executable path");
    Check(taskLookups==1 && !taskDeletes && !elevationCalls,
        "an absent legacy task requires neither delete access nor elevation");
    int callsBefore=registryCalls;comBefore=startupComCalls;
    Check(Fixture_DoNotCallAutoStart(1) && registryCalls==callsBefore && startupComCalls==comBefore,
        "reapplying enabled startup performs no system calls");
    BYTE enabledValue[8192];memcpy(enabledValue,registryData,registrySize);DWORD enabledSize=registrySize;
    taskLookupResult=S_OK;taskDeleteResult=E_ACCESSDENIED;elevationError=ERROR_CANCELLED;
    Check(!Fixture_DoNotCallAutoStart(0) && g_cfg.autoStart==1 && registryExists &&
        registrySize==enabledSize && !memcmp(registryData,enabledValue,enabledSize) &&
        Config_GetLastError()==ERROR_CANCELLED && elevationCalls==1 && elevationValid && !elevationCloses,
        "cancelled legacy cleanup restores the enabled startup value and requests only the fixed Windows cleanup tool");
    elevationError=0;elevationExitCode=1;
    Check(!Fixture_DoNotCallAutoStart(0) && g_cfg.autoStart==1 && registryExists &&
        Config_GetLastError()==ERROR_ACCESS_DENIED && elevationCloses==1,
        "a failed elevated cleanup cannot report successful disable and closes its process handle");
    elevationExitCode=0;elevationWaitResult=E_FAIL;
    Check(!Fixture_DoNotCallAutoStart(0) && registryExists && Config_GetLastError()==(DWORD)E_FAIL && elevationCloses==2,
        "a failed cleanup wait preserves startup state and closes its process handle");
    elevationWaitResult=S_OK;elevationExitQueryFail=1;
    Check(!Fixture_DoNotCallAutoStart(0) && registryExists && Config_GetLastError()==ERROR_INVALID_HANDLE && elevationCloses==3,
        "an unreadable cleanup exit status preserves startup state");elevationExitQueryFail=0;
    Check(Fixture_DoNotCallAutoStart(0) && !g_cfg.autoStart && !g_autoStartApplied && !registryExists,
        "approved cleanup of an administrator legacy task allows disable to complete");
    Check(elevationValid && elevationCloses==4,"successful elevated cleanup also closes its process handle");
    taskLookupResult=HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
    int elevationsBefore=elevationCalls;
    Check(Fixture_DoNotCallAutoStart(1) && Fixture_DoNotCallAutoStart(0) && elevationCalls==elevationsBefore,
        "future startup toggles need no elevation after the legacy task is gone");
    taskLookupResult=S_OK;taskDeleteResult=S_OK;
    Check(Fixture_DoNotCallAutoStart(1) && elevationCalls==elevationsBefore,
        "legacy tasks removable with current permissions do not request elevation");
    taskDeleteResult=HRESULT_FROM_WIN32(ERROR_SERVICE_NOT_ACTIVE);
    Check(!Fixture_DoNotCallAutoStart(0) && registryExists && g_cfg.autoStart==1 &&
        Config_GetLastError()==ERROR_SERVICE_NOT_ACTIVE && elevationCalls==elevationsBefore,
        "non-permission scheduler failures preserve startup state without requesting elevation");
    taskDeleteResult=E_ACCESSDENIED;elevationError=ERROR_CANCELLED;
    g_cfg.autoStart=0;g_autoStartApplied=0;registryType=REG_EXPAND_SZ;
    memcpy(registryData,oldValue,sizeof(oldValue));registrySize=sizeof(oldValue);
    Check(!Fixture_DoNotCallAutoStart(1) && !g_cfg.autoStart && !g_autoStartApplied && registryExists &&
        registryType==REG_EXPAND_SZ && registrySize==sizeof(oldValue) && !memcmp(registryData,oldValue,sizeof(oldValue)),
        "cancelled elevation on enable restores the exact prior registry value and type");
    registryExists=0;
    Check(!Fixture_DoNotCallAutoStart(1) && !registryExists && !g_autoStartApplied,
        "cancelled elevation on enable removes a newly created Run value");
}

int main(void)
{
    GetCurrentDirectoryW(SC_PATH_CAP, g_exeDir);
    swprintf(fixture, SC_PATH_CAP, L"%ls\\tests\\config-safety-%lu.ini", g_exeDir, GetCurrentProcessId());
    TestIdAndBindings(); TestRoundTrip(); TestSaveFailure(); TestLegacy(); TestStartupMocks();
    DeleteFileW(fixture);
    wchar_t backup[SC_PATH_CAP]; swprintf(backup, SC_PATH_CAP, L"%ls.bak", fixture); DeleteFileW(backup);
    printf("RESULT checks=%d failures=%d\n", checks, failures);
    return failures ? 1 : 0;
}
