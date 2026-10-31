#include "main.h"
#include <stdlib.h>
#include <limits.h>
#include <io.h>
#include <errno.h>
#include <taskschd.h>

static DWORD g_configError;
static wchar_t g_configErrorText[512];
static int g_autoStartApplied = -1;

static void Config_Error(DWORD error)
{
    g_configError = error;
    g_configErrorText[0] = 0;
    if (error && !FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
            NULL, error, 0, g_configErrorText, 512, NULL))
        swprintf(g_configErrorText, 512, L"Windows error %lu", error);
}

DWORD Config_GetLastError(void) { return g_configError; }
const wchar_t *Config_GetLastErrorText(void) { return g_configErrorText; }

int CmpCmd(const void *a, const void *b)
{ return lstrcmpiA(((const Command*)a)->name, ((const Command*)b)->name); }
int CmpGes(const void *a, const void *b)
{
    int r = lstrcmpiA(((const GestureDef*)a)->name, ((const GestureDef*)b)->name);
    return r ? r : ((const GestureDef*)a)->zone - ((const GestureDef*)b)->zone;
}
int CmpHk(const void *a, const void *b)
{ return lstrcmpiA(((const HotkeyDef*)a)->description, ((const HotkeyDef*)b)->description); }

int Config_AllocateCommandId(AppConfig *config)
{
    if (!config || config->nextCmdId <= 0 || config->nextCmdId >= INT_MAX) return 0;
    return config->nextCmdId++;
}

static int Config_HasCommand(const AppConfig *config, int id)
{
    if (id <= CMDID_SETTINGS && id >= CMDID_DASHBOARD) return TRUE;
    for (int i = 0; i < config->commandCount; i++)
        if (config->commands[i].id == id && id > 0) return TRUE;
    return FALSE;
}

void Config_UnbindCommand(AppConfig *config, int id)
{
    if (!config || id == CMDID_NONE) return;
    for (int i = 0; i < config->gestureCount; i++)
        if (config->gestures[i].commandId == id)
            { config->gestures[i].commandId = CMDID_NONE; config->gestures[i].enabled = 0; }
    for (int i = 0; i < config->hotkeyCount; i++)
        if (config->hotkeys[i].commandId == id)
            { config->hotkeys[i].commandId = CMDID_NONE; config->hotkeys[i].enabled = 0; }
    for (int i = 0; i < MAX_COMMANDS; i++)
        if (config->usage[i].cmdId == id) memset(&config->usage[i], 0, sizeof(config->usage[i]));
    /* Dock positions belong to Dashboard. Leave an invalid marker for its
       normal reconciliation rather than shifting items without their positions. */
    for (int i = 0; i < config->dockCount; i++)
        if (!config->dockItems[i].folderId && config->dockItems[i].commandId == id)
            config->dockItems[i].commandId = CMDID_NONE;
}

static int Clamp(int value, int low, int high)
{ return value < low ? low : value > high ? high : value; }

