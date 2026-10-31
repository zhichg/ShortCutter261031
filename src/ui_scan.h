/* Background scanning reads shortcuts only; importing preserves the .lnk. */
#include "pinyin.h"
#define MAX_SCAN_ENTRIES 300
typedef struct {
    wchar_t name[SC_NAME_CAP],path[SC_PATH_CAP],shortcut[SC_PATH_CAP];
    wchar_t arguments[SC_PATH_CAP],workingDirectory[SC_PATH_CAP];
    int source,added,selected,detailsComplete,showCommand,firstShortcut;
    unsigned sources;DWORD launchFlags;
} ScanEntry;
typedef struct { wchar_t path[SC_PATH_CAP];int next; } ScanShortcut;
typedef struct {
    volatile LONG refs,cancel,done;int count,limited,importedCount;DWORD error;
    ScanShortcut *shortcuts;int shortcutCount,shortcutCapacity;
    wchar_t importedPaths[MAX_COMMANDS][SC_PATH_CAP];
    ScanEntry entries[MAX_SCAN_ENTRIES];
} ScanJob;
static ScanEntry g_scanEntries[MAX_SCAN_ENTRIES];
static ScanShortcut *g_scanShortcuts;
static int g_scanShortcutCount;
static SC_PinyinKey g_scanNameKeys[MAX_SCAN_ENTRIES];
static int g_scanCount,g_scanFilter,g_scanFilling;
static wchar_t g_scanQuery[SC_PATH_CAP];
static HWND g_hScanDlg,g_hScanList,g_hScanCbo;
static ScanJob *g_scanJob;
static void Scan_Release(ScanJob *job)
{if(InterlockedDecrement(&job->refs)==0){if(job->shortcuts)HeapFree(GetProcessHeap(),0,job->shortcuts);HeapFree(GetProcessHeap(),0,job);}}
static int Scan_FilterOut(const wchar_t *path)
{
    const wchar_t *ext=wcsrchr(path,L'.');
    return !ext || lstrcmpiW(ext,L".exe") || UI_Contains(path,L"\\Windows\\") || UI_Contains(path,L"unins") || UI_Contains(path,L"卸载");
}
static int Scan_PathsEqual(const wchar_t *a,const wchar_t *b)
{
    if(!lstrcmpiW(a,b))return 1;
    if(!*a || !*b)return 0;
    wchar_t x[SC_PATH_CAP],y[SC_PATH_CAP];
    DWORD nx=ExpandEnvironmentStringsW(a,x,SC_PATH_CAP),ny=ExpandEnvironmentStringsW(b,y,SC_PATH_CAP);
    if(!nx || nx>SC_PATH_CAP || !ny || ny>SC_PATH_CAP)return 0;
    size_t length=wcslen(x);while(length>3 && (x[length-1]==L'\\' || x[length-1]==L'/'))x[--length]=0;
    length=wcslen(y);while(length>3 && (y[length-1]==L'\\' || y[length-1]==L'/'))y[--length]=0;
    return !lstrcmpiW(x,y);
}
static int Scan_SameApplication(const ScanEntry *a,const ScanEntry *b)
{
    return !lstrcmpiW(a->name,b->name) && Scan_PathsEqual(a->path,b->path);
}
static int Scan_ReadShortcut(const wchar_t *path,ScanEntry *entry)
{
    IShellLinkW *link=NULL;IPersistFile *file=NULL;int loaded=0;
    if(FAILED(CoCreateInstance(&CLSID_ShellLink,NULL,CLSCTX_INPROC_SERVER,&IID_IShellLinkW,(void**)&link)))return 0;
    if(SUCCEEDED(link->lpVtbl->QueryInterface(link,&IID_IPersistFile,(void**)&file))){
        if(SUCCEEDED(file->lpVtbl->Load(file,path,STGM_READ)))
            loaded=SUCCEEDED(link->lpVtbl->GetPath(link,entry->path,SC_PATH_CAP,NULL,SLGP_RAWPATH));
        file->lpVtbl->Release(file);
    }
    if(loaded){
        HRESULT arguments=link->lpVtbl->GetArguments(link,entry->arguments,SC_PATH_CAP);
        HRESULT directory=link->lpVtbl->GetWorkingDirectory(link,entry->workingDirectory,SC_PATH_CAP);
        HRESULT show=link->lpVtbl->GetShowCmd(link,&entry->showCommand);
        entry->detailsComplete=SUCCEEDED(arguments) && SUCCEEDED(directory) && SUCCEEDED(show);
        /* Incomplete details are labeled; the original shortcut is still kept. */
        if(wcslen(entry->path)==SC_PATH_CAP-1 || wcslen(entry->arguments)==SC_PATH_CAP-1 ||
           wcslen(entry->workingDirectory)==SC_PATH_CAP-1)entry->detailsComplete=0;
        IShellLinkDataList *data=NULL;DWORD flags=0;
        if(SUCCEEDED(link->lpVtbl->QueryInterface(link,&IID_IShellLinkDataList,(void**)&data))){
            if(FAILED(data->lpVtbl->GetFlags(data,&flags)))entry->detailsComplete=0;
            data->lpVtbl->Release(data);
        }else entry->detailsComplete=0;
        entry->launchFlags=flags&(SLDF_RUNAS_USER|SLDF_RUN_IN_SEPARATE);
        if(flags&(SLDF_HAS_DARWINID|SLDF_RUN_WITH_SHIMLAYER))entry->detailsComplete=0;
    }
    link->lpVtbl->Release(link);return loaded;
}
static int Scan_RecordShortcut(ScanJob *job,ScanEntry *entry,const wchar_t *path)
{
    for(int link=entry->firstShortcut;link;link=job->shortcuts[link-1].next)
        if(!lstrcmpiW(job->shortcuts[link-1].path,path))return 1;
    if(job->shortcutCount==job->shortcutCapacity){
        int capacity=job->shortcutCapacity?job->shortcutCapacity*2:32;
        if(job->shortcutCapacity>32768){job->error=ERROR_NOT_ENOUGH_MEMORY;job->cancel=1;return 0;}
        ScanShortcut *grown=job->shortcuts?HeapReAlloc(GetProcessHeap(),0,job->shortcuts,sizeof(*grown)*(size_t)capacity):HeapAlloc(GetProcessHeap(),0,sizeof(*grown)*(size_t)capacity);
        if(!grown){job->error=ERROR_NOT_ENOUGH_MEMORY;job->cancel=1;return 0;}
        job->shortcuts=grown;job->shortcutCapacity=capacity;
    }
    ScanShortcut *shortcut=&job->shortcuts[job->shortcutCount];
    lstrcpyW(shortcut->path,path);shortcut->next=entry->firstShortcut;
    entry->firstShortcut=++job->shortcutCount;return 1;
}
static void Scan_AddEntry(ScanJob *job,ScanEntry *entry)
{
    entry->sources=1u<<entry->source;
    for(int i=0;i<job->importedCount;i++)if(!lstrcmpiW(job->importedPaths[i],entry->shortcut)){entry->added=1;break;}
    for(int i=0;i<job->count;i++){
        ScanEntry *existing=&job->entries[i];
        if(!lstrcmpiW(existing->shortcut,entry->shortcut) || Scan_SameApplication(existing,entry)){
            if(!Scan_RecordShortcut(job,existing,entry->shortcut))return;
            unsigned sources=existing->sources|entry->sources;
            /* Keep an already imported alias as the representative shortcut. */
            if(entry->added && !existing->added){int first=existing->firstShortcut;*existing=*entry;existing->firstShortcut=first;}
            existing->sources=sources;existing->added|=entry->added;return;
        }
    }
    if(job->count==MAX_SCAN_ENTRIES){job->limited=1;return;}
    entry->firstShortcut=0;
    if(Scan_RecordShortcut(job,entry,entry->shortcut))job->entries[job->count++]=*entry;
}
static void Scan_LnkDir(ScanJob *job,const wchar_t *dir,int depth,int source)
{
    if(depth>3 || job->cancel)return;
    wchar_t mask[SC_PATH_CAP];if(swprintf(mask,SC_PATH_CAP,L"%ls\\*",dir)<0)return;
    WIN32_FIND_DATAW data;HANDLE find=FindFirstFileW(mask,&data);if(find==INVALID_HANDLE_VALUE)return;
    do {
        if(job->cancel)break;
        if(data.cFileName[0]==L'.')continue;
        wchar_t full[SC_PATH_CAP];if(swprintf(full,SC_PATH_CAP,L"%ls\\%ls",dir,data.cFileName)<0)continue;
        if(data.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY){if(!(data.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT))Scan_LnkDir(job,full,depth+1,source);continue;}
        const wchar_t *ext=wcsrchr(data.cFileName,L'.');if(!ext || lstrcmpiW(ext,L".lnk"))continue;
        ScanEntry candidate={0};ScanEntry *entry=&candidate;
        if(!Scan_ReadShortcut(full,entry) || Scan_FilterOut(entry->path))continue;
        size_t n=(size_t)(ext-data.cFileName);if(n>=SC_NAME_CAP)n=SC_NAME_CAP-1;
        wmemcpy(entry->name,data.cFileName,n);entry->name[n]=0;
        lstrcpyW(entry->shortcut,full);
        entry->source=source==0?0:UI_Contains(entry->path,L"\\Program Files (x86)\\")?2:UI_Contains(entry->path,L"\\Program Files\\")?1:3;
        Scan_AddEntry(job,entry);
    }while(FindNextFileW(find,&data));FindClose(find);
}
static DWORD WINAPI Scan_Worker(void *arg)
{
    ScanJob *job=arg;HRESULT hr=CoInitializeEx(NULL,COINIT_APARTMENTTHREADED);
    if(SUCCEEDED(hr)) {
        const int folders[]={CSIDL_DESKTOPDIRECTORY,CSIDL_COMMON_DESKTOPDIRECTORY,CSIDL_PROGRAMS,CSIDL_COMMON_PROGRAMS};
        for(int i=0;i<4 && !job->cancel;i++){wchar_t path[MAX_PATH];if(SUCCEEDED(SHGetFolderPathW(NULL,folders[i],NULL,0,path)))Scan_LnkDir(job,path,0,i<2?0:-1);}
        CoUninitialize();
    }else job->error=(DWORD)hr;
    InterlockedExchange(&job->done,1);Scan_Release(job);return 0;
}
static int ScanDlg_RowToIdx(int row){return UI_ListData(g_hScanList,row);}
static const SC_PinyinKey *ScanDlg_NameKey(int index)
{
    SC_PinyinKey *key=&g_scanNameKeys[index];
    if(wcscmp(key->name,g_scanEntries[index].name))SC_PinyinBuild(key,g_scanEntries[index].name);
    return key;
}
static int CALLBACK ScanDlg_CompareNames(LPARAM left,LPARAM right,LPARAM context)
{
    (void)context;
    ScanEntry *a=&g_scanEntries[left],*b=&g_scanEntries[right];
    int result=lstrcmpiW(ScanDlg_NameKey((int)left)->full,ScanDlg_NameKey((int)right)->full);
    if(!result)result=lstrcmpiW(a->name,b->name);
    if(!result)result=lstrcmpiW(a->shortcut,b->shortcut);
    return result?result:(int)left-(int)right;
}
static void ScanDlg_Fill(void)
{
    wchar_t phonetic[SC_PATH_CAP];size_t used=0;
    for(const wchar_t *p=g_scanQuery;*p && used+1<SC_PATH_CAP;p++){
        if(*p==L' ' || *p==L'\'')continue;
        phonetic[used++]=(*p==L'ü' || *p==L'Ü')?L'v':*p;
    }
    phonetic[used]=0;
    g_scanFilling=1;SendMessageW(g_hScanList,WM_SETREDRAW,FALSE,0);ListView_DeleteAllItems(g_hScanList);
    for(int i=0;i<g_scanCount;i++){
        ScanEntry *e=&g_scanEntries[i];unsigned sources=e->sources?e->sources:1u<<e->source;
        if(g_scanFilter && !(sources&(1u<<(g_scanFilter-1))))continue;
        const SC_PinyinKey *key=ScanDlg_NameKey(i);
        if(!UI_Contains(e->name,g_scanQuery) && !UI_Contains(e->path,g_scanQuery) &&
           !(phonetic[0] && (UI_Contains(key->full,phonetic) || UI_Contains(key->initials,phonetic))))continue;
        LVITEMW item={0};item.mask=LVIF_TEXT|LVIF_PARAM;item.iItem=ListView_GetItemCount(g_hScanList);item.pszText=e->name;item.lParam=i;
        int row=(int)SendMessageW(g_hScanList,LVM_INSERTITEMW,0,(LPARAM)&item);
        item.mask=LVIF_TEXT;item.iItem=row;item.iSubItem=1;item.pszText=e->path;SendMessageW(g_hScanList,LVM_SETITEMW,0,(LPARAM)&item);
        ListView_SetCheckState(g_hScanList,row,e->selected && !e->added);
    }
    ListView_SortItems(g_hScanList,ScanDlg_CompareNames,0);
    SendMessageW(g_hScanList,WM_SETREDRAW,TRUE,0);InvalidateRect(g_hScanList,NULL,TRUE);
    g_scanFilling=0;
}
static void ScanDlg_Complete(HWND parent)
{
    ScanJob *job=g_scanJob;if(!job || !job->done)return;KillTimer(parent,21);
    g_scanCount=job->count;memcpy(g_scanEntries,job->entries,sizeof(ScanEntry)*job->count);
    if(g_scanShortcuts)HeapFree(GetProcessHeap(),0,g_scanShortcuts);
    g_scanShortcuts=job->shortcuts;g_scanShortcutCount=job->shortcutCount;job->shortcuts=NULL;
    for(int i=0;i<g_scanCount;i++)for(int j=0;j<g_cfg.commandCount;j++){
        wchar_t path[SC_PATH_CAP];if(g_cfg.commands[j].type==CMD_RUN && SC_WideFromUtf8(path,SC_PATH_CAP,g_cfg.commands[j].runPath) && !lstrcmpiW(path,g_scanEntries[i].shortcut))g_scanEntries[i].added=1;
    }
    wchar_t text[160];swprintf(text,160,job->error?SC_Text(L"Scan failed (%lu)",L"扫描失败（%lu）"):job->limited?SC_Text(L"Found %lu entries; scan capacity reached",L"找到 %lu 个入口，扫描数量已达上限"):SC_Text(L"Found %lu entries; shortcuts keep their arguments",L"找到 %lu 个入口，保留快捷方式参数"),job->error?job->error:(DWORD)job->count);
    SetDlgItemTextW(parent,5010,text);ScanDlg_Fill();EnableWindow(GetDlgItem(parent,101),TRUE);g_scanJob=NULL;Scan_Release(job);
}

