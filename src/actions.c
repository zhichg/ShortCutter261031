#include "main.h"

/* ── 弹框常量 ─────────────────────────────── */
#define POPUP_W      200
#define POPUP_H      30
#define MAX_HIST     5
#define MAX_SCRIPTS  50

static HWND g_hPopup     = NULL;

/* 由鼠标钩子调用：点击在弹窗外则关闭 */
int Popup_CloseIfOutside(int x, int y)
{
    if (!g_hPopup) return 0;
    RECT r; GetWindowRect(g_hPopup, &r);
    if (x < r.left || x > r.right || y < r.top || y > r.bottom) {
        DestroyWindow(g_hPopup);
        return 1;
    }
    return 0;
}
static HWND g_hPopupEdit = NULL;
static HWND g_hList      = NULL;
static WNDPROC g_oldEditProc;
static HFONT  g_hPopupFont = NULL;
static HBRUSH g_hPopupBg   = NULL;
static int    g_listVisible = 0;

/* ── 脚本文件夹 ─────────────────────────────── */
static wchar_t g_scriptNames[MAX_SCRIPTS][64];
static wchar_t g_scriptPaths[MAX_SCRIPTS][MAX_PATH];
static int    g_scriptCount = 0;

/* ── 历史记录 ─────────────────────────────── */
static char  g_history[MAX_HIST][128];
static int   g_histCount = 0;

static void History_Add(const char *cmd)
{
    int i, j;
    for (i = 0; i < g_histCount; i++) {
        if (lstrcmpiA(g_history[i], cmd) == 0) {
            for (j = i; j < g_histCount - 1; j++)
                strcpy(g_history[j], g_history[j+1]);
            g_histCount--;
            break;
        }
    }
    if (g_histCount >= MAX_HIST) {
        for (i = 0; i < MAX_HIST - 1; i++)
            strcpy(g_history[i], g_history[i+1]);
        g_histCount = MAX_HIST - 1;
    }
    strncpy(g_history[g_histCount], cmd, 127);
    g_histCount++;
}

/* ── 脚本文件夹扫描 ──────────────────────────── */
static void Scripts_Scan(void)
{
    g_scriptCount = 0;
    wchar_t dir[MAX_PATH];
    lstrcpyW(dir, GetExeDir());
    lstrcatW(dir, L"\\scripts\\");
    CreateDirectoryW(dir, NULL);

    const wchar_t *exts[] = { L"*.bat", L"*.cmd", L"*.ps1", L"*.vbs" };
    for (int e = 0; e < 4 && g_scriptCount < MAX_SCRIPTS; e++) {
        wchar_t pattern[MAX_PATH];
        lstrcpyW(pattern, dir);
        lstrcatW(pattern, exts[e]);
        WIN32_FIND_DATAW fd;
        HANDLE hFind = FindFirstFileW(pattern, &fd);
        if (hFind == INVALID_HANDLE_VALUE) continue;
        do {
            if (g_scriptCount >= MAX_SCRIPTS) break;
            wchar_t nameOnly[64];
            lstrcpynW(nameOnly, fd.cFileName, 64);
            wchar_t *dot = wcsrchr(nameOnly, L'.');
            if (dot) *dot = 0;
            int dup = 0;
            for (int k = 0; k < g_scriptCount; k++) {
                if (lstrcmpiW(g_scriptNames[k], nameOnly) == 0) { dup = 1; break; }
            }
            if (dup) continue;
            lstrcpynW(g_scriptNames[g_scriptCount], nameOnly, 64);
            lstrcpyW(g_scriptPaths[g_scriptCount], dir);
            lstrcatW(g_scriptPaths[g_scriptCount], fd.cFileName);
            g_scriptCount++;
        } while (FindNextFileW(hFind, &fd));
        FindClose(hFind);
    }
}

/* ── 建议列表 ─────────────────────────────── */
static int FuzzyScore(const char *filter, const char *target)
{
    const char *f = filter, *t = target;
    int lastPos = -1;
    while (*f && *t) {
        char fc = *f, tc = *t;
        if (fc >= 'A' && fc <= 'Z') fc += 32;
        if (tc >= 'A' && tc <= 'Z') tc += 32;
        if (fc == tc) { lastPos = (int)(t - target); f++; }
        t++;
    }
    return *f ? -1 : lastPos;
}

