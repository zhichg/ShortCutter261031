#include "main.h"
#include <stdlib.h>

/* ── 排序比较函数 ──────────────────────────── */
int CmpCmd(const void *a, const void *b)
{ return lstrcmpiA(((const Command*)a)->name, ((const Command*)b)->name); }

int CmpGes(const void *a, const void *b)
{
    int r = lstrcmpiA(((const GestureDef*)a)->name, ((const GestureDef*)b)->name);
    if (r == 0) return ((const GestureDef*)a)->zone - ((const GestureDef*)b)->zone;
    return r;
}

int CmpHk(const void *a, const void *b)
{ return lstrcmpiA(((const HotkeyDef*)a)->description, ((const HotkeyDef*)b)->description); }

/* ── 脚本同步 ──────────────────────────── */
void Scripts_ScanAndSync(void)
{
    /* 扫描 scripts\ 文件夹 */
    wchar_t dir[MAX_PATH];
    lstrcpyW(dir, GetExeDir());
    lstrcatW(dir, L"\\scripts\\");
    CreateDirectoryW(dir, NULL);

    /* 清除旧的脚本命令（文件已删除的） */
    for (int i = g_cfg.commandCount - 1; i >= 0; i--) {
        if (g_cfg.commands[i].type == CMD_SCRIPT && g_cfg.commands[i].locked) {
            wchar_t wPath[512];
            MultiByteToWideChar(CP_UTF8, 0, g_cfg.commands[i].runPath, -1, wPath, 512);
            if (GetFileAttributesW(wPath) == INVALID_FILE_ATTRIBUTES) {
                /* 文件已删除，移除命令 */
                for (int j = i; j < g_cfg.commandCount - 1; j++)
                    g_cfg.commands[j] = g_cfg.commands[j+1];
                g_cfg.commandCount--;
            }
        }
    }

    /* 添加新的脚本 */
    const wchar_t *exts[] = { L"*.bat", L"*.cmd", L"*.ps1", L"*.vbs" };
    for (int e = 0; e < 4 && g_cfg.commandCount < MAX_COMMANDS; e++) {
        wchar_t pattern[MAX_PATH];
        lstrcpyW(pattern, dir); lstrcatW(pattern, exts[e]);
        WIN32_FIND_DATAW fd;
        HANDLE hFind = FindFirstFileW(pattern, &fd);
        if (hFind == INVALID_HANDLE_VALUE) continue;
        do {
            if (g_cfg.commandCount >= MAX_COMMANDS) break;
            wchar_t fullPath[MAX_PATH];
            lstrcpyW(fullPath, dir); lstrcatW(fullPath, fd.cFileName);
            /* 检查是否已存在 */
            char pathA[512];
            WideCharToMultiByte(CP_UTF8, 0, fullPath, -1, pathA, 512, NULL, NULL);
            int exists = 0;
            for (int i = 0; i < g_cfg.commandCount; i++) {
                if (g_cfg.commands[i].type == CMD_SCRIPT &&
                    lstrcmpiA(g_cfg.commands[i].runPath, pathA) == 0)
                    { exists = 1; break; }
            }
            if (exists) continue;
            /* 添加新脚本命令 */
            Command *c = &g_cfg.commands[g_cfg.commandCount];
            c->id = g_cfg.nextCmdId++;
            c->type = CMD_SCRIPT;
            c->locked = 1;
            strncpy(c->group, "Scripts", 31);
            /* 文件名去扩展名作为 name */
            wchar_t nameOnly[64];
            lstrcpynW(nameOnly, fd.cFileName, 64);
            wchar_t *dot = wcsrchr(nameOnly, L'.');
            if (dot) *dot = 0;
            WideCharToMultiByte(CP_UTF8, 0, nameOnly, -1, c->name, 64, NULL, NULL);
            strncpy(c->runPath, pathA, 511);
            c->keySeq[0] = 0;
            g_cfg.commandCount++;
        } while (FindNextFileW(hFind, &fd));
        FindClose(hFind);
    }
}