static int Scan_NameUsed(const AppConfig *draft,const char *name)
{
    if(CmdNameToId(name))return 1;
    for(int i=0;i<draft->commandCount;i++)
        if(!lstrcmpiA(draft->commands[i].name,name))return 1;
    return 0;
}

static int ScanDlg_Import(HWND parent)
{
    int needed=0;
    for(int i=0;i<g_scanCount;i++)
        if(g_scanEntries[i].selected && !g_scanEntries[i].added)needed++;
    if(!needed)return 1;
    if(needed>MAX_COMMANDS-g_cfg.commandCount) {
        wchar_t text[128];
        swprintf(text,128,SC_Text(L"Only %d command slots remain. Select fewer entries.",L"还可添加 %d 个命令，请减少选中项。"),MAX_COMMANDS-g_cfg.commandCount);
        MessageBoxW(parent,text,L"ShortCutter",MB_OK|MB_ICONWARNING);return 0;
    }
    /* Validate names, paths and IDs for the whole selection before committing. */
    AppConfig *draft=HeapAlloc(GetProcessHeap(),0,sizeof(*draft));
    if(!draft) {
        MessageBoxW(parent,SC_Text(L"Not enough memory to import.",L"内存不足，无法导入。"),L"ShortCutter",MB_OK|MB_ICONWARNING);return 0;
    }
    *draft=g_cfg;
    const wchar_t *error=NULL;
    for(int i=0;i<g_scanCount && !error;i++) {
        ScanEntry *entry=&g_scanEntries[i];
        if(!entry->selected || entry->added)continue;
        Command command={0};
        if(!SC_Utf8FromWide(command.name,sizeof(command.name),entry->name) ||
           !SC_Utf8FromWide(command.runPath,sizeof(command.runPath),entry->shortcut)) {
            error=SC_Text(L"A selected entry is too long or invalid.",L"某个选中入口的名称或路径过长，或含无效字符。");break;
        }
        int suffix=2;
        while(Scan_NameUsed(draft,command.name)) {
            wchar_t unique[SC_NAME_CAP];
            if(swprintf(unique,SC_NAME_CAP,L"%ls (%d)",entry->name,suffix++)<0 ||
               !SC_Utf8FromWide(command.name,sizeof(command.name),unique)) {
                error=SC_Text(L"A duplicate name is too long to distinguish.",L"重名入口的名称过长，无法添加区分后缀。");break;
            }
        }
        if(error)break;
        command.id=Config_AllocateCommandId(draft);
        if(!command.id) {error=SC_Text(L"No command IDs remain.",L"可用命令 ID 已用尽。");break;}
        command.type=CMD_RUN;SC_CopyUtf8(command.group,sizeof(command.group),"Apps");
        draft->commands[draft->commandCount++]=command;
    }
    if(!error) {
        memcpy(g_cfg.commands,draft->commands,sizeof(g_cfg.commands));
        g_cfg.commandCount=draft->commandCount;g_cfg.nextCmdId=draft->nextCmdId;
        for(int i=0;i<g_scanCount;i++)if(g_scanEntries[i].selected)g_scanEntries[i].added=1;
        UI_Dirty();PostMessageW(GetParent(parent),WM_SC_REFRESH,0,0);
    }
    HeapFree(GetProcessHeap(),0,draft);
    if(error)MessageBoxW(parent,error,L"ShortCutter",MB_OK|MB_ICONWARNING);
    return error==NULL;
}

