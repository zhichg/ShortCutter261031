/* Exercise the real scan dialog using the existing isolated settings fixture. */
#define main SettingsFixtureMain
#include "settings_resilience.c"
#undef main

static void Search(const wchar_t *query){SetDlgItemTextW(g_hScanDlg,5021,query);}
static void Source(int source)
{
    SendMessageW(g_hScanCbo,CB_SETCURSEL,source,0);
    SendMessageW(g_hScanDlg,WM_COMMAND,MAKEWPARAM(5020,CBN_SELCHANGE),(LPARAM)g_hScanCbo);
}
static int WriteShortcut(const wchar_t *path,const wchar_t *target,const wchar_t *arguments,const wchar_t *workingDirectory)
{
    IShellLinkW *link=NULL;IPersistFile *file=NULL;int savedLink=0;
    if(FAILED(CoCreateInstance(&CLSID_ShellLink,NULL,CLSCTX_INPROC_SERVER,&IID_IShellLinkW,(void**)&link)))return 0;
    if(SUCCEEDED(link->lpVtbl->SetPath(link,target)) &&
       SUCCEEDED(link->lpVtbl->SetArguments(link,arguments)) &&
       SUCCEEDED(link->lpVtbl->SetWorkingDirectory(link,workingDirectory)) &&
       SUCCEEDED(link->lpVtbl->SetShowCmd(link,SW_SHOWNORMAL)) &&
       SUCCEEDED(link->lpVtbl->QueryInterface(link,&IID_IPersistFile,(void**)&file)))
        savedLink=SUCCEEDED(file->lpVtbl->Save(file,path,TRUE));
    if(file)file->lpVtbl->Release(file);
    link->lpVtbl->Release(link);return savedLink;
}
static int shortcutChoice,shortcutDialogSeen,shortcutCancel;
static DWORD shortcutDialogDeadline;
static BOOL CALLBACK FindShortcutDialog(HWND window,LPARAM output)
{
    wchar_t title[80];GetWindowTextW(window,title,80);
    if(!wcscmp(title,L"Application shortcuts") || !wcscmp(title,L"应用快捷方式")){*(HWND*)output=window;return FALSE;}
    return TRUE;
}
static void CALLBACK ChooseShortcutTimer(HWND window,UINT message,UINT_PTR timer,DWORD tick)
{
    (void)message;(void)tick;
    if(GetTickCount()-shortcutDialogDeadline>10000){fprintf(stderr,"FAIL: shortcut dialog test timed out\n");ExitProcess(1);}
    HWND popup=NULL;EnumThreadWindows(GetCurrentThreadId(),FindShortcutDialog,(LPARAM)&popup);
    if(!popup)return;
    KillTimer(window,timer);shortcutDialogSeen=1;
    Capture(popup,L"tests\\scan-shortcut-choices.bmp");
    SendMessageW(popup,TDM_CLICK_RADIO_BUTTON,1000+shortcutChoice,0);
    PostMessageW(popup,TDM_CLICK_BUTTON,shortcutCancel?IDCANCEL:IDOK,0);
}
static void TestDuplicates(void)
{
    HRESULT com=CoInitializeEx(NULL,COINIT_APARTMENTTHREADED);
    Check(SUCCEEDED(com),"shortcut fixture initializes COM");if(FAILED(com))return;
    wchar_t root[SC_PATH_CAP],desktop[SC_PATH_CAP],menu[SC_PATH_CAP],variant[SC_PATH_CAP];
    wchar_t first[SC_PATH_CAP],second[SC_PATH_CAP],third[SC_PATH_CAP],target[SC_PATH_CAP];
    swprintf(root,SC_PATH_CAP,L"%ls\\tests\\scan-dedup-%lu",g_exeDir,GetCurrentProcessId());
    swprintf(desktop,SC_PATH_CAP,L"%ls\\Desktop",root);swprintf(menu,SC_PATH_CAP,L"%ls\\Menu",root);swprintf(variant,SC_PATH_CAP,L"%ls\\Variant",root);
    CreateDirectoryW(root,NULL);CreateDirectoryW(desktop,NULL);CreateDirectoryW(menu,NULL);CreateDirectoryW(variant,NULL);
    swprintf(first,SC_PATH_CAP,L"%ls\\Duplicate App.lnk",desktop);swprintf(second,SC_PATH_CAP,L"%ls\\Duplicate App.lnk",menu);swprintf(third,SC_PATH_CAP,L"%ls\\Duplicate App.lnk",variant);
    swprintf(target,SC_PATH_CAP,L"%ls\\test-app.exe",root);
    int made=WriteShortcut(first,target,L"--profile=Main",root) && WriteShortcut(second,target,L"--profile=Main",root) && WriteShortcut(third,target,L"--profile=Other",root);
    Check(made,"fixture creates real duplicate links and a distinct argument variant inside the project");
    ScanJob *job=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof(*job));
    if(made && job){
        job->refs=1;job->importedCount=1;lstrcpyW(job->importedPaths[0],second);
        Scan_LnkDir(job,desktop,0,0);Scan_LnkDir(job,menu,0,-1);Scan_LnkDir(job,variant,0,-1);
        Check(job->count==1,"same name and target produce one app row even when arguments differ");
        Check(job->shortcutCount==3,"all three original shortcuts are retained as launch choices");
        Check(job->entries[0].sources==((1u<<0)|(1u<<3)),"merged entry retains both desktop and other source memberships");
        Check(job->entries[0].added && !lstrcmpiW(job->entries[0].shortcut,second),"an imported secondary alias marks the merged row imported and remains its representative");
        ScanEntry changed=job->entries[0];lstrcpyW(changed.name,L"Different App");Check(!Scan_SameApplication(&job->entries[0],&changed),"different names for one executable remain available");
        changed=job->entries[0];wcscat(changed.path,L".other.exe");Check(!Scan_SameApplication(&job->entries[0],&changed),"same names for different executables remain distinct");
        job->count=MAX_SCAN_ENTRIES;ScanEntry duplicate=job->entries[0];lstrcpyW(duplicate.shortcut,first);
        Scan_AddEntry(job,&duplicate);Check(!job->limited && job->count==MAX_SCAN_ENTRIES && job->shortcutCount==3,"repeated shortcut scans do not consume row or shortcut capacity");job->count=1;
        job->done=1;g_scanJob=job;ScanDlg_Complete(g_hScanDlg);job=NULL;
        Check(g_scanCount==1 && ListView_GetItemCount(g_hScanList)==1 && g_scanShortcutCount==3,"completion displays one app and transfers all original shortcuts");
        Source(1);Check(ListView_GetItemCount(g_hScanList)==1 && g_scanEntries[ScanDlg_RowToIdx(0)].added,"desktop filter retains the merged imported entry");
        Source(4);Check(ListView_GetItemCount(g_hScanList)==1,"other filter also retains the merged entry");Source(0);
        shortcutChoice=0;shortcutDialogSeen=0;
        for(int option=1;option<=g_scanShortcutCount;option++)if(!lstrcmpiW(g_scanShortcuts[option-1].path,third))shortcutChoice=option;
        shortcutDialogDeadline=GetTickCount();UINT_PTR choiceTimer=SetTimer(g_hScanDlg,77,30,ChooseShortcutTimer);
        Check(choiceTimer!=0 && shortcutChoice!=0,"launch-choice test can select the original alternate shortcut");
        if(choiceTimer && shortcutChoice){
            ListView_SetItemState(g_hScanList,0,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);
            SendMessageW(g_hScanDlg,WM_COMMAND,5013,0);KillTimer(g_hScanDlg,77);
        }
        Check(shortcutDialogSeen && !lstrcmpiW(g_scanEntries[0].shortcut,third) && !wcscmp(g_scanEntries[0].arguments,L"--profile=Other"),"Details opens a real chooser and applies the selected shortcut without changing its arguments");
        Check(!g_scanEntries[0].added && g_scanEntries[0].sources==((1u<<0)|(1u<<3)),"choosing an unimported variant enables import and retains source memberships");
        shortcutCancel=1;shortcutDialogSeen=0;shortcutDialogDeadline=GetTickCount();
        for(int option=1;option<=g_scanShortcutCount;option++)if(!lstrcmpiW(g_scanShortcuts[option-1].path,second))shortcutChoice=option;
        choiceTimer=SetTimer(g_hScanDlg,77,30,ChooseShortcutTimer);
        if(choiceTimer){SendMessageW(g_hScanDlg,WM_COMMAND,5013,0);KillTimer(g_hScanDlg,77);}
        Check(shortcutDialogSeen && !lstrcmpiW(g_scanEntries[0].shortcut,third) && !g_scanEntries[0].added,"canceling the shortcut chooser preserves the prior choice and import state");shortcutCancel=0;
        int before=g_uiDraft.commandCount;ListView_SetCheckState(g_hScanList,0,TRUE);
        Check(ScanDlg_Import(g_hScanDlg) && g_uiDraft.commandCount==before+1,"import adds only the distinct unimported launch variant");
        wchar_t imported[SC_PATH_CAP];SC_WideFromUtf8(imported,SC_PATH_CAP,g_uiDraft.commands[before].runPath);
        Check(!lstrcmpiW(imported,third),"variant import preserves the original shortcut and its arguments");
    }
    if(job)Scan_Release(job);
    DeleteFileW(first);DeleteFileW(second);DeleteFileW(third);
    RemoveDirectoryW(desktop);RemoveDirectoryW(menu);RemoveDirectoryW(variant);RemoveDirectoryW(root);
    CoUninitialize();
}
static void FillSample(void)
{
    g_scanCount=6;memset(g_scanEntries,0,sizeof(ScanEntry)*g_scanCount);
    const wchar_t *names[]={L"Steam",L"bilibili",L"Adobe Photoshop",L"网易云音乐",L"bilibili",L"AutoCAD"};
    for(int i=0;i<g_scanCount;i++){
        lstrcpyW(g_scanEntries[i].name,names[i]);
        swprintf(g_scanEntries[i].path,SC_PATH_CAP,L"D:\\Apps\\%ls.exe",names[i]);
        swprintf(g_scanEntries[i].shortcut,SC_PATH_CAP,L"D:\\Shortcuts\\%d.lnk",i);
        g_scanEntries[i].source=i==1?0:1;
    }
    g_scanEntries[0].added=1;ScanDlg_Fill();
}
static void FillPhoneticSample(void)
{
    const wchar_t *names[]={L"Word",L"WPS",L"百度网盘",L"bilibili",L"哔哩哔哩",L"QQ音乐",L"阿里云",L"长城",L"重庆工具",L"网易云音乐",L"Steam"};
    g_scanCount=(int)(sizeof(names)/sizeof(names[0]));
    memset(g_scanEntries,0,sizeof(ScanEntry)*g_scanCount);
    for(int i=0;i<g_scanCount;i++){
        lstrcpyW(g_scanEntries[i].name,names[i]);
        swprintf(g_scanEntries[i].path,SC_PATH_CAP,L"D:\\Apps\\%d.exe",i);
        swprintf(g_scanEntries[i].shortcut,SC_PATH_CAP,L"D:\\Shortcuts\\%d.lnk",i);
        g_scanEntries[i].source=i==2?0:1;
    }
    Search(L"");ScanDlg_Fill();
}
static void TestPhonetic(void)
{
    FillPhoneticSample();
    const int expected[]={6,2,3,4,7,8,5,10,9,0,1};int sorted=1;
    for(int row=0;row<g_scanCount;row++)if(ScanDlg_RowToIdx(row)!=expected[row])sorted=0;
    Check(sorted,"Chinese pinyin interleaves with English: Ali, Baidu, bilibili, Changcheng, Chongqing, QQ, Steam, Wangyi, Word, WPS");
    Search(L"BAIDU");
    Check(ListView_GetItemCount(g_hScanList)==1 && ScanDlg_RowToIdx(0)==2,"Baidu matches its full pinyin without regard to case");
    ListView_SetCheckState(g_hScanList,0,TRUE);
    Search(L"bdwp");Check(ListView_GetItemCount(g_hScanList)==1 && ScanDlg_RowToIdx(0)==2 && ListView_GetCheckState(g_hScanList,0),"pinyin initials find Baidu and retain its checkbox");
    Search(L"bai du wang pan");Check(ListView_GetItemCount(g_hScanList)==1 && ScanDlg_RowToIdx(0)==2,"pinyin search accepts spaces between syllables");
    Source(2);Check(ListView_GetItemCount(g_hScanList)==0,"source filter also applies to phonetic matches");Source(0);
    Search(L"bilibili");Check(ListView_GetItemCount(g_hScanList)==2,"pinyin search matches Chinese and English application names together");
    Search(L"wangyiyunyinyue");Check(ListView_GetItemCount(g_hScanList)==1 && ScanDlg_RowToIdx(0)==9,"music phrase resolves the polyphonic character le as yue");
    Search(L"qqyy");Check(ListView_GetItemCount(g_hScanList)==1 && ScanDlg_RowToIdx(0)==5,"initial search combines Latin letters and Chinese pinyin");
    Search(L"cqgj");Check(ListView_GetItemCount(g_hScanList)==1 && ScanDlg_RowToIdx(0)==8,"Chongqing uses its phrase pronunciation in initials");
    Search(L"changcheng");Check(ListView_GetItemCount(g_hScanList)==1 && ScanDlg_RowToIdx(0)==7,"Great Wall uses chang instead of the standalone zhang reading");
    SC_PinyinKey key;SC_PinyinBuild(&key,L"😀百度");
    Check(!wcscmp(key.full,L"😀baidu"),"unknown supplementary characters survive transliteration");
    wchar_t longName[SC_NAME_CAP];for(int i=0;i<SC_NAME_CAP-1;i++)longName[i]=L'双';longName[SC_NAME_CAP-1]=0;
    SC_PinyinBuild(&key,longName);
    Check(wcslen(key.full)==6*(SC_NAME_CAP-1) && wcslen(key.initials)==SC_NAME_CAP-1,"maximum length names retain their complete phonetic keys");
    FillPhoneticSample();
}
int main(void)
{
    g_hInst=GetModuleHandleW(NULL);SetProcessDPIAware();
    INITCOMMONCONTROLSEX controls={sizeof(controls),ICC_WIN95_CLASSES};InitCommonControlsEx(&controls);
    GetCurrentDirectoryW(SC_PATH_CAP,g_exeDir);
    Config_SetDefaults();g_cfg.autoStart=0;
    Open();ScanDlg_Show(g_hDlg);
    DWORD start=GetTickCount();
    while(g_scanJob && GetTickCount()-start<15000){Pump();Sleep(5);}
    Check(g_hScanDlg && !g_scanJob,"scan dialog completes background scanning");
    if(g_scanJob || !g_hScanDlg){if(g_hScanDlg)DestroyWindow(g_hScanDlg);Close();return 1;}
    Check(GetDlgItem(g_hScanDlg,5021)!=NULL,"scan dialog exposes a search edit control");
    int ccSwitch=0,clashVerge=0,legion=0,edge=0,uu=0,duplicateApps=0;
    for(int i=0;i<g_scanCount;i++){
        if(!lstrcmpiW(g_scanEntries[i].name,L"CC Switch"))ccSwitch++;
        if(!lstrcmpiW(g_scanEntries[i].name,L"Clash Verge"))clashVerge++;
        if(!lstrcmpiW(g_scanEntries[i].name,L"Legion Zone"))legion++;
        if(!lstrcmpiW(g_scanEntries[i].name,L"Microsoft Edge"))edge++;
        if(!wcscmp(g_scanEntries[i].name,L"UU加速器"))uu++;
        for(int j=0;j<i;j++)if(Scan_SameApplication(&g_scanEntries[i],&g_scanEntries[j]))duplicateApps++;
    }
    printf("OBSERVED live scan: CC Switch=%d Clash Verge=%d Legion Zone=%d Microsoft Edge=%d UU=%d\n",ccSwitch,clashVerge,legion,edge,uu);
    Check(!duplicateApps,"the entire real scan has no duplicate name and target pairs");
    Search(L"Legion Zone");Capture(g_hScanDlg,L"tests\\scan-legion-live.bmp");
    Search(L"Microsoft Edge");Capture(g_hScanDlg,L"tests\\scan-edge-live.bmp");Search(L"");
    TestDuplicates();FillSample();
    int sorted=1;
    for(int row=1;row<g_scanCount;row++)
        if(ScanDlg_CompareNames(ScanDlg_RowToIdx(row-1),ScanDlg_RowToIdx(row),0)>0)sorted=0;
    Check(sorted && ScanDlg_RowToIdx(0)==2 && ScanDlg_RowToIdx(1)==5,"names sort ascending with stable underlying entry IDs");
    Check(ScanDlg_RowToIdx(2)==1 && ScanDlg_RowToIdx(3)==4,"duplicate names use a deterministic shortcut tie break");
    ListView_SetCheckState(g_hScanList,0,TRUE);
    Search(L"BILIBILI");
    Check(ListView_GetItemCount(g_hScanList)==2 && ScanDlg_RowToIdx(0)==1,"name search ignores case and keeps duplicate entries");
    Source(1);
    Check(ListView_GetItemCount(g_hScanList)==1 && ScanDlg_RowToIdx(0)==1,"search combines with the source filter");
    SendMessageW(g_hScanDlg,WM_COMMAND,5012,0);
    Check(g_scanEntries[1].selected && !g_scanEntries[4].selected && g_scanEntries[2].selected,"toggle all affects visible rows and retains hidden selections");
    Source(0);Search(L"网易");
    Check(ListView_GetItemCount(g_hScanList)==1 && ScanDlg_RowToIdx(0)==3,"search accepts Chinese names");
    Search(L"APPS\\AUTOCAD");
    Check(ListView_GetItemCount(g_hScanList)==1 && ScanDlg_RowToIdx(0)==5,"target path search ignores case");
    Search(L"no-such-entry");Check(ListView_GetItemCount(g_hScanList)==0,"unmatched search shows an empty result list");
    Search(L"");
    Check(ListView_GetItemCount(g_hScanList)==6 && ListView_GetCheckState(g_hScanList,0) && ListView_GetCheckState(g_hScanList,2),"clearing search restores sorted results and checkbox state");
    Search(L"Steam");ListView_SetCheckState(g_hScanList,0,TRUE);
    Check(!g_scanEntries[0].selected && !ListView_GetCheckState(g_hScanList,0),"already imported entries cannot be checked after searching");
    Search(L"bilibili");int before=g_uiDraft.commandCount;
    Check(ScanDlg_Import(g_hScanDlg) && g_uiDraft.commandCount==before+2 &&
        !strcmp(g_uiDraft.commands[before].runPath,"D:\\Shortcuts\\1.lnk") &&
        !strcmp(g_uiDraft.commands[before+1].runPath,"D:\\Shortcuts\\2.lnk"),"import preserves visible and hidden selections and their original shortcuts");
    TestPhonetic();SetDlgItemTextW(g_hScanDlg,5010,L"Found 11 entries; shortcuts keep their arguments");
    Capture(g_hScanDlg,L"tests\\scan-search-en.bmp");
    Search(L"pending query");DestroyWindow(g_hScanDlg);
    g_uiDraft.uiLanguage=UI_LANG_CHINESE;g_dpiScale=144;ScanDlg_Show(g_hDlg);
    Check(!g_scanQuery[0] && GetWindowTextLengthW(GetDlgItem(g_hScanDlg,5021))==0,"reopening the scan dialog clears the previous search");
    start=GetTickCount();while(g_scanJob && GetTickCount()-start<15000){Pump();Sleep(5);}
    if(g_scanJob){DestroyWindow(g_hScanDlg);Close();return 1;}
    FillPhoneticSample();SetDlgItemTextW(g_hScanDlg,5010,L"找到 11 个入口，保留快捷方式参数");
    wchar_t cue[80]=L"";
    SendDlgItemMessageW(g_hScanDlg,5021,EM_GETCUEBANNER,(WPARAM)cue,80);
    Check(!wcscmp(cue,L"搜索名称、拼音或目标路径"),"search placeholder follows the Chinese interface language");
    RECT combo,edit,list;GetWindowRect(g_hScanCbo,&combo);GetWindowRect(GetDlgItem(g_hScanDlg,5021),&edit);GetWindowRect(g_hScanList,&list);
    Check(edit.left>combo.right && edit.right<=list.right && edit.bottom<list.top,"search fits beside the source filter at 150 percent scaling");
    Capture(g_hScanDlg,L"tests\\scan-search-144.bmp");
    DestroyWindow(g_hScanDlg);Close();
    printf("RESULT checks=%d failures=%d\n",checks,failures);return failures?1:0;
}