/* 预置命令（不可删除，可改名） */
static void Config_CreateBuiltins(void)
{
    struct { char *name; CmdType type; char *path; char *keys; char *group; } blt[] = {
        /* ── Built-in:Keys ── */
        {"Alt+F4",        CMD_KEYS, "", "{Alt down}{F4}{Alt up}",                    "Built-in:Keys"},
        {"Alt+Space,N",   CMD_KEYS, "", "{Alt down}{Space}{Alt up}{Sleep 80}{N}",   "Built-in:Keys"},
        {"Alt+Space,X",   CMD_KEYS, "", "{Alt down}{Space}{Alt up}{Sleep 80}{X}",   "Built-in:Keys"},
        {"Alt+Tab",       CMD_KEYS, "", "{Alt down}{Tab}{Alt up}",                   "Built-in:Keys"},
        {"Alt+Shift+Tab", CMD_KEYS, "", "{Alt down}{Shift down}{Tab}{Alt up}{Shift up}", "Built-in:Keys"},
        {"Alt+Esc",       CMD_KEYS, "", "{Alt down}{Esc}{Alt up}",                   "Built-in:Keys"},
        {"Ctrl+Alt+Tab",  CMD_KEYS, "", "{Ctrl down}{Alt down}{Tab}{Ctrl up}{Alt up}", "Built-in:Keys"},
        {"Win+D",         CMD_KEYS, "", "{Win down}{D}{Win up}",                     "Built-in:Keys"},
        {"Win+M",         CMD_KEYS, "", "{Win down}{M}{Win up}",                     "Built-in:Keys"},
        {"Win+Shift+M",   CMD_KEYS, "", "{Win down}{Shift down}{M}{Win up}{Shift up}","Built-in:Keys"},
        {"Win+Up",        CMD_KEYS, "", "{Win down}{Up}{Win up}",                    "Built-in:Keys"},
        {"Win+Down",      CMD_KEYS, "", "{Win down}{Down}{Win up}",                  "Built-in:Keys"},
        {"Win+Left",      CMD_KEYS, "", "{Win down}{Left}{Win up}",                  "Built-in:Keys"},
        {"Win+Right",     CMD_KEYS, "", "{Win down}{Right}{Win up}",                 "Built-in:Keys"},
        {"Win+Home",      CMD_KEYS, "", "{Win down}{Home}{Win up}",                  "Built-in:Keys"},
        {"Win+Tab",       CMD_KEYS, "", "{Win down}{Tab}{Win up}",                   "Built-in:Keys"},
        {"Ctrl+T",        CMD_KEYS, "", "{Ctrl down}{T}{Ctrl up}",                   "Built-in:Keys"},
        {"Ctrl+Shift+T",  CMD_KEYS, "", "{Ctrl down}{Shift down}{T}{Ctrl up}{Shift up}","Built-in:Keys"},
        {"Ctrl+Tab",      CMD_KEYS, "", "{Ctrl down}{Tab}{Ctrl up}",                 "Built-in:Keys"},
        {"Ctrl+Shift+Tab",CMD_KEYS, "", "{Ctrl down}{Shift down}{Tab}{Ctrl up}{Shift up}","Built-in:Keys"},
        {"Alt+Left",      CMD_KEYS, "", "{Alt down}{Left}{Alt up}",                  "Built-in:Keys"},
        {"Alt+Right",     CMD_KEYS, "", "{Alt down}{Right}{Alt up}",                 "Built-in:Keys"},
        /* ── Built-in:Run ── */
        {"Calculator",    CMD_RUN,  "calc.exe",      "", "Built-in:Run"},
        {"Notepad",       CMD_RUN,  "notepad.exe",   "", "Built-in:Run"},
        {"Task Manager",  CMD_RUN,  "taskmgr.exe",   "", "Built-in:Run"},
        {"Control Panel", CMD_RUN,  "control.exe",   "", "Built-in:Run"},
        {"Explorer",      CMD_RUN,  "explorer",      "", "Built-in:Run"},
        {"Dashboard",     CMD_RUN,  "sc:dashboard",  "", "Built-in:Run"},
    };
    int n = sizeof(blt)/sizeof(blt[0]);
    for (int i = 0; i < n && g_cfg.commandCount < MAX_COMMANDS; i++) {
        /* 检查是否已存在（通过锁定的 name 匹配） */
        int exists = 0;
        for (int j = 0; j < g_cfg.commandCount; j++) {
            if (g_cfg.commands[j].locked &&
                lstrcmpiA(g_cfg.commands[j].name, blt[i].name) == 0)
                { exists = 1; break; }
        }
        if (exists) continue;
        Command *c = &g_cfg.commands[g_cfg.commandCount];
        c->id = g_cfg.nextCmdId++;
        strncpy(c->name, blt[i].name, 63);
        c->type = blt[i].type;
        strncpy(c->runPath, blt[i].path, 511);
        strncpy(c->keySeq, blt[i].keys, 511);
        c->locked = 1;
        strncpy(c->group, blt[i].group, 31);
        g_cfg.commandCount++;
    }
}