void Config_Validate(AppConfig *config)
{
    if (!config) return;
    config->commandCount = Clamp(config->commandCount, 0, MAX_COMMANDS);
    config->gestureCount = Clamp(config->gestureCount, 0, MAX_GESTURES);
    config->hotkeyCount = Clamp(config->hotkeyCount, 0, MAX_HOTKEYS);
    config->dockCount = Clamp(config->dockCount, 0, MAX_DOCK_ITEMS);
    int highest = 0;
    /* Include references before dropping them, protecting old INI files whose
       former highest ID survives only in a binding or usage record. */
    for (int i = 0; i < config->gestureCount; i++)
        if (config->gestures[i].commandId > highest) highest = config->gestures[i].commandId;
    for (int i = 0; i < config->hotkeyCount; i++)
        if (config->hotkeys[i].commandId > highest) highest = config->hotkeys[i].commandId;
    for (int i = 0; i < config->dockCount; i++)
        if (config->dockItems[i].commandId > highest) highest = config->dockItems[i].commandId;
    for (int i = 0; i < MAX_COMMANDS; i++)
        if (config->usage[i].cmdId > highest) highest = config->usage[i].cmdId;
    for (int i = 0; i < config->commandCount;) {
        Command *c = &config->commands[i];
        c->name[SC_NAME_CAP - 1] = c->group[SC_GROUP_CAP - 1] = 0;
        c->runPath[SC_PATH_CAP - 1] = c->arguments[SC_PATH_CAP - 1] = c->workingDir[SC_PATH_CAP - 1] = 0;
        c->keySeq[SC_KEYS_CAP - 1] = 0;
        if (c->id > highest) highest = c->id;
        int duplicate = 0;
        for (int j = 0; j < i; j++) if (config->commands[j].id == c->id) duplicate = 1;
        if (c->id <= 0 || duplicate || !c->name[0] || c->type < CMD_RUN || c->type > CMD_SCRIPT) {
            memmove(c, c + 1, sizeof(*c) * (config->commandCount - i - 1));
            config->commandCount--;
            continue;
        }
        c->locked = !!c->locked;
        c->activateExisting = !!c->activateExisting;
        c->confirmBeforeRun = !!c->confirmBeforeRun;
        c->runAsAdmin = !!c->runAsAdmin;
        i++;
    }
    if (config->nextCmdId <= highest)
        config->nextCmdId = highest >= INT_MAX - 1 ? INT_MAX : highest + 1;
    if (config->nextCmdId < 1) config->nextCmdId = 1;
    for (int i = 0; i < config->gestureCount; i++) {
        GestureDef *g = &config->gestures[i];
        g->name[SC_NAME_CAP - 1] = 0;
        g->chainCount = Clamp(g->chainCount, 0, 3);
        /* Invalid persisted values must not expand to the All areas scope. */
        if (g->zone < ZONE_MAIN || g->zone >= ZONE_COUNT) g->zone = ZONE_MAIN;
        g->modifier &= SC_MOD_CTRL | SC_MOD_ALT | SC_MOD_SHIFT | SC_MOD_WIN | SC_MOD_X1 | SC_MOD_X2;
        for (int s = 0; s < 3; s++) {
            g->chains[s][63] = g->ptsStr[s][1023] = 0;
            for (int p = 0; g->chains[s][p]; p++)
                if (g->chains[s][p] < '0' || g->chains[s][p] > '7') { g->chains[s][0] = 0; break; }
        }
        if (!Config_HasCommand(config, g->commandId)) { g->commandId = CMDID_NONE; g->enabled = 0; }
        int usable = 0;
        for (int s = 0; s < g->chainCount; s++) if (g->chains[s][0]) usable = 1;
        if (!usable) g->enabled = 0;
        g->enabled = !!g->enabled;
    }
    for (int i = 0; i < config->hotkeyCount; i++) {
        HotkeyDef *h = &config->hotkeys[i];
        h->label[SC_LABEL_CAP - 1] = h->description[SC_NAME_CAP - 1] = 0;
        h->mods &= SC_MOD_CTRL | SC_MOD_ALT | SC_MOD_SHIFT | SC_MOD_WIN;
        if (!Config_HasCommand(config, h->commandId)) { h->commandId = CMDID_NONE; h->enabled = 0; }
        if (!h->vk) h->enabled = 0;
        for (int j = 0; j < i; j++)
            if (config->hotkeys[j].enabled && config->hotkeys[j].vk == h->vk && config->hotkeys[j].mods == h->mods)
                h->enabled = 0;
        if (h->vk == config->toggleVk && h->mods == config->toggleMods) h->enabled = 0;
        h->enabled = !!h->enabled;
    }
    for (int i = 0; i < MAX_COMMANDS; i++)
        if (!Config_HasCommand(config, config->usage[i].cmdId) || config->usage[i].useCount < 1)
            memset(&config->usage[i], 0, sizeof(config->usage[i]));
    config->minGesture = Clamp(config->minGesture, 10, 10000);
    config->matchThreshold = Clamp(config->matchThreshold, 50, 95);
    if (config->mouseButton < 1 || config->mouseButton > 2) config->mouseButton = 1;
    config->trailGlowWidth = Clamp(config->trailGlowWidth, 0, 15);
    config->trailInnerWidth = Clamp(config->trailInnerWidth, 1, 15);
    config->zoneLineWidth = Clamp(config->zoneLineWidth, 1, 15);
    config->dashboardOpacity = Clamp(config->dashboardOpacity, 20, 100);
    if (config->dashboardTheme < 0 || config->dashboardTheme >= DASH_THEME_COUNT) config->dashboardTheme = DASH_THEME_WHITE;
    if (config->uiLanguage < 0 || config->uiLanguage >= UI_LANG_COUNT) config->uiLanguage = UI_LANG_ENGLISH;
    config->dockPageCount = Clamp(config->dockPageCount, 1, MAX_DOCK_PAGES);
    config->dashboardQuickInput = config->dashboardQuickInput == 1;
    config->dashboardQuickDesktop = config->dashboardQuickDesktop == 1;
    config->dashboardQuickExplorer = config->dashboardQuickExplorer == 1;
    config->dashboardCompact = !!config->dashboardCompact;
    if(config->settingsWidth<SC_SETTINGS_MIN_WIDTH || config->settingsWidth>SC_SETTINGS_MAX_SIZE)
        config->settingsWidth=SC_SETTINGS_MIN_WIDTH;
    if(config->settingsHeight<SC_SETTINGS_MIN_HEIGHT || config->settingsHeight>SC_SETTINGS_MAX_SIZE)
        config->settingsHeight=SC_SETTINGS_MIN_HEIGHT;
    for(int row=0;row<SC_SETTINGS_LIST_COUNT;row++)for(int col=0;col<SC_SETTINGS_COLUMN_COUNT;col++)
        if(config->settingsColumnWidths[row][col]<0 || config->settingsColumnWidths[row][col]>SC_SETTINGS_MAX_SIZE+1)
            config->settingsColumnWidths[row][col]=0;
    config->enabled = !!config->enabled;
    config->autoStart = !!config->autoStart;
    config->trailEnabled = !!config->trailEnabled;
    config->zoneLinesEnabled = !!config->zoneLinesEnabled;
    config->toggleMods &= SC_MOD_CTRL | SC_MOD_ALT | SC_MOD_SHIFT | SC_MOD_WIN;
    config->toggleLabel[SC_LABEL_CAP - 1] = 0;
    config->configVersion = SC_CONFIG_VERSION;
}

