typedef struct { const wchar_t *titleEn,*titleZh,*textEn,*textZh; } UI_HelpTopic;
static const UI_HelpTopic g_helpTopics[]={
 {L"Getting started",L"快速开始",
  L"ShortCutter 4.1.0\r\n\r\nOpen Dashboard from the tray or a gesture/hotkey binding. Type a command name, then press Enter or Space. Tab completes the first result; arrows select a result.\r\n\r\nSettings changes stay in a draft until Save & Apply. Cancel or close discards unapplied changes. Language changes preview inside Settings. Pause cancels unfinished gesture and key actions.",
  L"ShortCutter 4.1.0\r\n\r\n从托盘、手势或热键打开仪表盘。输入名称，用回车或空格执行；Tab 补全首个结果，上下方向键切换候选。\r\n\r\n设置修改先保留为草稿，点击“保存并应用”后生效。取消或关闭窗口会放弃未应用的修改。语言切换先在设置中预览。暂停会取消未完成的手势和按键动作。"},
 {L"Commands and applications",L"添加命令与应用",
  L"Commands → New: choose Run, Keys or Script. Run supports arguments, a working folder, optional existing-window activation, confirmation and elevation. Documents, folders, URLs and shortcuts always open normally.\r\n\r\nScan Apps reads desktop and Start Menu shortcuts in the background. Imported .lnk entries keep their original arguments and working folders. Search filters the list; selecting a row shows its full target. Built-in and synchronized script commands have a source label. Deleting a custom command disables its bindings.",
  L"命令 → 新建：选择运行、按键或脚本。运行支持参数、工作目录、激活已有应用窗口、执行前确认和管理员权限。文档、目录、网址及快捷方式始终正常打开。\r\n\r\n“扫描应用”在后台读取桌面与开始菜单的快捷方式。导入 .lnk 会保留原有参数和工作目录。搜索可筛选列表，选中一行可查看完整目标。来源列区分内置、脚本目录和自定义命令。删除自定义命令会停用关联绑定。"},
 {L"Dashboard and quick input",L"仪表盘与快捷输入",
  L"Hold Ctrl and select several shortcuts. Release Ctrl to open them in selection order; click a selected command again to remove it. Escape cancels the queue.\r\n\r\nF6 switches between search and the shortcut grid. Arrow keys move in the grid; Enter opens the selected shortcut. Ctrl+Enter queues a command. The wheel scrolls rows on small screens and changes pages otherwise. Compact layout reduces icon size.\r\n\r\nQuick input is optional: enable Desktop and/or File Explorer. Only A–Z keys trigger it, with no Ctrl/Alt/Win held. Explorer text fields, rename boxes and dialogs keep normal input. Dashboard search bypasses IME and uses English letters and digits with every keyboard layout. Ctrl+A/C/X/V/Z and Shift+arrows edit the search text.",
  L"按住 Ctrl 点选多个快捷方式，松开 Ctrl 后按选中顺序打开；再次点选同一命令可移出队列。Esc 取消队列。\r\n\r\nF6 在搜索框与快捷方式网格之间切换。方向键移动，回车打开选中项；Ctrl+回车加入队列。小屏幕下滚轮滚动网格行，其余情况下切换分页。紧凑布局使用较小图标。\r\n\r\n快捷输入可分别在桌面、资源管理器启用。只有 A–Z 键触发，按住 Ctrl、Alt 或 Win 时不触发。资源管理器的输入框、重命名和对话框保留正常输入。仪表盘搜索禁用输入法，各键盘布局均直接输入英文字母和数字，支持数字小键盘；支持 Ctrl+A/C/X/V/Z 和 Shift+方向键编辑。"},
 {L"Recording gestures",L"录制手势",
  L"Gestures → Record: right-drag inside the canvas, up to three samples. Choose a name, modifier, screen zone and command from the selector. Clear starts over. The direction chain uses arrows.\r\n\r\nGeneral controls the trigger button, minimum gesture size and match threshold (50–95%). Use similar samples. Smaller thresholds tolerate more variation. Left and right Win keys both work. Zones use the virtual desktop, including monitors to the left or above the primary monitor. The starting point determines the zone. Main and corner bindings only fire in their own zones; choose All areas to allow any starting zone. Equal scores favor the specific zone binding.",
  L"手势 → 录制：在画布内按住鼠标右键绘制，最多保存三个采样。填写名称，选择修饰键、屏幕区域和命令。“清空”后可重新录制，方向链以箭头显示。\r\n\r\n常规页可设置触发鼠标键、最小手势尺寸和识别阈值（50–95%）。建议三个采样尽量相似；阈值越低，容许的形状差异越大。左右 Win 键均可使用。分区采用整个虚拟桌面，支持主屏左方或上方的显示器。区域由起笔位置决定；主区域及四角手势只在各自区域触发，选择“全部区域”后可在任意区域起笔。相似度相同时，优先匹配区域专用手势。"},
 {L"Hotkeys and key macros",L"热键与按键序列",
  L"Hotkeys → New: focus the key button and press a shortcut, then choose a command. Enabled duplicate bindings and conflicts with the pause shortcut are rejected. Right-click the key button to clear it.\r\n\r\nCommands → New → Keys: click Start recording, press your shortcut or key sequence, then click Stop recording with the mouse. The read-only preview shows presses (↓), releases (↑) and timing. Recording again replaces the sequence; stopping without any keys keeps the previous one. Clear removes the sequence.\r\n\r\nRecording captures Tab, Enter, Escape and modifier shortcuts without running them. Switching windows stops recording. Release any keys held before starting, then press them again to capture them. Auto-repeat is ignored. A 60-second timing or storage limit stops recording and keeps the captured keys. Existing key macros remain compatible. Playback delays do not block input handling; a focus change cancels playback and releases synthetic modifiers.",
  L"热键 → 新建：在按键按钮上按下快捷键，然后选择命令。已启用的重复绑定以及与暂停快捷键的冲突会被拒绝。右键点击按键按钮可清除。\r\n\r\n命令 → 新建 → 按键：点击“开始录制”，按下所需的快捷键或连续按键，最后用鼠标点击“停止录制”。只读预览显示按下（↓）、松开（↑）和间隔。重新录制会替换序列；未录入任何按键就停止会保留原序列。“清空”移除当前序列。\r\n\r\n录制会捕获 Tab、Enter、Esc 和修饰键组合，不触发这些快捷键；切换窗口会自动停止。开始前已按住的按键需先松开再重新按下，长按自动重复会被忽略。累计间隔达到 60 秒或存储容量上限时自动停止并保留已录内容。已有按键命令仍然兼容。播放延迟期间程序保持响应；目标焦点变化时会取消播放并释放模拟按下的修饰键。"},
 {L"Saving and troubleshooting",L"保存与故障排查",
  L"Configuration is stored next to ShortCutter.exe. Save writes a temporary file, then replaces the configuration and keeps a .bak backup. A failed save reports an error and restores unapplied changes. Names and multiline macros support UTF-8.\r\n\r\nIf a command fails, check its target, arguments and folder. Enable administrator permission only for commands that need it. Normal startup does not request elevation. Start with Windows is changed only when you Apply a different startup setting.\r\n\r\nTray status reports paused or unavailable input hooks. Errors are recorded in ShortCutter.log beside the program. Explorer taskbar restarts restore the tray icon. Back up the INI and scripts folder before moving the application.",
  L"配置保存在 ShortCutter.exe 同目录。保存时先写临时文件，再替换配置并保留 .bak 备份。保存失败会提示原因，并还原未成功应用的变动。名称及多行按键序列使用 UTF-8 保存。\r\n\r\n命令启动失败时检查目标、参数和工作目录。只为确实需要权限的命令启用管理员选项；正常启动使用普通权限。只有保存不同的开机启动设置时，才更新系统启动项。\r\n\r\n托盘状态会显示暂停或输入监听不可用。错误记录在程序目录的 ShortCutter.log。资源管理器重建任务栏后会恢复托盘图标。移动程序前请备份 INI 和 scripts 文件夹。"}
};
static HWND g_helpList,g_helpContent,g_helpSearch;
static void Help_Fill(void)
{
    wchar_t query[128];GetWindowTextW(g_helpSearch,query,128);SendMessageW(g_helpList,LB_RESETCONTENT,0,0);
    for(int i=0;i<6;i++){const UI_HelpTopic *t=&g_helpTopics[i];const wchar_t *title=SC_Text(t->titleEn,t->titleZh),*body=SC_Text(t->textEn,t->textZh);if(!query[0] || UI_Contains(title,query) || UI_Contains(body,query)){int row=(int)SendMessageW(g_helpList,LB_ADDSTRING,0,(LPARAM)title);SendMessageW(g_helpList,LB_SETITEMDATA,row,i);}}
    SendMessageW(g_helpList,LB_SETCURSEL,0,0);
    int topic=(int)SendMessageW(g_helpList,LB_GETITEMDATA,0,0);
    SetWindowTextW(g_helpContent,topic>=0?SC_Text(g_helpTopics[topic].textEn,g_helpTopics[topic].textZh):SC_Text(L"No matching help topic",L"未找到相关帮助"));
}
static LRESULT CALLBACK HelpDlgProc(HWND hWnd,UINT msg,WPARAM wp,LPARAM lp)
{
    switch(msg){
    case WM_CREATE:{HFONT font=UI_CreateFont();SetWindowLongPtrW(hWnd,GWLP_USERDATA,(LONG_PTR)font);
        g_helpSearch=Cmd_Field(hWnd,L"EDIT",L"",5400,14,14,548,24,WS_TABSTOP|ES_AUTOHSCROLL);
        SendMessageW(g_helpSearch,EM_SETCUEBANNER,TRUE,(LPARAM)SC_Text(L"Search help",L"搜索帮助"));
        g_helpList=Cmd_Field(hWnd,L"LISTBOX",L"",5401,14,50,142,312,WS_TABSTOP|WS_VSCROLL|LBS_NOTIFY|LBS_NOINTEGRALHEIGHT|WS_BORDER);
        g_helpContent=Cmd_Field(hWnd,L"EDIT",L"",5402,168,50,394,312,WS_TABSTOP|WS_VSCROLL|ES_MULTILINE|ES_AUTOVSCROLL|ES_READONLY);
        Cmd_Field(hWnd,L"BUTTON",SC_Text(L"Close",L"关闭"),102,470,376,92,28,WS_TABSTOP);EnumChildWindows(hWnd,UI_SetFont,(LPARAM)font);Help_Fill();return 0;}
    case WM_COMMAND:
        if(LOWORD(wp)==5400 && HIWORD(wp)==EN_CHANGE){Help_Fill();return 0;}
        if(LOWORD(wp)==5401 && HIWORD(wp)==LBN_SELCHANGE){int row=(int)SendMessageW(g_helpList,LB_GETCURSEL,0,0),i=(int)SendMessageW(g_helpList,LB_GETITEMDATA,row,0);if(i>=0)SetWindowTextW(g_helpContent,SC_Text(g_helpTopics[i].textEn,g_helpTopics[i].textZh));return 0;}
        if(LOWORD(wp)==102){DestroyWindow(hWnd);return 0;}break;
    case WM_DPICHANGED:UI_ScaleDialog(hWnd,wp,lp);return 0;
    case WM_CLOSE:DestroyWindow(hWnd);return 0;
    case WM_DESTROY:DeleteObject((HFONT)GetWindowLongPtrW(hWnd,GWLP_USERDATA));UI_EndEditor(hWnd);g_hHelpDlg=NULL;return 0;
    }
    return DefWindowProcW(hWnd,msg,wp,lp);
}