void Config_SetDefaults(void)
{
    memset(&g_cfg, 0, sizeof(g_cfg));
    g_cfg.minGesture     = 30;
    g_cfg.matchThreshold = 65;
    g_cfg.mouseButton    = 1;
    g_cfg.trailEnabled     = 1;
    g_cfg.zoneLinesEnabled = 0;
    g_cfg.enabled     = 1;
    g_cfg.nextCmdId   = 1;
    g_cfg.autoStart   = 0;
    g_cfg.dashboardOpacity = 100;
    /* 外观默认值 */
    g_cfg.zoneLineColor   = 0x64DC64;  /* RGB(100,220,100) 绿色 */
    g_cfg.zoneLineWidth   = 2;
    g_cfg.trailGlowColor  = 0x0078DC;  /* RGB(0,120,220) 蓝色 */
    g_cfg.trailGlowWidth  = 5;  /* 超出 Core 的额外宽度：Core(3)+Outer(5)=总外发光8 */
    g_cfg.trailInnerColor = 0xB4DCFF;  /* RGB(180,220,255) 浅蓝色 */
    g_cfg.trailInnerWidth = 3;
}
void Config_Load(void)
{
    const wchar_t *path = GetIniPath();
    wchar_t buf[512];
    int i;

    /* Settings */
    g_cfg.minGesture     = GetPrivateProfileIntW(L"Settings", L"MinGesture", 30, path);
    g_cfg.matchThreshold = GetPrivateProfileIntW(L"Settings", L"Match", 65, path);
    g_cfg.mouseButton    = GetPrivateProfileIntW(L"Settings", L"MouseButton", 1, path);
    g_cfg.trailEnabled     = GetPrivateProfileIntW(L"Settings", L"Trail", 1, path);
    g_cfg.zoneLinesEnabled = GetPrivateProfileIntW(L"Settings", L"ZoneLines", 0, path);
    g_cfg.enabled     = GetPrivateProfileIntW(L"Settings", L"Enabled",      1, path);
    g_cfg.toggleMods  = (BYTE)GetPrivateProfileIntW(L"Settings", L"ToggleMods", 0, path);
    g_cfg.toggleVk    = (BYTE)GetPrivateProfileIntW(L"Settings", L"ToggleVk",   0, path);
    GetPrivateProfileStringW(L"Settings", L"ToggleLabel", L"", buf, 32, path);
    WideCharToMultiByte(CP_UTF8, 0, buf, -1, g_cfg.toggleLabel, 32, NULL, NULL);
    g_cfg.autoStart = GetPrivateProfileIntW(L"Settings", L"AutoStart", 0, path);
    g_cfg.dashboardOpacity = GetPrivateProfileIntW(L"Settings", L"DashboardOpacity", 100, path);
    g_cfg.zoneLineColor   = (int)GetPrivateProfileIntW(L"Settings", L"ZoneColor", 0x64DC64, path);
    g_cfg.zoneLineWidth   = (int)GetPrivateProfileIntW(L"Settings", L"ZoneWidth", 2, path);
    g_cfg.trailGlowColor  = (int)GetPrivateProfileIntW(L"Settings", L"TrailGlowColor", 0x0078DC, path);
    g_cfg.trailGlowWidth  = (int)GetPrivateProfileIntW(L"Settings", L"TrailGlowWidth", 5, path);
    g_cfg.trailInnerColor = (int)GetPrivateProfileIntW(L"Settings", L"TrailInnerColor", 0xB4DCFF, path);
    g_cfg.trailInnerWidth = (int)GetPrivateProfileIntW(L"Settings", L"TrailInnerWidth", 3, path);

    if (g_cfg.minGesture < 10) g_cfg.minGesture = 10;
    if (g_cfg.matchThreshold < 50)  g_cfg.matchThreshold = 50;
    if (g_cfg.matchThreshold > 95)  g_cfg.matchThreshold = 95;
    if (g_cfg.mouseButton < 1)  g_cfg.mouseButton = 1;
    if (g_cfg.mouseButton > 2)  g_cfg.mouseButton = 1;
    /* 宽度 clamp：上限 15，发光允许为 0 */
    if (g_cfg.trailGlowWidth  < 0)  g_cfg.trailGlowWidth  = 0;
    if (g_cfg.trailGlowWidth  > 15) g_cfg.trailGlowWidth  = 15;
    if (g_cfg.trailInnerWidth < 1)  g_cfg.trailInnerWidth = 1;
    if (g_cfg.trailInnerWidth > 15) g_cfg.trailInnerWidth = 15;
    if (g_cfg.zoneLineWidth   < 1)  g_cfg.zoneLineWidth   = 1;
    if (g_cfg.zoneLineWidth   > 15) g_cfg.zoneLineWidth   = 15;
    if (g_cfg.dashboardOpacity < 20) g_cfg.dashboardOpacity = 20;
    if (g_cfg.dashboardOpacity > 100) g_cfg.dashboardOpacity = 100;

    /* Commands */
    {
        int count = GetPrivateProfileIntW(L"Commands", L"Count", -1, path);
        if (count >= 0 && count <= MAX_COMMANDS) {
            g_cfg.commandCount = count;
            int maxId = 0;
            for (i = 0; i < count; i++) {
                wchar_t key[32];
                swprintf(key, 32, L"Id%d", i);
                g_cfg.commands[i].id = (int)GetPrivateProfileIntW(L"Commands", key, 1, path);
                if (g_cfg.commands[i].id > maxId) maxId = g_cfg.commands[i].id;
                swprintf(key, 32, L"Name%d", i);
                GetPrivateProfileStringW(L"Commands", key, L"", buf, 64, path);
                WideCharToMultiByte(CP_UTF8, 0, buf, -1, g_cfg.commands[i].name, 64, NULL, NULL);
                swprintf(key, 32, L"Type%d", i);
                g_cfg.commands[i].type = (CmdType)GetPrivateProfileIntW(L"Commands", key, 0, path);
                swprintf(key, 32, L"Path%d", i);
                GetPrivateProfileStringW(L"Commands", key, L"", buf, 512, path);
                WideCharToMultiByte(CP_UTF8, 0, buf, -1, g_cfg.commands[i].runPath, 512, NULL, NULL);
                swprintf(key, 32, L"Keys%d", i);
                GetPrivateProfileStringW(L"Commands", key, L"", buf, 512, path);
                WideCharToMultiByte(CP_UTF8, 0, buf, -1, g_cfg.commands[i].keySeq, 512, NULL, NULL);
                swprintf(key, 32, L"Locked%d", i);
                g_cfg.commands[i].locked = (int)GetPrivateProfileIntW(L"Commands", key, 0, path);
                swprintf(key, 32, L"Group%d", i);
                GetPrivateProfileStringW(L"Commands", key, L"None", buf, 32, path);
                WideCharToMultiByte(CP_UTF8, 0, buf, -1, g_cfg.commands[i].group, 32, NULL, NULL);
            }
            g_cfg.nextCmdId = maxId + 1;
        }
        Config_CreateBuiltins(); /* 增量添加缺失的预置命令 */
    }

    /* Gestures */
    {
        int count = GetPrivateProfileIntW(L"Gestures", L"Count", -1, path);
        if (count >= 0 && count <= MAX_GESTURES) {
            g_cfg.gestureCount = count;
            for (i = 0; i < count; i++) {
                wchar_t key[32];
                swprintf(key, 32, L"Name%d", i);
                GetPrivateProfileStringW(L"Gestures", key, L"", buf, 64, path);
                WideCharToMultiByte(CP_UTF8, 0, buf, -1, g_cfg.gestures[i].name, 64, NULL, NULL);
                g_cfg.gestures[i].chainCount = 0;
                for (int s = 0; s < 3; s++) {
                    swprintf(key, 32, L"Chain%d_%d", i, s);
                    GetPrivateProfileStringW(L"Gestures", key, L"", buf, 64, path);
                    if (buf[0]) {
                        WideCharToMultiByte(CP_UTF8, 0, buf, -1, g_cfg.gestures[i].chains[s], 64, NULL, NULL);
                        g_cfg.gestures[i].chainCount = s + 1;
                    }
                }
                swprintf(key, 32, L"Mod%d", i);
                g_cfg.gestures[i].modifier = (int)GetPrivateProfileIntW(L"Gestures", key, 0, path);
                swprintf(key, 32, L"CmdId%d", i);
                g_cfg.gestures[i].commandId = (int)GetPrivateProfileIntW(L"Gestures", key, 0, path);
                swprintf(key, 32, L"En%d", i);
                g_cfg.gestures[i].enabled = (int)GetPrivateProfileIntW(L"Gestures", key, 1, path);
                swprintf(key, 32, L"Zone%d", i);
                g_cfg.gestures[i].zone = (int)GetPrivateProfileIntW(L"Gestures", key, 0, path);
                if (g_cfg.gestures[i].zone < 0 || g_cfg.gestures[i].zone >= ZONE_COUNT)
                    g_cfg.gestures[i].zone = ZONE_MAIN;
                /* 加载采样点数据 */
                for (int s = 0; s < 3; s++) {
                    wchar_t ptsBuf[1024];
                    swprintf(key, 32, L"GestPts%d_%d", i, s);
                    GetPrivateProfileStringW(L"Gestures", key, L"", ptsBuf, 1024, path);
                    WideCharToMultiByte(CP_UTF8, 0, ptsBuf, -1, g_cfg.gestures[i].ptsStr[s], 1024, NULL, NULL);
                }
            }
        }
    }

    /* Hotkeys */
    {
        int count = GetPrivateProfileIntW(L"Hotkeys", L"Count", -1, path);
        if (count >= 0 && count <= MAX_HOTKEYS) {
            g_cfg.hotkeyCount = count;
            for (i = 0; i < count; i++) {
                wchar_t key[32];
                swprintf(key, 32, L"Mods%d", i);
                g_cfg.hotkeys[i].mods = (BYTE)GetPrivateProfileIntW(L"Hotkeys", key, 0, path);
                swprintf(key, 32, L"Vk%d", i);
                g_cfg.hotkeys[i].vk = (BYTE)GetPrivateProfileIntW(L"Hotkeys", key, 0, path);
                swprintf(key, 32, L"Label%d", i);
                GetPrivateProfileStringW(L"Hotkeys", key, L"", buf, 32, path);
                WideCharToMultiByte(CP_UTF8, 0, buf, -1, g_cfg.hotkeys[i].label, 32, NULL, NULL);
                swprintf(key, 32, L"Desc%d", i);
                GetPrivateProfileStringW(L"Hotkeys", key, L"", buf, 64, path);
                WideCharToMultiByte(CP_UTF8, 0, buf, -1, g_cfg.hotkeys[i].description, 64, NULL, NULL);
                swprintf(key, 32, L"CmdId%d", i);
                g_cfg.hotkeys[i].commandId = (int)GetPrivateProfileIntW(L"Hotkeys", key, 0, path);
                swprintf(key, 32, L"En%d", i);
                g_cfg.hotkeys[i].enabled = (int)GetPrivateProfileIntW(L"Hotkeys", key, 1, path);
            }
        }
    }

    /* Dashboard Dock */
    {   g_cfg.dockCount = (int)GetPrivateProfileIntW(L"Dashboard", L"DockCount", 0, path);
        if (g_cfg.dockCount < 0) g_cfg.dockCount = 0;
        if (g_cfg.dockCount > MAX_DOCK_ITEMS) g_cfg.dockCount = MAX_DOCK_ITEMS;
        for (int i = 0; i < g_cfg.dockCount; i++) {
            wchar_t key[32];
            swprintf(key, 32, L"DockCmd%d", i);
            g_cfg.dockItems[i].commandId = (int)GetPrivateProfileIntW(L"Dashboard", key, 0, path);
            swprintf(key, 32, L"DockPos%d", i);
            Dashboard_SetDockPos(i, (int)GetPrivateProfileIntW(L"Dashboard", key, i, path));
            swprintf(key, 32, L"DockIcon%d", i);
            GetPrivateProfileStringW(L"Dashboard", key, L"", buf, 512, path);
            WideCharToMultiByte(CP_UTF8, 0, buf, -1, g_cfg.dockItems[i].iconSource, 512, NULL, NULL);
        }
    }

    /* Usage */
    for (int i = 0; i < MAX_COMMANDS; i++) {
        wchar_t key[32];
        swprintf(key, 32, L"U%d", i);
        wchar_t wVal[128];
        if (GetPrivateProfileStringW(L"Usage", key, L"", wVal, 128, path)) {
            int cid = 0, cnt = 0;
            wchar_t wDate[32] = L"";
            wchar_t *p = wcstok(wVal, L",");
            if (p) cid = _wtoi(p);
            p = wcstok(NULL, L",");
            if (p) cnt = _wtoi(p);
            p = wcstok(NULL, L"");
            if (p) lstrcpynW(wDate, p, 32);
            if (cid > 0 && cnt > 0) {
                g_cfg.usage[i].cmdId = cid;
                g_cfg.usage[i].useCount = cnt;
                WideCharToMultiByte(CP_UTF8, 0, wDate, -1, g_cfg.usage[i].lastUsed, 20, NULL, NULL);
            }
        }
    }

    /* History */
    Dashboard_ClearHist();
    for (int i = 0; i < MAX_USAGE_HIST; i++) {
        wchar_t key[32], wVal[128];
        swprintf(key, 32, L"H%d", i);
        if (GetPrivateProfileStringW(L"History", key, L"", wVal, 128, path) && wVal[0]) {
            char name[128];
            WideCharToMultiByte(CP_UTF8, 0, wVal, -1, name, 128, NULL, NULL);
            Dashboard_AddHistItem(name);
        }
    }

    /* 排序 */
    if (g_cfg.commandCount > 1) qsort(g_cfg.commands, g_cfg.commandCount, sizeof(Command), CmpCmd);
    if (g_cfg.gestureCount > 1) qsort(g_cfg.gestures, g_cfg.gestureCount, sizeof(GestureDef), CmpGes);
    if (g_cfg.hotkeyCount  > 1) qsort(g_cfg.hotkeys,  g_cfg.hotkeyCount,  sizeof(HotkeyDef), CmpHk);
}