void Scripts_ScanAndSync(void)
{
    wchar_t dir[SC_PATH_CAP], pattern[SC_PATH_CAP];
    if (swprintf(dir, SC_PATH_CAP, L"%ls\\scripts", GetExeDir()) < 0) return;
    CreateDirectoryW(dir, NULL);
    for (int i = g_cfg.commandCount - 1; i >= 0; i--) {
        Command *c = &g_cfg.commands[i];
        if (c->type != CMD_SCRIPT || !c->locked) continue;
        wchar_t path[SC_PATH_CAP];
        if (!SC_WideFromUtf8(path, SC_PATH_CAP, c->runPath)) continue;
        if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) {
            /* A disconnected network/share is not proof that a script was removed. */
            DWORD error = GetLastError();
            if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND) continue;
            Config_UnbindCommand(&g_cfg, c->id);
            memmove(c, c + 1, sizeof(*c) * (g_cfg.commandCount - i - 1));
            g_cfg.commandCount--;
        }
    }
    const wchar_t *extensions[] = { L"*.bat", L"*.cmd", L"*.ps1", L"*.vbs" };
    for (int e = 0; e < 4 && g_cfg.commandCount < MAX_COMMANDS; e++) {
        if (swprintf(pattern, SC_PATH_CAP, L"%ls\\%ls", dir, extensions[e]) < 0) continue;
        WIN32_FIND_DATAW data;
        HANDLE find = FindFirstFileW(pattern, &data);
        if (find == INVALID_HANDLE_VALUE) continue;
        do {
            if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            if (g_cfg.commandCount >= MAX_COMMANDS) break;
            wchar_t full[SC_PATH_CAP], name[SC_NAME_CAP];
            char utf8Path[SC_PATH_CAP], utf8Name[SC_NAME_CAP];
            if (swprintf(full, SC_PATH_CAP, L"%ls\\%ls", dir, data.cFileName) < 0 ||
                !SC_Utf8FromWide(utf8Path, sizeof(utf8Path), full)) continue;
            int exists = 0;
            for (int i = 0; i < g_cfg.commandCount; i++)
                if (g_cfg.commands[i].type == CMD_SCRIPT && !lstrcmpiA(g_cfg.commands[i].runPath, utf8Path)) { exists = 1; break; }
            if (exists) continue;
            lstrcpynW(name, data.cFileName, SC_NAME_CAP);
            wchar_t *dot = wcsrchr(name, L'.');
            if (dot) *dot = 0;
            if (!SC_Utf8FromWide(utf8Name, sizeof(utf8Name), name)) continue;
            int id = Config_AllocateCommandId(&g_cfg);
            if (!id) break;
            Command *c = &g_cfg.commands[g_cfg.commandCount++];
            memset(c, 0, sizeof(*c));
            c->id = id;
            c->type = CMD_SCRIPT;
            c->locked = 1;
            SC_CopyUtf8(c->group, sizeof(c->group), "Scripts");
            SC_CopyUtf8(c->name, sizeof(c->name), utf8Name);
            SC_CopyUtf8(c->runPath, sizeof(c->runPath), utf8Path);
        } while (FindNextFileW(find, &data));
        FindClose(find);
    }
}

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
                (g_cfg.commands[j].type == blt[i].type && lstrcmpiA(g_cfg.commands[j].runPath, blt[i].path) == 0 && lstrcmpiA(g_cfg.commands[j].keySeq, blt[i].keys) == 0))
                { exists = 1; break; }
        }
        if (exists) continue;
        int allocated = Config_AllocateCommandId(&g_cfg);
        if (!allocated) break;
        Command *c = &g_cfg.commands[g_cfg.commandCount];
        memset(c, 0, sizeof(*c));
        c->id = allocated;
        SC_CopyUtf8(c->name, sizeof(c->name), blt[i].name);
        c->type = blt[i].type;
        SC_CopyUtf8(c->runPath, sizeof(c->runPath), blt[i].path);
        SC_CopyUtf8(c->keySeq, sizeof(c->keySeq), blt[i].keys);
        c->locked = 1;
        c->activateExisting = 1;
        SC_CopyUtf8(c->group, sizeof(c->group), blt[i].group);
        g_cfg.commandCount++;
    }
}



void Config_SetDefaults(void)
{
    memset(&g_cfg, 0, sizeof(g_cfg));
    g_cfg.configVersion = SC_CONFIG_VERSION;
    g_cfg.minGesture = 30;
    g_cfg.matchThreshold = 65;
    g_cfg.mouseButton = 1;
    g_cfg.trailEnabled = g_cfg.enabled = 1;
    g_cfg.nextCmdId = g_cfg.dockPageCount = 1;
    g_cfg.dashboardOpacity = 100;
    g_cfg.dashboardTheme = DASH_THEME_WHITE;
    g_cfg.settingsWidth = SC_SETTINGS_MIN_WIDTH;
    g_cfg.settingsHeight = SC_SETTINGS_MIN_HEIGHT;
    g_cfg.dashboardQuickDesktop = g_cfg.dashboardQuickExplorer = 1;
    g_cfg.zoneLineColor = 0x64DC64;
    g_cfg.zoneLineWidth = 2;
    g_cfg.trailGlowColor = 0x0078DC;
    g_cfg.trailGlowWidth = 5;
    g_cfg.trailInnerColor = 0xB4DCFF;
    g_cfg.trailInnerWidth = 3;
}

/* v3 text escaping preserves multiline macros, backslashes and quoted args.
   Old files without TextEncoding=1 remain literal and retain their paths. */
static void DecodeText(wchar_t *text)
{
    wchar_t *read = text, *write = text;
    while (*read) {
        if (*read == L'\\' && read[1]) {
            wchar_t escaped = read[1];
            if (escaped == L'n' || escaped == L'r' || escaped == L't' || escaped == L'\\' || escaped == L'"') {
                *write++ = escaped == L'n' ? L'\n' : escaped == L'r' ? L'\r' : escaped == L't' ? L'\t' : escaped;
                read += 2;
                continue;
            }
        }
        *write++ = *read++;
    }
    *write = 0;
}