#define MAX_MATCHES  254
typedef struct { wchar_t name[128]; int score; } MatchEntry;
static int MatchCmp(const void *a, const void *b)
{
    const MatchEntry *ma = (const MatchEntry *)a;
    const MatchEntry *mb = (const MatchEntry *)b;
    if (ma->score != mb->score) return ma->score - mb->score;
    return lstrcmpiW(ma->name, mb->name);
}

static void List_AddStr(const wchar_t *w)
{
    LRESULT f = SendMessageW(g_hList, LB_FINDSTRINGEXACT, (WPARAM)-1, (LPARAM)w);
    if (f == LB_ERR) SendMessageW(g_hList, LB_ADDSTRING, 0, (LPARAM)w);
}

static void List_Update(const char *filter)
{
    SendMessageW(g_hList, LB_RESETCONTENT, 0, 0);
    int flen = (int)strlen(filter);
    if (flen == 0) {
        for (int i = g_histCount - 1; i >= 0; i--) {
            wchar_t w[128];
            MultiByteToWideChar(CP_UTF8, 0, g_history[i], -1, w, 128);
            List_AddStr(w);
        }
    } else {
        MatchEntry matches[MAX_MATCHES];
        int mcnt = 0;
        const char *builtins[] = {"sc","cmd","powershell","ps",NULL};
        for (int i = 0; builtins[i] && mcnt < MAX_MATCHES; i++) {
            int s = FuzzyScore(filter, builtins[i]);
            if (s >= 0) {
                MultiByteToWideChar(CP_UTF8, 0, builtins[i], -1, matches[mcnt].name, 128);
                matches[mcnt].score = s; mcnt++;
            }
        }
        for (int i = 0; i < g_scriptCount && mcnt < MAX_MATCHES; i++) {
            char sname[64];
            WideCharToMultiByte(CP_UTF8, 0, g_scriptNames[i], -1, sname, 64, NULL, NULL);
            int s = FuzzyScore(filter, sname);
            if (s >= 0) {
                lstrcpyW(matches[mcnt].name, g_scriptNames[i]);
                matches[mcnt].score = s; mcnt++;
            }
        }
        /* Commands（跳过含"+"的快捷键组合，如 Alt+Tab） */
        for (int i = 0; i < g_cfg.commandCount && mcnt < MAX_MATCHES; i++) {
            if (strchr(g_cfg.commands[i].name, '+')) continue;
            int s = FuzzyScore(filter, g_cfg.commands[i].name);
            if (s >= 0) {
                MultiByteToWideChar(CP_UTF8, 0, g_cfg.commands[i].name, -1, matches[mcnt].name, 128);
                matches[mcnt].score = s; mcnt++;
            }
        }
        qsort(matches, mcnt, sizeof(MatchEntry), MatchCmp);
        for (int i = 0; i < mcnt; i++) List_AddStr(matches[i].name);
    }

    int added = (int)SendMessageW(g_hList, LB_GETCOUNT, 0, 0);
    int itemH = (int)SendMessageW(g_hList, LB_GETITEMHEIGHT, 0, 0);
    int needH = added * itemH + 4;
    int maxH = 6 * itemH + 4;
    if (needH > maxH) needH = maxH;
    int newExpH = POPUP_H + needH;
    if (added > 0) {
        if (!g_listVisible) { g_listVisible = 1; ShowWindow(g_hList, SW_SHOW); }
        SetWindowPos(g_hList, NULL, 0, 0, POPUP_W - 2, needH, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        SetWindowPos(g_hPopup, NULL, 0, 0, POPUP_W, newExpH, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        RECT r; GetWindowRect(g_hPopup, &r);
        int scrH = GetSystemMetrics(SM_CYSCREEN);
        if (r.bottom > scrH)
            SetWindowPos(g_hPopup, NULL, r.left, scrH - newExpH - 10, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        SendMessageW(g_hList, LB_SETCURSEL, 0, 0);
    } else if (g_listVisible) {
        g_listVisible = 0; ShowWindow(g_hList, SW_HIDE);
        SetWindowPos(g_hPopup, NULL, 0, 0, POPUP_W, POPUP_H, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
}

/* ── 进程去重 ──────────────────────────────── */
typedef struct { DWORD pid; HWND found; } PidWinCtx;
static BOOL CALLBACK FindWinByPid(HWND hWnd, LPARAM lp)
{
    PidWinCtx *ctx = (PidWinCtx*)lp;
    DWORD pid = 0;
    GetWindowThreadProcessId(hWnd, &pid);
    if (pid == ctx->pid && IsWindowVisible(hWnd)) { ctx->found = hWnd; return FALSE; }
    return TRUE;
}

static int BringToFrontIfRunning(const char *path)
{
    if (!path || !path[0]) return 0;
    wchar_t wPath[512], wExe[64] = {0};
    MultiByteToWideChar(CP_UTF8, 0, path, -1, wPath, 512);
    const char *dot = strrchr(path, '.');
    int isExe = (dot && lstrcmpiA(dot, ".exe") == 0);
    if (isExe) {
        const char *slash = strrchr(path, '\\');
        const char *exe = slash ? slash + 1 : path;
        MultiByteToWideChar(CP_UTF8, 0, exe, -1, wExe, 64);
    } else {
        wchar_t wAssoc[512] = {0};
        HINSTANCE ret = FindExecutableW(wPath, NULL, wAssoc);
        if ((INT_PTR)ret <= 32 || !wAssoc[0]) return 0;
        wchar_t *s = wcsrchr(wAssoc, L'\\');
        if (s) lstrcpyW(wExe, s + 1); else lstrcpyW(wExe, wAssoc);
    }
    if (!wExe[0]) return 0;
    DWORD pid = 0;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W pe = {0}; pe.dwSize = sizeof(pe);
        if (Process32FirstW(snap, &pe)) {
            do { if (lstrcmpiW(pe.szExeFile, wExe) == 0) { pid = pe.th32ProcessID; break; }
            } while (Process32NextW(snap, &pe));
        }
        CloseHandle(snap);
    }
    if (pid == 0) return 0;
    PidWinCtx ctx = { pid, NULL };
    EnumWindows(FindWinByPid, (LPARAM)&ctx);
    if (ctx.found) {
        if (IsIconic(ctx.found)) ShowWindow(ctx.found, SW_RESTORE);
        SetForegroundWindow(ctx.found);
        return 1;
    }
    return 0;
}

/* ── 按键序列执行 ──────────────────────────── */
static void ExecKeySeq(const char *seq)
{
    /* 按键名 → VK + 是否扩展键 */
    struct { const char *n; BYTE v; int ext; } map[] = {
        {"Alt",VK_MENU,0},{"Ctrl",VK_CONTROL,0},{"Shift",VK_SHIFT,0},
        {"Win",VK_LWIN,1},
        {"Tab",VK_TAB,0},{"Enter",VK_RETURN,0},{"Space",VK_SPACE,0},
        {"Esc",VK_ESCAPE,0},{"Backspace",VK_BACK,0},
        {"Delete",VK_DELETE,1},{"Insert",VK_INSERT,1},
        {"Home",VK_HOME,1},{"End",VK_END,1},{"PgUp",VK_PRIOR,1},{"PgDn",VK_NEXT,1},
        {"Up",VK_UP,1},{"Down",VK_DOWN,1},{"Left",VK_LEFT,1},{"Right",VK_RIGHT,1},
        {"F1",VK_F1,0},{"F2",VK_F2,0},{"F3",VK_F3,0},{"F4",VK_F4,0},
        {"F5",VK_F5,0},{"F6",VK_F6,0},{"F7",VK_F7,0},{"F8",VK_F8,0},
        {"F9",VK_F9,0},{"F10",VK_F10,0},{"F11",VK_F11,0},{"F12",VK_F12,0},
        {"Apps",VK_APPS,1},{"PrtSc",VK_SNAPSHOT,1},
        {"D",'D',0},{"A",'A',0},{"B",'B',0},{"C",'C',0},{"E",'E',0},{"F",'F',0},
        {"G",'G',0},{"H",'H',0},{"I",'I',0},{"J",'J',0},{"K",'K',0},{"L",'L',0},
        {"M",'M',0},{"N",'N',0},{"O",'O',0},{"P",'P',0},{"Q",'Q',0},{"R",'R',0},
        {"S",'S',0},{"T",'T',0},{"U",'U',0},{"V",'V',0},{"W",'W',0},{"X",'X',0},
        {"Y",'Y',0},{"Z",'Z',0},
        {"0",'0',0},{"1",'1',0},{"2",'2',0},{"3",'3',0},{"4",'4',0},
        {"5",'5',0},{"6",'6',0},{"7",'7',0},{"8",'8',0},{"9",'9',0},
    };
    int mcnt = (int)(sizeof(map)/sizeof(map[0]));

    INPUT batch[128]; int bi = 0;
    const char *p = seq;
    while (*p) {
        while (*p == ' ' || *p == '\r' || *p == '\n') p++;
        if (*p != '{') { p++; continue; }
        p++;
        char token[64]; int ti = 0;
        while (*p && *p != '}' && ti < 62) token[ti++] = *p++;
        if (*p == '}') p++;
        token[ti] = 0;

        char keyName[32] = "", action[8] = "";
        int ki = 0, ai = 0;
        const char *tp = token;
        while (*tp == ' ') tp++;
        while (*tp && *tp != ' ' && ki < 30) keyName[ki++] = *tp++;
        keyName[ki] = 0;
        while (*tp == ' ') tp++;
        while (*tp && ai < 7) action[ai++] = *tp++;
        action[ai] = 0;

        /* Sleep: 先发送已累积的批量，再延迟 */
        if (lstrcmpiA(keyName, "Sleep") == 0) {
            if (bi > 0) { SendInput(bi, batch, sizeof(INPUT)); bi = 0; }
            int ms = atoi(action);
            if (ms > 0 && ms <= 5000) Sleep(ms);
            continue;
        }

        BYTE vk = 0; int ext = 0;
        for (int i = 0; i < mcnt; i++)
            if (lstrcmpiA(keyName, map[i].n) == 0) { vk = map[i].v; ext = map[i].ext; break; }
        if (!vk) continue;

        if (bi + 2 > 128) { SendInput(bi, batch, sizeof(INPUT)); bi = 0; }

        DWORD fl = ext ? KEYEVENTF_EXTENDEDKEY : 0;
        if (lstrcmpiA(action, "down") == 0) {
            batch[bi].type = INPUT_KEYBOARD;
            batch[bi].ki.wVk = vk;
            batch[bi].ki.dwFlags = fl;
            bi++;
        } else if (lstrcmpiA(action, "up") == 0) {
            batch[bi].type = INPUT_KEYBOARD;
            batch[bi].ki.wVk = vk;
            batch[bi].ki.dwFlags = fl | KEYEVENTF_KEYUP;
            bi++;
        } else {
            batch[bi].type = INPUT_KEYBOARD;
            batch[bi].ki.wVk = vk;
            batch[bi].ki.dwFlags = fl; bi++;
            batch[bi].type = INPUT_KEYBOARD;
            batch[bi].ki.wVk = vk;
            batch[bi].ki.dwFlags = fl | KEYEVENTF_KEYUP; bi++;
        }
    }
    if (bi > 0) SendInput(bi, batch, sizeof(INPUT));
}

/* 前向声明 */
void Popup_Show(const char *prefix);

/* ── 根据 Command ID 执行 ──────────────────── */
void Action_ExecuteCommand(int cmdId)
{
    if (cmdId == CMDID_NONE) return;
    if (cmdId == CMDID_INPUT) {
        Scripts_Scan(); List_Update("");
        Popup_Show(NULL);
        return;
    }
    if (cmdId == CMDID_SETTINGS) { UI_Show(NULL); return; }
    if (cmdId == CMDID_TOGGLE) {
        g_enabled = !g_enabled;
        g_cfg.enabled = g_enabled;
        Config_Save();
        return;
    }
    if (cmdId == CMDID_CMDPROMPT) {
        ShellExecuteW(NULL, L"runas", L"cmd.exe", NULL, NULL, SW_SHOW); return;
    }
    if (cmdId == CMDID_POWERSHELL) {
        ShellExecuteW(NULL, L"runas", L"powershell.exe", NULL, NULL, SW_SHOW); return;
    }
    if (cmdId == CMDID_DASHBOARD) {
        Dashboard_Show(); return;
    }

    /* 查找用户 Command */
    for (int i = 0; i < g_cfg.commandCount; i++) {
        if (g_cfg.commands[i].id == cmdId) {
            Command *c = &g_cfg.commands[i];
            Dashboard_RecordUsage(cmdId);   /* 记录使用 */
            if (c->type == CMD_RUN) {
                /* 特殊：sc:input → 弹框 */
                if (lstrcmpiA(c->runPath, "sc:input") == 0) {
                    Scripts_Scan();
                    Popup_Show(NULL);
                    return;
                }
                if (lstrcmpiA(c->runPath, "sc:dashboard") == 0) {
                    Dashboard_Show();
                    return;
                }
                /* explorer 始终开新窗口（进程常驻，查重会匹配到桌面） */
                if (lstrcmpiA(c->runPath, "explorer") == 0) {
                    ShellExecuteW(NULL, L"open", L"explorer.exe", NULL, NULL, SW_SHOW);
                } else if (!BringToFrontIfRunning(c->runPath)) {
                    wchar_t w[512];
                    MultiByteToWideChar(CP_UTF8, 0, c->runPath, -1, w, 512);
                    ShellExecuteW(NULL, L"open", w, NULL, NULL, SW_SHOW);
                }
            } else if (c->type == CMD_KEYS) {
                ExecKeySeq(c->keySeq);
            } else if (c->type == CMD_SCRIPT) {
                wchar_t wPath[512];
                MultiByteToWideChar(CP_UTF8, 0, c->runPath, -1, wPath, 512);
                wchar_t *dot = wcsrchr(wPath, L'.');
                /* 检查文件名是否以 _Show 结尾（如 Ping_Show.ps1）→ 显示窗口 */
                int showWin = 0;
                if (dot) {
                    wchar_t *base = dot;
                    while (base > wPath && *(base-1) != L'\\') base--;
                    if (base + 5 <= dot && wcsstr(base, L"_Show.") == dot - 5)
                        showWin = 1;
                }
                if (dot && lstrcmpiW(dot, L".ps1") == 0) {
                    wchar_t params[MAX_PATH+64];
                    if (showWin)
                        lstrcpyW(params, L"-ExecutionPolicy Bypass -File \"");
                    else
                        lstrcpyW(params, L"-WindowStyle Hidden -ExecutionPolicy Bypass -File \"");
                    lstrcatW(params, wPath); lstrcatW(params, L"\"");
                    ShellExecuteW(NULL, L"open", L"powershell.exe", params, NULL,
                                  showWin ? SW_SHOW : SW_HIDE);
                } else {
                    ShellExecuteW(NULL, L"open", wPath, NULL, NULL,
                                  showWin ? SW_SHOW : SW_HIDE);
                }
            }
            return;
        }
    }
}

void Action_ExecuteCustom(const GestureDef *g)
{
    if (!g || !g->enabled) return;
    Action_ExecuteCommand(g->commandId);
}

/* ── 由键盘钩子调用：弹框可见且失焦时转发按键（解决 UIPI 隔离）── */
int Popup_IsVisible(void)
{
    return g_hPopup != NULL;
}

int Popup_HasFocus(void)
{
    return g_hPopup && g_hPopupEdit && GetFocus() == g_hPopupEdit;
}

void Popup_ForwardKey(DWORD vkCode, DWORD scanCode, DWORD flags)
{
    if (!g_hPopup || !g_hPopupEdit) return;

    /* 构造 lParam（WM_KEYDOWN/WM_CHAR 共用） */
    LPARAM lParam = (1) | (scanCode << 16);
    if (flags & LLKHF_EXTENDED) lParam |= (1 << 24);
    if (flags & LLKHF_ALTDOWN)  lParam |= (1 << 29);

    /* 特殊键：直接 SendMessage WM_KEYDOWN */
    if (vkCode == VK_RETURN || vkCode == VK_ESCAPE || vkCode == VK_TAB  ||
        vkCode == VK_UP     || vkCode == VK_DOWN   || vkCode == VK_BACK ||
        vkCode == VK_DELETE || vkCode == VK_SPACE  || vkCode == VK_LEFT ||
        vkCode == VK_RIGHT  || vkCode == VK_HOME   || vkCode == VK_END) {
        SendMessageW(g_hPopupEdit, WM_KEYDOWN, (WPARAM)vkCode, lParam);
        return;
    }

    /* 字符键：通过 ToUnicodeEx 生成正确的 WM_CHAR，绕过
       PostMessage 方式下 TranslateMessage 键盘状态不同步的问题 */
    {
        BYTE ks[256];
        GetKeyboardState(ks);
        ks[vkCode] = 0x80;  /* 标记此键为按下，使 ToUnicodeEx 正确工作 */
        WCHAR ch;
        if (ToUnicodeEx((UINT)vkCode, (UINT)scanCode, ks, &ch, 1, 0,
                        GetKeyboardLayout(0)) == 1) {
            SendMessageW(g_hPopupEdit, WM_CHAR, ch, lParam);
        }
    }
}

/* ── 弹框 UI ══════ */

static int Popup_Execute(const char *text)
{
    if (!text || !text[0]) return 0;
    while (*text == ' ') text++;
    if (!text[0]) return 0;

    if (lstrcmpiA(text, "sc") == 0) { UI_Show(NULL); return 1; }
    if (lstrcmpiA(text, "cmd") == 0) {
        ShellExecuteW(NULL, L"runas", L"cmd.exe", NULL, NULL, SW_SHOW); return 1;
    }
    if (lstrcmpiA(text, "powershell") == 0 || lstrcmpiA(text, "ps") == 0) {
        ShellExecuteW(NULL, L"runas", L"powershell.exe", NULL, NULL, SW_SHOW); return 1;
    }
    if (lstrcmpiA(text, "dashboard") == 0 || lstrcmpiA(text, "db") == 0) {
        Dashboard_Show(); return 1;
    }

    for (int i = 0; i < g_scriptCount; i++) {
        char sname[64];
        WideCharToMultiByte(CP_UTF8, 0, g_scriptNames[i], -1, sname, 64, NULL, NULL);
        if (lstrcmpiA(text, sname) == 0) {
            wchar_t *dot = wcsrchr(g_scriptPaths[i], L'.');
            /* _Show 后缀 → 显示窗口 */
            int slen = (int)wcslen(g_scriptNames[i]);
            int showWin = (slen >= 5 && _wcsicmp(g_scriptNames[i] + slen - 5, L"_Show") == 0);
            SHELLEXECUTEINFOW sei = {0};
            sei.cbSize = sizeof(sei); sei.nShow = showWin ? SW_SHOW : SW_HIDE;
            if (dot && lstrcmpiW(dot, L".ps1") == 0) {
                sei.lpFile = L"powershell.exe";
                wchar_t params[MAX_PATH + 64];
                if (showWin)
                    lstrcpyW(params, L"-ExecutionPolicy Bypass -File \"");
                else
                    lstrcpyW(params, L"-WindowStyle Hidden -ExecutionPolicy Bypass -File \"");
                lstrcatW(params, g_scriptPaths[i]);
                lstrcatW(params, L"\"");
                sei.lpParameters = params;
            } else {
                sei.lpFile = g_scriptPaths[i]; sei.lpVerb = L"open";
            }
            ShellExecuteExW(&sei);
            return 1;
        }
    }

    /* v5 Commands */
    for (int i = 0; i < g_cfg.commandCount; i++) {
        if (lstrcmpiA(text, g_cfg.commands[i].name) == 0) {
            Action_ExecuteCommand(g_cfg.commands[i].id);
            return 1;
        }
    }

    wchar_t wMsg[600], wText[512];
    MultiByteToWideChar(CP_UTF8, 0, text, -1, wText, 512);
    lstrcpyW(wMsg, L"Unknown command:\n");
    lstrcatW(wMsg, wText);
    MessageBoxW(NULL, wMsg, L"ShortCutter", MB_OK | MB_ICONWARNING);
    return 0;
}

static void Popup_Confirm(HWND hEdit)
{
    LRESULT sel = SendMessageW(g_hList, LB_GETCURSEL, 0, 0);
    if (g_listVisible && sel != LB_ERR) {
        int len = (int)SendMessageW(g_hList, LB_GETTEXTLEN, sel, 0);
        if (len > 0 && len < 128) {
            wchar_t w[128];
            SendMessageW(g_hList, LB_GETTEXT, sel, (LPARAM)w);
            SetWindowTextW(hEdit, w);
        }
    }
    wchar_t wFull[512]; char full[512];
    GetWindowTextW(hEdit, wFull, 512);
    WideCharToMultiByte(CP_UTF8, 0, wFull, -1, full, 512, NULL, NULL);
    int ok = Popup_Execute(full);
    if (ok) { History_Add(full); DestroyWindow(GetParent(hEdit)); return; }
    SetWindowTextW(hEdit, L"");
    List_Update("");
}

static LRESULT CALLBACK PopupEditProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_GETDLGCODE) {
        LRESULT code = CallWindowProcW(g_oldEditProc, hWnd, msg, wp, lp);
        return code | DLGC_WANTTAB | DLGC_WANTALLKEYS;
    }
    if (msg == WM_KEYDOWN) {
        switch (wp) {
        case VK_RETURN: Popup_Confirm(hWnd); return 0;
        case VK_SPACE: {
            /* 输入为空时：无历史→关闭，有历史→执行选中记录 */
            wchar_t ws[512]; GetWindowTextW(hWnd, ws, 512);
            wchar_t *p = ws; while (*p == L' ') p++;
            if (*p) {
                Popup_Confirm(hWnd);
            } else if (g_histCount > 0) {
                Popup_Confirm(hWnd);
            } else {
                DestroyWindow(GetParent(hWnd));
            }
            return 0; }
        case VK_ESCAPE: DestroyWindow(GetParent(hWnd)); return 0;
        case VK_TAB:
            if (g_listVisible) {
                int len = (int)SendMessageW(g_hList, LB_GETTEXTLEN, 0, 0);
                if (len > 0 && len < 128) {
                    wchar_t w[128];
                    SendMessageW(g_hList, LB_GETTEXT, 0, (LPARAM)w);
                    SetWindowTextW(g_hPopupEdit, w);
                    SendMessageW(g_hPopupEdit, EM_SETSEL, len, len);
                }
            }
            return 0;
        case VK_UP:
            if (g_listVisible) {
                LRESULT cur = SendMessageW(g_hList, LB_GETCURSEL, 0, 0);
                if (cur > 0) SendMessageW(g_hList, LB_SETCURSEL, cur - 1, 0);
                return 0;
            }
            break;
        case VK_DOWN:
            if (g_listVisible) {
                LRESULT cnt = SendMessageW(g_hList, LB_GETCOUNT, 0, 0);
                LRESULT cur = SendMessageW(g_hList, LB_GETCURSEL, 0, 0);
                if (cur < cnt - 1) SendMessageW(g_hList, LB_SETCURSEL, cur + 1, 0);
                return 0;
            }
            break;
        }
    }
    if (msg == WM_CHAR && (wp > 127 || wp == 9)) return 0;
    LRESULT ret = CallWindowProcW(g_oldEditProc, hWnd, msg, wp, lp);
    if (msg == WM_CHAR || msg == WM_KEYDOWN) {
        wchar_t wTxt[512]; char txt[512];
        GetWindowTextW(hWnd, wTxt, 512);
        WideCharToMultiByte(CP_UTF8, 0, wTxt, -1, txt, 512, NULL, NULL);
        List_Update(txt);
    }
    return ret;
}

static LRESULT CALLBACK PopupWndProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE: {
        g_hPopupFont = CreateFontW(MulDiv(15, g_dpiScale, 96), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                   DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                   CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                   FF_DONTCARE, L"Segoe UI");
        g_hPopupBg = CreateSolidBrush(RGB(255, 255, 255));
        g_hPopupEdit = CreateWindowExW(0, L"EDIT", L"",
                          WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                          1, 4, POPUP_W - 2, 22, hWnd, NULL, g_hInst, NULL);
        SendMessageW(g_hPopupEdit, WM_SETFONT, (WPARAM)g_hPopupFont, TRUE);
        SendMessageW(g_hPopupEdit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(6, 6));
        SendMessageW(g_hPopupEdit, 0x1501, TRUE, (LPARAM)L"Enter command...");
        ImmAssociateContext(g_hPopupEdit, NULL);
        g_hList = CreateWindowExW(0, L"LISTBOX", L"",
                     WS_CHILD | WS_VSCROLL | WS_BORDER | LBS_NOTIFY | LBS_HASSTRINGS,
                     1, POPUP_H, POPUP_W - 2, 1, hWnd, NULL, g_hInst, NULL);
        SendMessageW(g_hList, WM_SETFONT, (WPARAM)g_hPopupFont, TRUE);
        SendMessageW(g_hList, LB_SETITEMHEIGHT, 0, 28);
        g_oldEditProc = (WNDPROC)SetWindowLongPtrW(g_hPopupEdit, GWLP_WNDPROC, (LONG_PTR)PopupEditProc);
        SetFocus(g_hPopupEdit);
        return 0;
    }
    case WM_COMMAND:
        if (HIWORD(wp) == LBN_SELCHANGE && (HWND)lp == g_hList) {
            LRESULT sel = SendMessageW(g_hList, LB_GETCURSEL, 0, 0);
            if (sel != LB_ERR) {
                int len = (int)SendMessageW(g_hList, LB_GETTEXTLEN, sel, 0);
                if (len > 0 && len < 128) {
                    wchar_t w[128];
                    SendMessageW(g_hList, LB_GETTEXT, sel, (LPARAM)w);
                    char cmd[128];
                    WideCharToMultiByte(CP_UTF8, 0, w, -1, cmd, 128, NULL, NULL);
                    int ok = Popup_Execute(cmd);
                    if (ok) { History_Add(cmd); DestroyWindow(hWnd); return 0; }
                    SetWindowTextA(g_hPopupEdit, "");
                    List_Update("");
                }
            }
            return 0;
        }
        break;
    case WM_CTLCOLOREDIT:
        SetBkColor((HDC)wp, RGB(255, 255, 255));
        return (LRESULT)g_hPopupBg;
    case WM_CTLCOLORLISTBOX:
        SetBkColor((HDC)wp, RGB(255, 255, 255));
        return (LRESULT)g_hPopupBg;
    case WM_ACTIVATE:
        if (LOWORD(wp) == WA_INACTIVE) { DestroyWindow(hWnd); return 0; }
        break;
    case WM_TIMER:
        if (wp == 0x99) {
            KillTimer(hWnd, 0x99);
            if (g_hPopupEdit && GetFocus() != g_hPopupEdit) {
                SetActiveWindow(hWnd);
                SetFocus(g_hPopupEdit);
            }
        }
        return 0;
    case WM_DESTROY:
        KillTimer(hWnd, 0x99);
        if (g_hPopupFont) { DeleteObject(g_hPopupFont); g_hPopupFont = NULL; }
        if (g_hPopupBg)   { DeleteObject(g_hPopupBg);   g_hPopupBg   = NULL; }
        g_hPopup = NULL; g_hPopupEdit = NULL; g_hList = NULL;
        g_listVisible = 0;
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wp, lp);
}