/* 写入一行宽字符串（原始 UTF-16 LE 字节） */
static void WrLine(FILE *f, const wchar_t *s)
{
    fwrite(s, sizeof(wchar_t), wcslen(s), f);
}

void Config_Save(void)
{
    /* 保存前排序 */
    if (g_cfg.commandCount > 1) qsort(g_cfg.commands, g_cfg.commandCount, sizeof(Command), CmpCmd);
    if (g_cfg.gestureCount > 1) qsort(g_cfg.gestures, g_cfg.gestureCount, sizeof(GestureDef), CmpGes);
    if (g_cfg.hotkeyCount  > 1) qsort(g_cfg.hotkeys,  g_cfg.hotkeyCount,  sizeof(HotkeyDef), CmpHk);

    const wchar_t *path = GetIniPath();
    FILE *f = _wfopen(path, L"wb");
    if (!f) return;
    /* UTF-16 LE BOM */
    { wchar_t bom = 0xFEFF; fwrite(&bom, sizeof(wchar_t), 1, f); }
    wchar_t lb[1024];

    WrLine(f, L"[Settings]\r\n");
    swprintf(lb, 1024, L"MinGesture=%d\r\n", g_cfg.minGesture); WrLine(f, lb);
    swprintf(lb, 1024, L"Match=%d\r\n", g_cfg.matchThreshold); WrLine(f, lb);
    swprintf(lb, 1024, L"MouseButton=%d\r\n", g_cfg.mouseButton); WrLine(f, lb);
    swprintf(lb, 1024, L"Trail=%d\r\n", g_cfg.trailEnabled); WrLine(f, lb);
    swprintf(lb, 1024, L"ZoneLines=%d\r\n", g_cfg.zoneLinesEnabled); WrLine(f, lb);
    swprintf(lb, 1024, L"Enabled=%d\r\n", g_cfg.enabled); WrLine(f, lb);
    swprintf(lb, 1024, L"ToggleMods=%d\r\n", g_cfg.toggleMods); WrLine(f, lb);
    swprintf(lb, 1024, L"ToggleVk=%d\r\n", g_cfg.toggleVk); WrLine(f, lb);
    { wchar_t wl[64]; MultiByteToWideChar(CP_UTF8, 0, g_cfg.toggleLabel, -1, wl, 64);
      swprintf(lb, 1024, L"ToggleLabel=%ls\r\n", wl); WrLine(f, lb); }
    swprintf(lb, 1024, L"AutoStart=%d\r\n", g_cfg.autoStart); WrLine(f, lb);
    swprintf(lb, 1024, L"DashboardOpacity=%d\r\n", g_cfg.dashboardOpacity); WrLine(f, lb);
    swprintf(lb, 1024, L"ZoneColor=%d\r\n",     g_cfg.zoneLineColor);   WrLine(f, lb);
    swprintf(lb, 1024, L"ZoneWidth=%d\r\n",     g_cfg.zoneLineWidth);   WrLine(f, lb);
    swprintf(lb, 1024, L"TrailGlowColor=%d\r\n", g_cfg.trailGlowColor); WrLine(f, lb);
    swprintf(lb, 1024, L"TrailGlowWidth=%d\r\n", g_cfg.trailGlowWidth); WrLine(f, lb);
    swprintf(lb, 1024, L"TrailInnerColor=%d\r\n",g_cfg.trailInnerColor);WrLine(f, lb);
    swprintf(lb, 1024, L"TrailInnerWidth=%d\r\n",g_cfg.trailInnerWidth);WrLine(f, lb);

    WrLine(f, L"[Commands]\r\n");
    swprintf(lb, 1024, L"Count=%d\r\n", g_cfg.commandCount); WrLine(f, lb);
    for (int i = 0; i < g_cfg.commandCount; i++) {
        Command *c = &g_cfg.commands[i];
        wchar_t wn[128], wp[1024], wk[1024];
        MultiByteToWideChar(CP_UTF8, 0, c->name, -1, wn, 128);
        MultiByteToWideChar(CP_UTF8, 0, c->runPath, -1, wp, 1024);
        MultiByteToWideChar(CP_UTF8, 0, c->keySeq, -1, wk, 1024);
        swprintf(lb, 1024, L"Id%d=%d\r\n", i, c->id); WrLine(f, lb);
        swprintf(lb, 1024, L"Name%d=%ls\r\n", i, wn); WrLine(f, lb);
        swprintf(lb, 1024, L"Type%d=%d\r\n", i, (int)c->type); WrLine(f, lb);
        swprintf(lb, 1024, L"Path%d=%ls\r\n", i, wp); WrLine(f, lb);
        swprintf(lb, 1024, L"Keys%d=%ls\r\n", i, wk); WrLine(f, lb);
        swprintf(lb, 1024, L"Locked%d=%d\r\n", i, c->locked); WrLine(f, lb);
        { wchar_t wg[64]; MultiByteToWideChar(CP_UTF8, 0, c->group, -1, wg, 64);
          swprintf(lb, 1024, L"Group%d=%ls\r\n", i, wg); WrLine(f, lb); }
    }

    WrLine(f, L"[Gestures]\r\n");
    swprintf(lb, 1024, L"Count=%d\r\n", g_cfg.gestureCount); WrLine(f, lb);
    for (int i = 0; i < g_cfg.gestureCount; i++) {
        GestureDef *g2 = &g_cfg.gestures[i];
        wchar_t wn[128];
        MultiByteToWideChar(CP_UTF8, 0, g2->name, -1, wn, 128);
        swprintf(lb, 1024, L"Name%d=%ls\r\n", i, wn); WrLine(f, lb);
        for (int s = 0; s < g2->chainCount; s++) {
            wchar_t wc[128];
            MultiByteToWideChar(CP_UTF8, 0, g2->chains[s], -1, wc, 128);
            swprintf(lb, 1024, L"Chain%d_%d=%ls\r\n", i, s, wc); WrLine(f, lb);
        }
        swprintf(lb, 1024, L"Mod%d=%d\r\n", i, g2->modifier); WrLine(f, lb);
        swprintf(lb, 1024, L"CmdId%d=%d\r\n", i, g2->commandId); WrLine(f, lb);
        swprintf(lb, 1024, L"En%d=%d\r\n", i, g2->enabled); WrLine(f, lb);
        swprintf(lb, 1024, L"Zone%d=%d\r\n", i, g2->zone); WrLine(f, lb);
        for (int s = 0; s < g2->chainCount; s++) {
            if (g2->ptsStr[s][0]) {
                wchar_t wpts[1024];
                MultiByteToWideChar(CP_UTF8, 0, g2->ptsStr[s], -1, wpts, 1024);
                swprintf(lb, 1024, L"GestPts%d_%d=%ls\r\n", i, s, wpts); WrLine(f, lb);
            }
        }
    }

    WrLine(f, L"[Hotkeys]\r\n");
    swprintf(lb, 1024, L"Count=%d\r\n", g_cfg.hotkeyCount); WrLine(f, lb);
    for (int i = 0; i < g_cfg.hotkeyCount; i++) {
        HotkeyDef *h = &g_cfg.hotkeys[i];
        wchar_t wl[64], wd[128];
        MultiByteToWideChar(CP_UTF8, 0, h->label, -1, wl, 64);
        MultiByteToWideChar(CP_UTF8, 0, h->description, -1, wd, 128);
        swprintf(lb, 1024, L"Mods%d=%d\r\n", i, h->mods); WrLine(f, lb);
        swprintf(lb, 1024, L"Vk%d=%d\r\n", i, h->vk); WrLine(f, lb);
        swprintf(lb, 1024, L"Label%d=%ls\r\n", i, wl); WrLine(f, lb);
        swprintf(lb, 1024, L"Desc%d=%ls\r\n", i, wd); WrLine(f, lb);
        swprintf(lb, 1024, L"CmdId%d=%d\r\n", i, h->commandId); WrLine(f, lb);
        swprintf(lb, 1024, L"En%d=%d\r\n", i, h->enabled); WrLine(f, lb);
    }

    WrLine(f, L"[Dashboard]\r\n");
    swprintf(lb, 1024, L"DockCount=%d\r\n", g_cfg.dockCount); WrLine(f, lb);
    for (int i = 0; i < g_cfg.dockCount; i++) {
        swprintf(lb, 1024, L"DockCmd%d=%d\r\n", i, g_cfg.dockItems[i].commandId); WrLine(f, lb);
        swprintf(lb, 1024, L"DockPos%d=%d\r\n", i, Dashboard_GetDockPos(i)); WrLine(f, lb);
        if (g_cfg.dockItems[i].iconSource[0]) {
            wchar_t wIcon[1024];
            MultiByteToWideChar(CP_UTF8, 0, g_cfg.dockItems[i].iconSource, -1, wIcon, 1024);
            swprintf(lb, 1024, L"DockIcon%d=%ls\r\n", i, wIcon); WrLine(f, lb);
        }
    }

    /* Usage */
    WrLine(f, L"[Usage]\r\n");
    for (int i = 0; i < MAX_COMMANDS; i++) {
        if (g_cfg.usage[i].cmdId > 0 && g_cfg.usage[i].useCount > 0) {
            swprintf(lb, 1024, L"U%d=%d,%d,%hs\r\n", i, g_cfg.usage[i].cmdId,
                g_cfg.usage[i].useCount, g_cfg.usage[i].lastUsed); WrLine(f, lb);
        }
    }

    /* History */
    WrLine(f, L"[History]\r\n");
    int hc = Dashboard_GetHistCount();
    for (int i = 0; i < hc; i++) {
        const char *hn = Dashboard_GetHistItem(i);
        if (hn && hn[0]) {
            wchar_t wh[128];
            MultiByteToWideChar(CP_UTF8, 0, hn, -1, wh, 128);
            swprintf(lb, 1024, L"H%d=%ls\r\n", i, wh); WrLine(f, lb);
        }
    }

    fclose(f);
}