static BOOL ReadText(const wchar_t *section, const wchar_t *key, const wchar_t *fallback,
    char *destination, size_t capacity, int escaped)
{
    wchar_t buffer[SC_KEYS_CAP * 2 + 16];
    DWORD count = GetPrivateProfileStringW(section, key, fallback, buffer,
        sizeof(buffer) / sizeof(buffer[0]), GetIniPath());
    if (count >= sizeof(buffer) / sizeof(buffer[0]) - 1) {
        destination[0] = 0;
        Config_Error(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    if (escaped) DecodeText(buffer);
    if (!SC_Utf8FromWide(destination, capacity, buffer)) {
        Config_Error(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    return TRUE;
}

void Config_Load(void)
{
    Config_Error(0);
    Config_SetDefaults();
    const wchar_t *path = GetIniPath();
    int escaped = GetPrivateProfileIntW(L"Settings", L"TextEncoding", 0, path) == 1;
#define READ_SETTING(field, key, fallback) g_cfg.field = (int)GetPrivateProfileIntW(L"Settings", L##key, fallback, path)
    READ_SETTING(minGesture, "MinGesture", 30);
    READ_SETTING(matchThreshold, "Match", 65);
    READ_SETTING(mouseButton, "MouseButton", 1);
    READ_SETTING(trailEnabled, "Trail", 1);
    READ_SETTING(zoneLinesEnabled, "ZoneLines", 0);
    READ_SETTING(enabled, "Enabled", 1);
    READ_SETTING(toggleMods, "ToggleMods", 0);
    READ_SETTING(toggleVk, "ToggleVk", 0);
    ReadText(L"Settings", L"ToggleLabel", L"", g_cfg.toggleLabel, sizeof(g_cfg.toggleLabel), escaped);
    READ_SETTING(autoStart, "AutoStart", 0);
    READ_SETTING(uiLanguage, "UILanguage", UI_LANG_ENGLISH);
    READ_SETTING(dashboardOpacity, "DashboardOpacity", 100);
    READ_SETTING(dashboardTheme, "DashboardTheme", DASH_THEME_WHITE);
    READ_SETTING(dashboardQuickInput, "DashboardQuickInput", 0);
    READ_SETTING(dashboardQuickDesktop, "DashboardQuickDesktop", 1);
    READ_SETTING(dashboardQuickExplorer, "DashboardQuickExplorer", 1);
    READ_SETTING(dashboardCompact, "DashboardCompact", 0);
    READ_SETTING(settingsWidth, "SettingsWindowWidth", SC_SETTINGS_MIN_WIDTH);
    READ_SETTING(settingsHeight, "SettingsWindowHeight", SC_SETTINGS_MIN_HEIGHT);
    READ_SETTING(zoneLineColor, "ZoneColor", 0x64DC64);
    READ_SETTING(zoneLineWidth, "ZoneWidth", 2);
    READ_SETTING(trailGlowColor, "TrailGlowColor", 0x0078DC);
    READ_SETTING(trailGlowWidth, "TrailGlowWidth", 5);
    READ_SETTING(trailInnerColor, "TrailInnerColor", 0xB4DCFF);
    READ_SETTING(trailInnerWidth, "TrailInnerWidth", 3);
    READ_SETTING(nextCmdId, "NextCmdId", 1);
#undef READ_SETTING
    wchar_t key[64];
    const wchar_t *columnGroups[]={L"CommandColumn",L"GestureColumn",L"HotkeyColumn"};
    for(int row=0;row<SC_SETTINGS_LIST_COUNT;row++)for(int col=0;col<SC_SETTINGS_COLUMN_COUNT;col++) {
        swprintf(key,64,L"%ls%d",columnGroups[row],col);
        int width=(int)GetPrivateProfileIntW(L"Settings",key,-1,path);
        g_cfg.settingsColumnWidths[row][col]=width>=0 && width<=SC_SETTINGS_MAX_SIZE?width+1:0;
    }
    g_cfg.commandCount = Clamp(GetPrivateProfileIntW(L"Commands", L"Count", 0, path), 0, MAX_COMMANDS);
    for (int i = 0; i < g_cfg.commandCount; i++) {
        Command *c = &g_cfg.commands[i];
#define READ_CMD_INT(field, label, fallback) swprintf(key, 64, L##label L"%d", i); c->field = GetPrivateProfileIntW(L"Commands", key, fallback, path)
#define READ_CMD_TEXT(field, label, fallback) swprintf(key, 64, L##label L"%d", i); ReadText(L"Commands", key, L##fallback, c->field, sizeof(c->field), escaped)
        READ_CMD_INT(id, "Id", 0);
        READ_CMD_INT(type, "Type", 0);
        READ_CMD_INT(locked, "Locked", 0);
        READ_CMD_INT(activateExisting, "ActivateExisting", 1);
        READ_CMD_INT(confirmBeforeRun, "ConfirmBeforeRun", 0);
        READ_CMD_INT(runAsAdmin, "RunAsAdmin", 0);
        READ_CMD_TEXT(name, "Name", "");
        READ_CMD_TEXT(runPath, "Path", "");
        READ_CMD_TEXT(keySeq, "Keys", "");
        READ_CMD_TEXT(arguments, "Arguments", "");
        READ_CMD_TEXT(workingDir, "WorkingDir", "");
        READ_CMD_TEXT(group, "Group", "None");
#undef READ_CMD_INT
#undef READ_CMD_TEXT
    }
    g_cfg.gestureCount = Clamp(GetPrivateProfileIntW(L"Gestures", L"Count", 0, path), 0, MAX_GESTURES);
    for (int i = 0; i < g_cfg.gestureCount; i++) {
        GestureDef *g = &g_cfg.gestures[i];
        swprintf(key, 64, L"Name%d", i);
        ReadText(L"Gestures", key, L"", g->name, sizeof(g->name), escaped);
        for (int s = 0; s < 3; s++) {
            swprintf(key, 64, L"Chain%d_%d", i, s);
            ReadText(L"Gestures", key, L"", g->chains[s], sizeof(g->chains[s]), escaped);
            if (g->chains[s][0]) g->chainCount = s + 1;
            swprintf(key, 64, L"GestPts%d_%d", i, s);
            ReadText(L"Gestures", key, L"", g->ptsStr[s], sizeof(g->ptsStr[s]), escaped);
        }
#define READ_GES(field, label, fallback) swprintf(key,64,L##label L"%d",i); g->field=GetPrivateProfileIntW(L"Gestures",key,fallback,path)
        READ_GES(modifier, "Mod", 0);
        READ_GES(commandId, "CmdId", 0);
        READ_GES(enabled, "En", 1);
        READ_GES(zone, "Zone", 0);
#undef READ_GES
    }
    g_cfg.hotkeyCount = Clamp(GetPrivateProfileIntW(L"Hotkeys", L"Count", 0, path), 0, MAX_HOTKEYS);
    for (int i = 0; i < g_cfg.hotkeyCount; i++) {
        HotkeyDef *h = &g_cfg.hotkeys[i];
#define READ_HK(field, label, fallback) swprintf(key,64,L##label L"%d",i); h->field=GetPrivateProfileIntW(L"Hotkeys",key,fallback,path)
        READ_HK(mods, "Mods", 0);
        READ_HK(vk, "Vk", 0);
        READ_HK(commandId, "CmdId", 0);
        READ_HK(enabled, "En", 1);
#undef READ_HK
        swprintf(key,64,L"Label%d",i); ReadText(L"Hotkeys",key,L"",h->label,sizeof(h->label),escaped);
        swprintf(key,64,L"Desc%d",i); ReadText(L"Hotkeys",key,L"",h->description,sizeof(h->description),escaped);
    }
    g_cfg.dockCount = Clamp(GetPrivateProfileIntW(L"Dashboard",L"DockCount",0,path),0,MAX_DOCK_ITEMS);
    g_cfg.dockPageCount = GetPrivateProfileIntW(L"Dashboard",L"PageCount",1,path);
    for (int i=0;i<g_cfg.dockCount;i++) {
        DockItem *d=&g_cfg.dockItems[i];
#define READ_DOCK(field, label, fallback) swprintf(key,64,L##label L"%d",i); d->field=GetPrivateProfileIntW(L"Dashboard",key,fallback,path)
        READ_DOCK(commandId,"DockCmd",0);
        READ_DOCK(parentFolderId,"DockParent",0);
        READ_DOCK(folderId,"DockFolder",0);
        READ_DOCK(pageCount,"DockPages",d->folderId>0 ? 1 : 0);
#undef READ_DOCK
        swprintf(key,64,L"DockPos%d",i); Dashboard_SetDockPos(i,GetPrivateProfileIntW(L"Dashboard",key,i,path));
        swprintf(key,64,L"DockIcon%d",i); ReadText(L"Dashboard",key,L"",d->iconSource,sizeof(d->iconSource),escaped);
        swprintf(key,64,L"DockName%d",i); ReadText(L"Dashboard",key,L"",d->folderName,sizeof(d->folderName),escaped);
    }
    for (int i=0;i<MAX_COMMANDS;i++) {
        wchar_t value[128];
        swprintf(key,64,L"U%d",i);
        if (GetPrivateProfileStringW(L"Usage",key,L"",value,128,path)) {
            int id=0,count=0;
            wchar_t date[20]=L"";
            if (swscanf(value,L"%d,%d,%19ls",&id,&count,date)>=2 && id>0 && count>0) {
                g_cfg.usage[i].cmdId=id;
                g_cfg.usage[i].useCount=count;
                SC_Utf8FromWide(g_cfg.usage[i].lastUsed,sizeof(g_cfg.usage[i].lastUsed),date);
            }
        }
    }
    /* Validate IDs before adding builtins, preserving the persistent allocator. */
    Config_Validate(&g_cfg);
    Config_CreateBuiltins();
    Config_Validate(&g_cfg);
    Dashboard_ClearHist();
    for (int i=0;i<MAX_USAGE_HIST;i++) {
        char name[SC_NAME_CAP];
        swprintf(key,64,L"H%d",i);
        if (ReadText(L"History",key,L"",name,sizeof(name),escaped) && name[0]) Dashboard_AddHistItem(name);
    }
    if(g_cfg.commandCount>1) qsort(g_cfg.commands,g_cfg.commandCount,sizeof(Command),CmpCmd);
    if(g_cfg.gestureCount>1) qsort(g_cfg.gestures,g_cfg.gestureCount,sizeof(GestureDef),CmpGes);
    if(g_cfg.hotkeyCount>1) qsort(g_cfg.hotkeys,g_cfg.hotkeyCount,sizeof(HotkeyDef),CmpHk);
    /* Loading configuration is read-only with respect to Windows startup. */
    g_autoStartApplied=g_cfg.autoStart;
}

static BOOL WrLine(FILE *file,const wchar_t *text)
{
    size_t length=wcslen(text);
    return fwrite(text,sizeof(wchar_t),length,file)==length;
}

static BOOL WriteInt(FILE *file,const wchar_t *key,int value)
{
    wchar_t line[128];
    swprintf(line,128,L"%ls=%d\r\n",key,value);
    return WrLine(file,line);
}

static BOOL WriteText(FILE *file,const wchar_t *key,const char *text,size_t capacity)
{
    /* Reject unterminated UTF-8 before asking Windows to scan for a terminator. */
    if (!memchr(text,0,capacity)) { Config_Error(ERROR_INVALID_DATA); return FALSE; }
    int needed=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text,-1,NULL,0);
    if(needed<=0) { Config_Error(ERROR_NO_UNICODE_TRANSLATION); return FALSE; }
    wchar_t *wide=(wchar_t*)malloc((size_t)needed*sizeof(wchar_t));
    if(!wide) { Config_Error(ERROR_NOT_ENOUGH_MEMORY); return FALSE; }
    if(!SC_WideFromUtf8(wide,needed,text)) { free(wide); Config_Error(ERROR_NO_UNICODE_TRANSLATION); return FALSE; }
    BOOL ok=WrLine(file,key)&&WrLine(file,L"=");
    for(const wchar_t *p=wide;ok&&*p;p++) {
        const wchar_t *escape=*p==L'\\'?L"\\\\":*p==L'\r'?L"\\r":*p==L'\n'?L"\\n":*p==L'\t'?L"\\t":*p==L'"'?L"\\\"":NULL;
        if(escape) ok=WrLine(file,escape);
        else ok=fwrite(p,sizeof(wchar_t),1,file)==1;
    }
    if(ok) ok=WrLine(file,L"\r\n");
    free(wide);
    return ok;
}

static BOOL Config_Write(FILE *file)
{
    wchar_t key[64];
#define SECTION(text) if(!WrLine(file,L##text L"\r\n")) return FALSE
#define INT(key, value) if(!WriteInt(file,L##key,value)) return FALSE
#define WRITE_TEXT(key, value) if(!WriteText(file,L##key,value,sizeof(value))) return FALSE
    wchar_t bom=0xFEFF;
    if(fwrite(&bom,sizeof(bom),1,file)!=1) return FALSE;
    SECTION("[Settings]");
    INT("ConfigVersion",SC_CONFIG_VERSION);
    INT("TextEncoding",1);
    INT("NextCmdId",g_cfg.nextCmdId);
    INT("MinGesture",g_cfg.minGesture); INT("Match",g_cfg.matchThreshold);
    INT("MouseButton",g_cfg.mouseButton); INT("Trail",g_cfg.trailEnabled);
    INT("ZoneLines",g_cfg.zoneLinesEnabled); INT("Enabled",g_cfg.enabled);
    INT("ToggleMods",g_cfg.toggleMods); INT("ToggleVk",g_cfg.toggleVk);
    WRITE_TEXT("ToggleLabel",g_cfg.toggleLabel);
    INT("AutoStart",g_cfg.autoStart); INT("UILanguage",g_cfg.uiLanguage);
    INT("DashboardOpacity",g_cfg.dashboardOpacity); INT("DashboardTheme",g_cfg.dashboardTheme);
    INT("DashboardQuickInput",g_cfg.dashboardQuickInput); INT("DashboardQuickDesktop",g_cfg.dashboardQuickDesktop);
    INT("DashboardQuickExplorer",g_cfg.dashboardQuickExplorer); INT("DashboardCompact",g_cfg.dashboardCompact);
    INT("SettingsWindowWidth",g_cfg.settingsWidth); INT("SettingsWindowHeight",g_cfg.settingsHeight);
    const wchar_t *columnGroups[]={L"CommandColumn",L"GestureColumn",L"HotkeyColumn"};
    for(int row=0;row<SC_SETTINGS_LIST_COUNT;row++)for(int col=0;col<SC_SETTINGS_COLUMN_COUNT;col++) {
        swprintf(key,64,L"%ls%d",columnGroups[row],col);
        if(!WriteInt(file,key,g_cfg.settingsColumnWidths[row][col]-1))return FALSE;
    }
    INT("ZoneColor",g_cfg.zoneLineColor); INT("ZoneWidth",g_cfg.zoneLineWidth);
    INT("TrailGlowColor",g_cfg.trailGlowColor); INT("TrailGlowWidth",g_cfg.trailGlowWidth);
    INT("TrailInnerColor",g_cfg.trailInnerColor); INT("TrailInnerWidth",g_cfg.trailInnerWidth);
    SECTION("[Commands]"); INT("Count",g_cfg.commandCount);
    for(int i=0;i<g_cfg.commandCount;i++) {
        const Command *c=&g_cfg.commands[i];
#define CMD_INT(label,field) swprintf(key,64,L##label L"%d",i); if(!WriteInt(file,key,c->field)) return FALSE
#define CMD_TEXT(label,field) swprintf(key,64,L##label L"%d",i); if(!WriteText(file,key,c->field,sizeof(c->field))) return FALSE
        CMD_INT("Id",id); CMD_INT("Type",type); CMD_INT("Locked",locked);
        CMD_INT("ActivateExisting",activateExisting); CMD_INT("ConfirmBeforeRun",confirmBeforeRun); CMD_INT("RunAsAdmin",runAsAdmin);
        CMD_TEXT("Name",name); CMD_TEXT("Path",runPath); CMD_TEXT("Keys",keySeq);
        CMD_TEXT("Arguments",arguments); CMD_TEXT("WorkingDir",workingDir); CMD_TEXT("Group",group);
#undef CMD_INT
#undef CMD_TEXT
    }
    SECTION("[Gestures]"); INT("Count",g_cfg.gestureCount);
    for(int i=0;i<g_cfg.gestureCount;i++) {
        const GestureDef *g=&g_cfg.gestures[i];
        swprintf(key,64,L"Name%d",i); if(!WriteText(file,key,g->name,sizeof(g->name))) return FALSE;
        for(int s=0;s<g->chainCount;s++) {
            swprintf(key,64,L"Chain%d_%d",i,s); if(!WriteText(file,key,g->chains[s],sizeof(g->chains[s]))) return FALSE;
            swprintf(key,64,L"GestPts%d_%d",i,s); if(!WriteText(file,key,g->ptsStr[s],sizeof(g->ptsStr[s]))) return FALSE;
        }
        swprintf(key,64,L"Mod%d",i); if(!WriteInt(file,key,g->modifier)) return FALSE;
        swprintf(key,64,L"CmdId%d",i); if(!WriteInt(file,key,g->commandId)) return FALSE;
        swprintf(key,64,L"En%d",i); if(!WriteInt(file,key,g->enabled)) return FALSE;
        swprintf(key,64,L"Zone%d",i); if(!WriteInt(file,key,g->zone)) return FALSE;
    }
    SECTION("[Hotkeys]"); INT("Count",g_cfg.hotkeyCount);
    for(int i=0;i<g_cfg.hotkeyCount;i++) {
        const HotkeyDef *h=&g_cfg.hotkeys[i];
        swprintf(key,64,L"Mods%d",i); if(!WriteInt(file,key,h->mods)) return FALSE;
        swprintf(key,64,L"Vk%d",i); if(!WriteInt(file,key,h->vk)) return FALSE;
        swprintf(key,64,L"CmdId%d",i); if(!WriteInt(file,key,h->commandId)) return FALSE;
        swprintf(key,64,L"En%d",i); if(!WriteInt(file,key,h->enabled)) return FALSE;
        swprintf(key,64,L"Label%d",i); if(!WriteText(file,key,h->label,sizeof(h->label))) return FALSE;
        swprintf(key,64,L"Desc%d",i); if(!WriteText(file,key,h->description,sizeof(h->description))) return FALSE;
    }
    SECTION("[Dashboard]"); INT("Version",2); INT("PageCount",g_cfg.dockPageCount); INT("DockCount",g_cfg.dockCount);
    for(int i=0;i<g_cfg.dockCount;i++) {
        const DockItem *d=&g_cfg.dockItems[i];
        swprintf(key,64,L"DockCmd%d",i); if(!WriteInt(file,key,d->commandId)) return FALSE;
        swprintf(key,64,L"DockPos%d",i); if(!WriteInt(file,key,Dashboard_GetDockPos(i))) return FALSE;
        swprintf(key,64,L"DockParent%d",i); if(!WriteInt(file,key,d->parentFolderId)) return FALSE;
        swprintf(key,64,L"DockFolder%d",i); if(!WriteInt(file,key,d->folderId)) return FALSE;
        swprintf(key,64,L"DockPages%d",i); if(!WriteInt(file,key,d->pageCount)) return FALSE;
        swprintf(key,64,L"DockName%d",i); if(!WriteText(file,key,d->folderName,sizeof(d->folderName))) return FALSE;
        swprintf(key,64,L"DockIcon%d",i); if(!WriteText(file,key,d->iconSource,sizeof(d->iconSource))) return FALSE;
    }
    SECTION("[Usage]");
    for(int i=0;i<MAX_COMMANDS;i++) if(g_cfg.usage[i].cmdId>0&&g_cfg.usage[i].useCount>0) {
        wchar_t line[128],date[20];
        if(!SC_WideFromUtf8(date,20,g_cfg.usage[i].lastUsed)) return FALSE;
        swprintf(line,128,L"U%d=%d,%d,%ls\r\n",i,g_cfg.usage[i].cmdId,g_cfg.usage[i].useCount,date);
        if(!WrLine(file,line)) return FALSE;
    }
    SECTION("[History]");
    int count=Clamp(Dashboard_GetHistCount(),0,MAX_USAGE_HIST);
    for(int i=0;i<count;i++) {
        const char *name=Dashboard_GetHistItem(i);
        if(!name||!name[0]) continue;
        swprintf(key,64,L"H%d",i);
        if(!WriteText(file,key,name,SC_NAME_CAP)) return FALSE;
    }
#undef SECTION
#undef INT
#undef WRITE_TEXT
    return !ferror(file);
}

static DWORD Config_FileOpenError(void)
{
    switch(errno) {
        case ENOENT: return ERROR_PATH_NOT_FOUND;
        case EACCES: return ERROR_ACCESS_DENIED;
        case ENOSPC: return ERROR_DISK_FULL;
        case ENOMEM: return ERROR_NOT_ENOUGH_MEMORY;
        default: return ERROR_OPEN_FAILED;
    }
}

BOOL Config_Save(void)
{
    Config_Error(0);
    /* Do not reorder or mutate the live model while persisting a UI draft. */
    if(g_cfg.commandCount<0||g_cfg.commandCount>MAX_COMMANDS||g_cfg.gestureCount<0||g_cfg.gestureCount>MAX_GESTURES||
       g_cfg.hotkeyCount<0||g_cfg.hotkeyCount>MAX_HOTKEYS||g_cfg.dockCount<0||g_cfg.dockCount>MAX_DOCK_ITEMS) {
        Config_Error(ERROR_INVALID_DATA); return FALSE;
    }
    for(int i=0;i<g_cfg.gestureCount;i++) if(g_cfg.gestures[i].chainCount<0||g_cfg.gestures[i].chainCount>3)
        { Config_Error(ERROR_INVALID_DATA); return FALSE; }
    const wchar_t *path=GetIniPath();
    wchar_t temporary[SC_PATH_CAP],backup[SC_PATH_CAP];
    if(swprintf(temporary,SC_PATH_CAP,L"%ls.tmp.%lu",path,GetCurrentProcessId())<0 ||
       swprintf(backup,SC_PATH_CAP,L"%ls.bak",path)<0) { Config_Error(ERROR_FILENAME_EXCED_RANGE); return FALSE; }
    FILE *file=_wfopen(temporary,L"wb");
    if(!file) { Config_Error(Config_FileOpenError()); return FALSE; }
    BOOL ok=Config_Write(file);
    if(ok && fflush(file)!=0) ok=FALSE;
    if(ok && _commit(_fileno(file))!=0) ok=FALSE;
    if(fclose(file)!=0) ok=FALSE;
    if(!ok) {
        if(!g_configError) Config_Error(ERROR_WRITE_FAULT);
        DeleteFileW(temporary);
        return FALSE;
    }
    /* The INI APIs cache reads. Flush before replacing and invalidate afterward. */
    WritePrivateProfileStringW(NULL,NULL,NULL,path);
    if(GetFileAttributesW(path)!=INVALID_FILE_ATTRIBUTES)
        ok=ReplaceFileW(path,temporary,backup,0,NULL,NULL);
    else if(GetLastError()==ERROR_FILE_NOT_FOUND || GetLastError()==ERROR_PATH_NOT_FOUND)
        ok=MoveFileExW(temporary,path,MOVEFILE_WRITE_THROUGH);
    else ok=FALSE;
    if(!ok) {
        DWORD error=GetLastError();
        DeleteFileW(temporary);
        Config_Error(error?error:ERROR_WRITE_FAULT);
        return FALSE;
    }
    WritePrivateProfileStringW(NULL,NULL,NULL,path);
    Config_Error(0);
    return TRUE;
}

static DWORD Config_RemoveLegacyStartupTaskElevated(void)
{
    /* Old releases registered a highest-privilege task. Only its removal needs
       consent; keep this process and the per-user Run value unelevated. Use the
       Windows binary and a fixed task name, never a shell or user-supplied text. */
    wchar_t executable[SC_PATH_CAP];
    UINT length=GetSystemDirectoryW(executable,SC_PATH_CAP);
    if(!length)return GetLastError();
    if(length>=SC_PATH_CAP || length+wcslen(L"\\schtasks.exe")>=SC_PATH_CAP)
        return ERROR_FILENAME_EXCED_RANGE;
    lstrcatW(executable,L"\\schtasks.exe");
    SHELLEXECUTEINFOW execute={0};
    execute.cbSize=sizeof(execute);
    execute.fMask=SEE_MASK_NOCLOSEPROCESS|SEE_MASK_NOASYNC|SEE_MASK_FLAG_NO_UI;
    execute.hwnd=GetActiveWindow();
    execute.lpVerb=L"runas";
    execute.lpFile=executable;
    execute.lpParameters=L"/Delete /TN \"\\ShortCutter\" /F";
    execute.nShow=SW_HIDE;
    if(!ShellExecuteExW(&execute))return GetLastError();
    if(!execute.hProcess)return ERROR_INVALID_HANDLE;
    DWORD index=0,exitCode=0,error=ERROR_SUCCESS;
    /* This runs on the UI thread with COM initialized. Use a COM modal wait so
       Windows/COM messages remain serviced until the short-lived tool exits. */
    HRESULT waited=CoWaitForMultipleHandles(0,INFINITE,1,&execute.hProcess,&index);
    if(FAILED(waited))error=HRESULT_FACILITY(waited)==FACILITY_WIN32?HRESULT_CODE(waited):(DWORD)waited;
    else if(!GetExitCodeProcess(execute.hProcess,&exitCode))error=GetLastError();
    else if(exitCode)error=ERROR_ACCESS_DENIED;
    CloseHandle(execute.hProcess);
    return error;
}

static DWORD Config_RemoveLegacyStartupTask(void)
{
    /* Only called after a user-requested startup state change. No task is ever
       created, and Config_Load never reaches this code. */
    HRESULT initialized=CoInitializeEx(NULL,COINIT_APARTMENTTHREADED);
    if(FAILED(initialized)&&initialized!=RPC_E_CHANGED_MODE) return (DWORD)initialized;
    ITaskService *service=NULL;
    ITaskFolder *folder=NULL;
    IRegisteredTask *task=NULL;
    int found=0;
    /* Some MinGW uuid libraries omit the Task Scheduler GUIDs. */
    static const GUID schedulerClass={0x0f87369f,0xa4e5,0x4cfc,{0xbd,0x3e,0x73,0xe6,0x15,0x45,0x72,0xdd}};
    static const GUID serviceInterface={0x2faba4c7,0x4da9,0x4013,{0x96,0x97,0x20,0xcc,0x3f,0xd4,0x0f,0x85}};
    BSTR root=SysAllocString(L"\\"),name=SysAllocString(L"ShortCutter");
    HRESULT result=(!root||!name)?E_OUTOFMEMORY:
        CoCreateInstance(&schedulerClass,NULL,CLSCTX_INPROC_SERVER,&serviceInterface,(void**)&service);
    VARIANT empty;
    VariantInit(&empty);
    if(SUCCEEDED(result)) result=service->lpVtbl->Connect(service,empty,empty,empty,empty);
    if(SUCCEEDED(result)) result=service->lpVtbl->GetFolder(service,root,&folder);
    /* Checking first avoids requesting delete access (or elevation) when there
       is no legacy task to migrate. */
    if(SUCCEEDED(result)) result=folder->lpVtbl->GetTask(folder,name,&task);
    if(SUCCEEDED(result)) {
        found=1;
        result=folder->lpVtbl->DeleteTask(folder,name,0);
    }
    if(task) task->lpVtbl->Release(task);
    if(folder) folder->lpVtbl->Release(folder);
    if(service) service->lpVtbl->Release(service);
    SysFreeString(root);
    SysFreeString(name);
    if(found && result==E_ACCESSDENIED) {
        DWORD elevatedError=Config_RemoveLegacyStartupTaskElevated();
        result=HRESULT_FROM_WIN32(elevatedError);
    }
    if(SUCCEEDED(initialized)) CoUninitialize();
    if(SUCCEEDED(result)||result==HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND)||
        result==HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND)) return ERROR_SUCCESS;
    return HRESULT_FACILITY(result)==FACILITY_WIN32?HRESULT_CODE(result):(DWORD)result;
}

BOOL Config_AutoStart(int enable)
{
    enable=!!enable;
    Config_Error(0);
    if(enable==g_autoStartApplied || (g_autoStartApplied<0 && enable==g_cfg.autoStart)) {
        g_autoStartApplied=enable;
        return TRUE;
    }
    /* Explicit state changes use the per-user Run value. Never request elevated
       scheduled tasks, and never rebuild startup registration during app launch. */
    const wchar_t *runKey=L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    HKEY key=NULL;
    LONG status=enable?RegCreateKeyExW(HKEY_CURRENT_USER,runKey,0,NULL,0,KEY_QUERY_VALUE|KEY_SET_VALUE,NULL,&key,NULL):
        RegOpenKeyExW(HKEY_CURRENT_USER,runKey,0,KEY_QUERY_VALUE|KEY_SET_VALUE,&key);
    if(!enable && status==ERROR_FILE_NOT_FOUND)status=ERROR_SUCCESS;
    if(status!=ERROR_SUCCESS) { Config_Error((DWORD)status); return FALSE; }

    BYTE *previous=NULL;DWORD previousType=0,previousSize=0;int existed=0;
    if(key) {
        status=RegQueryValueExW(key,L"ShortCutter",NULL,&previousType,NULL,&previousSize);
        if(status==ERROR_SUCCESS) {
            existed=1;
            previous=HeapAlloc(GetProcessHeap(),0,previousSize?previousSize:1);
            if(!previous)status=ERROR_NOT_ENOUGH_MEMORY;
            else status=RegQueryValueExW(key,L"ShortCutter",NULL,&previousType,previous,&previousSize);
        }else if(status==ERROR_FILE_NOT_FOUND)status=ERROR_SUCCESS;
        if(status!=ERROR_SUCCESS) {
            if(previous)HeapFree(GetProcessHeap(),0,previous);
            RegCloseKey(key);Config_Error((DWORD)status);return FALSE;
        }
    }
    if(enable) {
        wchar_t executable[SC_PATH_CAP];
        DWORD length=GetModuleFileNameW(NULL,executable,SC_PATH_CAP);
        wchar_t quoted[SC_PATH_CAP+4];
        if(!length || length>=SC_PATH_CAP || swprintf(quoted,SC_PATH_CAP+4,L"\"%ls\"",executable)<0) status=ERROR_FILENAME_EXCED_RANGE;
        else status=RegSetValueExW(key,L"ShortCutter",0,REG_SZ,(const BYTE*)quoted,(DWORD)((wcslen(quoted)+1)*sizeof(wchar_t)));
    } else if(key) {
        status=RegDeleteValueW(key,L"ShortCutter");
        if(status==ERROR_FILE_NOT_FOUND) status=ERROR_SUCCESS;
    }
    /* Do not remove the old startup mechanism until the new value succeeded.
       A denied migration restores the exact previous value and its type. */
    if(status==ERROR_SUCCESS) {
        DWORD legacyError=Config_RemoveLegacyStartupTask();
        if(legacyError!=ERROR_SUCCESS) {
            LONG restored=key?(existed?RegSetValueExW(key,L"ShortCutter",0,previousType,previous,previousSize):
                RegDeleteValueW(key,L"ShortCutter")):ERROR_SUCCESS;
            if(restored==ERROR_FILE_NOT_FOUND)restored=ERROR_SUCCESS;
            status=restored==ERROR_SUCCESS?(LONG)legacyError:restored;
        }
    }
    if(previous)HeapFree(GetProcessHeap(),0,previous);
    if(key)RegCloseKey(key);
    if(status!=ERROR_SUCCESS) { Config_Error((DWORD)status); return FALSE; }
    g_autoStartApplied=enable;
    g_cfg.autoStart=enable;
    return TRUE;
}