static int ScanDlg_ChooseShortcut(int index,int option)
{
    if(index<0 || index>=g_scanCount || option<1 || option>g_scanShortcutCount)return 0;
    ScanEntry *entry=&g_scanEntries[index];int found=0;
    for(int link=entry->firstShortcut;link;link=g_scanShortcuts[link-1].next)if(link==option){found=1;break;}
    if(!found)return 0;
    ScanEntry chosen={0};HRESULT com=CoInitializeEx(NULL,COINIT_APARTMENTTHREADED);
    int loaded=Scan_ReadShortcut(g_scanShortcuts[option-1].path,&chosen);
    if(SUCCEEDED(com))CoUninitialize();
    lstrcpyW(chosen.name,entry->name);
    if(!loaded || !Scan_SameApplication(entry,&chosen))return 0;
    lstrcpyW(chosen.shortcut,g_scanShortcuts[option-1].path);
    chosen.firstShortcut=entry->firstShortcut;chosen.sources=entry->sources;chosen.source=entry->source;
    for(int i=0;i<g_cfg.commandCount;i++){
        wchar_t path[SC_PATH_CAP];
        if(g_cfg.commands[i].type==CMD_RUN && SC_WideFromUtf8(path,SC_PATH_CAP,g_cfg.commands[i].runPath) && !lstrcmpiW(path,chosen.shortcut)){chosen.added=1;break;}
    }
    chosen.selected=entry->selected && !chosen.added;*entry=chosen;return 1;
}
static void ScanDlg_Details(HWND parent,int index)
{
    ScanEntry *entry=&g_scanEntries[index];int count=0;
    for(int link=entry->firstShortcut;link;link=g_scanShortcuts[link-1].next)count++;
    if(count<2){MessageBoxW(parent,entry->shortcut,entry->name,MB_OK);return;}
    typedef HRESULT (WINAPI *TaskDialogFunction)(const TASKDIALOGCONFIG*,int*,int*,BOOL*);
    union { FARPROC address;TaskDialogFunction call; } dialog;
    dialog.address=GetProcAddress(GetModuleHandleW(L"comctl32.dll"),"TaskDialogIndirect");
    if(!dialog.address){MessageBoxW(parent,entry->shortcut,entry->name,MB_OK);return;}
    const size_t capacity=SC_PATH_CAP*3+256;
    TASKDIALOG_BUTTON *choices=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof(*choices)*(size_t)count);
    wchar_t *labels=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof(*labels)*capacity*(size_t)count);
    if(!choices || !labels){
        if(choices)HeapFree(GetProcessHeap(),0,choices);
        if(labels)HeapFree(GetProcessHeap(),0,labels);
        MessageBoxW(parent,SC_Text(L"Not enough memory to show shortcuts.",L"内存不足，无法显示快捷方式。"),L"ShortCutter",MB_OK|MB_ICONWARNING);return;
    }
    HRESULT com=CoInitializeEx(NULL,COINIT_APARTMENTTHREADED);int row=0,selected=0;
    for(int link=entry->firstShortcut;link;link=g_scanShortcuts[link-1].next,row++){
        const wchar_t *path=g_scanShortcuts[link-1].path;ScanEntry details={0};
        choices[row].nButtonID=1000+link;choices[row].pszButtonText=labels+capacity*(size_t)row;
        if(!lstrcmpiW(path,entry->shortcut))selected=choices[row].nButtonID;
        if(Scan_ReadShortcut(path,&details))
            swprintf(labels+capacity*(size_t)row,capacity,SC_Text(L"%ls\nArguments: %ls\nStart in: %ls\nWindow: %ls%ls%ls",L"%ls\n参数：%ls\n起始位置：%ls\n窗口：%ls%ls%ls"),path,
                details.arguments[0]?details.arguments:SC_Text(L"(none)",L"（无）"),
                details.workingDirectory[0]?details.workingDirectory:SC_Text(L"(default)",L"（默认）"),
                details.showCommand==SW_SHOWMAXIMIZED?SC_Text(L"Maximized",L"最大化"):details.showCommand==SW_SHOWMINIMIZED || details.showCommand==SW_SHOWMINNOACTIVE?SC_Text(L"Minimized",L"最小化"):SC_Text(L"Normal",L"正常"),
                details.launchFlags&SLDF_RUNAS_USER?SC_Text(L"\nRun as administrator",L"\n以管理员身份运行"):L"",
                details.detailsComplete?L"":SC_Text(L"\nSome settings cannot be displayed completely; the original shortcut will be preserved.",L"\n部分设置无法完整显示，导入时仍保留原快捷方式。"));
        else swprintf(labels+capacity*(size_t)row,capacity,SC_Text(L"%ls\nShortcut unavailable",L"%ls\n快捷方式不可用"),path);
    }
    if(SUCCEEDED(com))CoUninitialize();
    TASKDIALOG_BUTTON buttons[]={{IDOK,SC_Text(L"Use selected shortcut",L"使用所选快捷方式")},{IDCANCEL,SC_Text(L"Cancel",L"取消")}};
    TASKDIALOGCONFIG config={0};config.cbSize=sizeof(config);config.hwndParent=parent;
    config.dwFlags=TDF_ALLOW_DIALOG_CANCELLATION|TDF_POSITION_RELATIVE_TO_WINDOW;
    config.pszWindowTitle=SC_Text(L"Application shortcuts",L"应用快捷方式");config.pszMainInstruction=entry->name;
    config.pszContent=SC_Text(L"This application is listed once. Choose the shortcut to import; its original launch settings will be kept.",L"同名、同目标的应用只显示一次。可选择要导入的快捷方式，并保留其原始启动设置。");
    config.cButtons=2;config.pButtons=buttons;config.cRadioButtons=(UINT)count;config.pRadioButtons=choices;config.nDefaultRadioButton=selected;config.cxWidth=400;
    int button=0,radio=0;HRESULT result=dialog.call(&config,&button,&radio,NULL);
    if(FAILED(result))MessageBoxW(parent,SC_Text(L"Could not show the shortcut choices.",L"无法显示快捷方式选项。"),L"ShortCutter",MB_OK|MB_ICONWARNING);
    if(SUCCEEDED(result) && button==IDOK){
        if(!ScanDlg_ChooseShortcut(index,radio-1000))MessageBoxW(parent,SC_Text(L"The shortcut has changed or is unavailable. Scan again.",L"快捷方式已变化或不可用，请重新扫描。"),L"ShortCutter",MB_OK|MB_ICONWARNING);
        ScanDlg_Fill();
        for(int r=0;r<ListView_GetItemCount(g_hScanList);r++)if(ScanDlg_RowToIdx(r)==index){ListView_SetItemState(g_hScanList,r,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);break;}
    }
    HeapFree(GetProcessHeap(),0,choices);HeapFree(GetProcessHeap(),0,labels);
}
static LRESULT CALLBACK ScanDlgProc(HWND hWnd,UINT msg,WPARAM wp,LPARAM lp)
{
    switch(msg){
    case WM_CREATE:{
        HFONT font=UI_CreateFont();SetWindowLongPtrW(hWnd,GWLP_USERDATA,(LONG_PTR)font);
        Cmd_Field(hWnd,L"STATIC",SC_Text(L"Scanning…",L"正在扫描…"),5010,12,12,512,22,0);
        g_hScanCbo=Cmd_Field(hWnd,L"COMBOBOX",L"",5020,12,42,180,160,CBS_DROPDOWNLIST|WS_VSCROLL|WS_TABSTOP);
        const wchar_t *sources[]={SC_Text(L"All",L"全部"),SC_Text(L"Desktop",L"桌面"),L"Program Files",L"Program Files (x86)",SC_Text(L"Other",L"其他")};
        for(int i=0;i<5;i++)SendMessageW(g_hScanCbo,CB_ADDSTRING,0,(LPARAM)sources[i]);
        SendMessageW(g_hScanCbo,CB_SETCURSEL,0,0);
        HWND search=Cmd_Field(hWnd,L"EDIT",L"",5021,202,42,322,22,WS_BORDER|WS_TABSTOP|ES_AUTOHSCROLL);
        SendMessageW(search,EM_SETLIMITTEXT,SC_PATH_CAP-1,0);
        SendMessageW(search,EM_SETCUEBANNER,TRUE,(LPARAM)SC_Text(L"Search name, pinyin or target",L"搜索名称、拼音或目标路径"));
        g_hScanList=Cmd_Field(hWnd,WC_LISTVIEWW,L"",5011,12,76,512,228,LVS_REPORT|LVS_SINGLESEL|LVS_SHOWSELALWAYS|WS_TABSTOP|WS_BORDER);
        ListView_SetExtendedListViewStyle(g_hScanList,LVS_EX_CHECKBOXES|LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER|LVS_EX_LABELTIP);
        LVCOLUMNW col={0};col.mask=LVCF_TEXT|LVCF_WIDTH;col.cx=SCALE(175);col.pszText=(LPWSTR)SC_Text(L"Name",L"名称");SendMessageW(g_hScanList,LVM_INSERTCOLUMNW,0,(LPARAM)&col);
        col.cx=SCALE(312);col.pszText=(LPWSTR)SC_Text(L"Target",L"目标");SendMessageW(g_hScanList,LVM_INSERTCOLUMNW,1,(LPARAM)&col);
        Cmd_Field(hWnd,L"BUTTON",SC_Text(L"Toggle all",L"全选 / 取消"),5012,12,318,106,28,WS_TABSTOP);
        Cmd_Field(hWnd,L"BUTTON",SC_Text(L"Details",L"详情"),5013,130,318,70,28,WS_TABSTOP);
        Cmd_Field(hWnd,L"BUTTON",SC_Text(L"Import",L"导入"),101,338,318,86,28,WS_TABSTOP|BS_DEFPUSHBUTTON);
        Cmd_Field(hWnd,L"BUTTON",SC_Text(L"Cancel",L"取消"),102,436,318,86,28,WS_TABSTOP);
        EnumChildWindows(hWnd,UI_SetFont,(LPARAM)font);EnableWindow(GetDlgItem(hWnd,101),FALSE);
        g_scanJob=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof(ScanJob));
        if(g_scanJob){
            g_scanJob->refs=1;
            for(int i=0;i<g_cfg.commandCount && g_scanJob->importedCount<MAX_COMMANDS;i++)
                if(g_cfg.commands[i].type==CMD_RUN && SC_WideFromUtf8(g_scanJob->importedPaths[g_scanJob->importedCount],SC_PATH_CAP,g_cfg.commands[i].runPath))g_scanJob->importedCount++;
            if(!SetTimer(hWnd,21,120,NULL)) {
                Scan_Release(g_scanJob);g_scanJob=NULL;
                SetDlgItemTextW(hWnd,5010,SC_Text(L"Could not schedule scanning",L"无法启动扫描计时器"));
            }else{
                g_scanJob->refs=2;HANDLE worker=CreateThread(NULL,0,Scan_Worker,g_scanJob,0,NULL);
                if(worker)CloseHandle(worker);
                else{KillTimer(hWnd,21);g_scanJob->refs=1;Scan_Release(g_scanJob);g_scanJob=NULL;SetDlgItemTextW(hWnd,5010,SC_Text(L"Could not start scanning",L"无法启动扫描"));}
            }
        }
        else SetDlgItemTextW(hWnd,5010,SC_Text(L"Not enough memory to scan",L"内存不足，无法扫描"));
        return 0;
    }
    case WM_TIMER:if(wp==21){ScanDlg_Complete(hWnd);return 0;}break;
    case WM_NOTIFY:{NMHDR *hdr=(NMHDR*)lp;
        if(hdr->idFrom==5011 && hdr->code==LVN_ITEMCHANGED && !g_scanFilling){NMLISTVIEW *event=(NMLISTVIEW*)lp;int i=ScanDlg_RowToIdx(event->iItem);if(i>=0 && (event->uChanged&LVIF_STATE)){ScanEntry *e=&g_scanEntries[i];e->selected=ListView_GetCheckState(g_hScanList,event->iItem) && !e->added;if(e->added && ListView_GetCheckState(g_hScanList,event->iItem)){g_scanFilling=1;ListView_SetCheckState(g_hScanList,event->iItem,FALSE);g_scanFilling=0;}}}return 0;
    }
    case WM_COMMAND:
        if(LOWORD(wp)==5021 && HIWORD(wp)==EN_CHANGE){GetDlgItemTextW(hWnd,5021,g_scanQuery,SC_PATH_CAP);if(g_hScanList)ScanDlg_Fill();return 0;}
        if(LOWORD(wp)==5020 && HIWORD(wp)==CBN_SELCHANGE){g_scanFilter=(int)SendMessageW(g_hScanCbo,CB_GETCURSEL,0,0);ScanDlg_Fill();return 0;}
        if(LOWORD(wp)==5012){int rows=ListView_GetItemCount(g_hScanList),all=1;for(int r=0;r<rows;r++){int i=ScanDlg_RowToIdx(r);if(i>=0 && !g_scanEntries[i].added && !g_scanEntries[i].selected)all=0;}for(int r=0;r<rows;r++){int i=ScanDlg_RowToIdx(r);if(i>=0 && !g_scanEntries[i].added)ListView_SetCheckState(g_hScanList,r,!all);}return 0;}
        if(LOWORD(wp)==5013){int row=ListView_GetNextItem(g_hScanList,-1,LVNI_SELECTED),i=ScanDlg_RowToIdx(row);if(i>=0)ScanDlg_Details(hWnd,i);return 0;}
        if(LOWORD(wp)==101 && !g_scanJob){
            if(ScanDlg_Import(hWnd))DestroyWindow(hWnd);
            return 0;
        }
        if(LOWORD(wp)==102){DestroyWindow(hWnd);return 0;}break;
    case WM_DPICHANGED:UI_ScaleDialog(hWnd,wp,lp);return 0;
    case WM_CLOSE:DestroyWindow(hWnd);return 0;
    case WM_DESTROY:
        KillTimer(hWnd,21);if(g_scanJob){InterlockedExchange(&g_scanJob->cancel,1);Scan_Release(g_scanJob);g_scanJob=NULL;}
        if(g_scanShortcuts)HeapFree(GetProcessHeap(),0,g_scanShortcuts);
        g_scanShortcuts=NULL;g_scanShortcutCount=0;
        DeleteObject((HFONT)GetWindowLongPtrW(hWnd,GWLP_USERDATA));UI_EndEditor(hWnd);g_hScanDlg=g_hScanList=g_hScanCbo=NULL;return 0;
    }
    return DefWindowProcW(hWnd,msg,wp,lp);
}
static void ScanDlg_Show(HWND parent)
{
    if(g_hScanDlg){SetForegroundWindow(g_hScanDlg);return;}if(!UI_BeginEditor(parent))return;
    g_scanCount=g_scanFilter=0;g_scanQuery[0]=0;static int registered;
    if(!registered){WNDCLASSEXW wc={0};wc.cbSize=sizeof(wc);wc.lpfnWndProc=ScanDlgProc;wc.hInstance=g_hInst;wc.hCursor=LoadCursor(NULL,IDC_ARROW);wc.hbrBackground=GetSysColorBrush(COLOR_BTNFACE);wc.lpszClassName=L"SC_ScanDlg";RegisterClassExW(&wc);registered=1;}
    g_hScanDlg=CreateWindowExW(WS_EX_DLGMODALFRAME|WS_EX_CONTROLPARENT,L"SC_ScanDlg",SC_Text(L"Scan applications",L"扫描应用"),WS_POPUP|WS_CAPTION|WS_SYSMENU|WS_VISIBLE,0,0,SCALE(556),SCALE(400),parent,NULL,g_hInst,NULL);
    if(g_hScanDlg){EnableWindow(parent,FALSE);UI_CenterOwned(g_hScanDlg,parent);}
}