/* ── 开机自启（任务计划程序静默提权，无 UAC 弹窗） ── */
static DWORD RunSchTasks(const wchar_t *args)
{
    /* 用 ShellExecuteEx 而非 CreateProcess——schtasks 是控制台程序，
       CreateProcess + CREATE_NO_WINDOW 可能导致其运行异常 */
    SHELLEXECUTEINFOW sei = {0};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI;
    sei.lpVerb = L"open";
    sei.lpFile = L"schtasks.exe";
    sei.lpParameters = args;
    sei.nShow = SW_HIDE;
    if (!ShellExecuteExW(&sei))
        return GetLastError();
    WaitForSingleObject(sei.hProcess, 10000);
    DWORD ec = 1;
    GetExitCodeProcess(sei.hProcess, &ec);
    CloseHandle(sei.hProcess);
    return ec;
}

void Config_AutoStart(int enable)
{
    const wchar_t *runKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    const wchar_t *name   = L"ShortCutter";
    HKEY hKey;
    wchar_t exePath[MAX_PATH];

    lstrcpyW(exePath, L"\"");
    lstrcatW(exePath, GetExeDir());
    lstrcatW(exePath, L"\\ShortCutter.exe\"");

    if (enable) {
        /* 先清理旧自启方式 */
        RunSchTasks(L"/delete /tn \"ShortCutter\" /f"); /* 不管成败，删旧任务 */
        if (RegOpenKeyExW(HKEY_CURRENT_USER, runKey, 0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
            RegDeleteValueW(hKey, name); RegCloseKey(hKey); }
        /* 创建任务计划程序任务（Windows 原生提权自启，无 UAC） */
        wchar_t args[800];
        lstrcpyW(args, L"/create /tn \"ShortCutter\" /tr ");
        lstrcatW(args, exePath);
        lstrcatW(args, L" /sc onlogon /rl highest /f");
        DWORD ec = RunSchTasks(args);
        if (ec != 0) {
            /* 失败时弹窗提示，并回退注册表 */
            wchar_t msg[900], num[16];
            wsprintfW(num, L"%lu", ec);
            lstrcpyW(msg, L"schtasks /create failed, exit: "); lstrcatW(msg, num);
            lstrcatW(msg, L"\n"); lstrcatW(msg, args);
            MessageBoxW(NULL, msg, L"AutoStart Error", MB_OK|MB_ICONWARNING);
            if (RegOpenKeyExW(HKEY_CURRENT_USER, runKey, 0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
                RegSetValueExW(hKey, name, 0, REG_SZ, (BYTE*)exePath,
                               (DWORD)(wcslen(exePath) + 1) * sizeof(wchar_t));
                RegCloseKey(hKey); }
        }
    } else {
        RunSchTasks(L"/delete /tn \"ShortCutter\" /f");
        if (RegOpenKeyExW(HKEY_CURRENT_USER, runKey, 0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
            RegDeleteValueW(hKey, name); RegCloseKey(hKey); }
    }
    g_cfg.autoStart = enable;
}