void Popup_Show(const char *prefix)
{
    if (g_hPopup) { DestroyWindow(g_hPopup); g_hPopup = NULL; }
    static BOOL clsReg = FALSE;
    if (!clsReg) {
        WNDCLASSEXW wc = {0};
        wc.cbSize = sizeof(WNDCLASSEXW); wc.lpfnWndProc = PopupWndProc;
        wc.hInstance = g_hInst; wc.hCursor = LoadCursor(NULL, IDC_IBEAM);
        wc.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
        wc.lpszClassName = L"SC_InputPopup";
        RegisterClassExW(&wc);
        clsReg = TRUE;
    }
    POINT pt; GetCursorPos(&pt);
    int x = pt.x + 10, y = pt.y + 12;
    if (x + POPUP_W > GetSystemMetrics(SM_CXSCREEN))
        x = GetSystemMetrics(SM_CXSCREEN) - POPUP_W - 5;
    if (y + POPUP_H > GetSystemMetrics(SM_CYSCREEN))
        y = pt.y - POPUP_H - 5;
    if (y < 0) y = 5;
    g_hPopup = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
        L"SC_InputPopup", L"", WS_POPUP | WS_BORDER | WS_CLIPCHILDREN,
        x, y, POPUP_W, POPUP_H, NULL, NULL, g_hInst, NULL);
    SetWindowPos(g_hPopup, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    /* 获取键盘焦点 */
    { HWND hFore = GetForegroundWindow();
      DWORD idFore = GetWindowThreadProcessId(hFore, NULL);
      DWORD idCur  = GetCurrentThreadId();
      if (idFore != idCur) AttachThreadInput(idCur, idFore, TRUE);
      SetForegroundWindow(g_hPopup);
      SetActiveWindow(g_hPopup);
      SetFocus(g_hPopupEdit);
      if (idFore != idCur) AttachThreadInput(idCur, idFore, FALSE);
      /* 首次失败则 80ms 后重试一次 */
      if (GetFocus() != g_hPopupEdit)
          SetTimer(g_hPopup, 0x99, 80, NULL); }
    char txt[512] = "";
    if (prefix && prefix[0]) strncpy(txt, prefix, 511);
    Scripts_Scan();
    List_Update(txt);
}
