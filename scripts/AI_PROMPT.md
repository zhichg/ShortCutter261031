# ShortCutter 4.0 — AI 脚本编写指南

## 工作原理

ShortCutter 启动时会扫描 `scripts\` 文件夹，**文件名（去掉扩展名）即命令名**。Commands 标签页中自动注册为 `CMD_SCRIPT` 类型的命令（Group="Scripts"，不可删除）。

```
scripts\
├── run.ps1           → 命令名 run     → 打开运行对话框
├── backup.bat         → 命令名 backup → 执行备份
├── Ping_Show.ps1     → 命令名 Ping_Show → ping 并显示窗口
└── vpn.cmd            → 命令名 vpn    → 连接 VPN
```

## 脚本规范

1. **静默执行** — ShortCutter 默认带 `-WindowStyle Hidden` / `SW_HIDE` 执行脚本（无黑框）。如需启动 GUI 程序，用 `start "" "程序名"`（bat）或 `Start-Process`（ps1）
2. **窗口可见** — 文件名以 `_Show` 结尾（如 `Ping_Show.ps1`）→ 显示控制台窗口
3. **无交互** — 不要 `pause`、`Read-Host`、弹确认框
4. **快速完成** — 避免长时间阻塞，耗时操作考虑后台启动
5. **独立自足** — 不依赖工作目录，路径用绝对路径或 `%~dp0` / `$PSScriptRoot`
6. **幂等友好** — 重复执行不造成副作用

## 改名同步

Settings → Commands → Edit → 修改 Name 会通过 `MoveFile` 同步重命名实际脚本文件，扩展名保持不变。命令 ID 和绑定关系不受影响。

## 支持的脚本类型

| 扩展名 | 执行方式 | 默认窗口 |
|--------|----------|:---:|
| `.bat` | `ShellExecuteW` (open) | 隐藏 |
| `.cmd` | `ShellExecuteW` (open) | 隐藏 |
| `.ps1` | `powershell.exe -WindowStyle Hidden -ExecutionPolicy Bypass -File "..."` | 隐藏 |
| `.vbs` | `ShellExecuteW` (open) | 隐藏 |

> 文件名以 `_Show` 结尾（扩展名之前）→ 跳过 `-WindowStyle Hidden`，用 `SW_SHOW` 显示窗口。

## 示例

### .bat — 打开计算器
```bat
start "" calc.exe
```

### .ps1 — 打开显示设置
```powershell
Start-Process "ms-settings:display"
```

### .ps1 — 显示窗口（_Show 后缀）
```powershell
# Ping_Show.ps1 — 文件名 _Show 后缀，窗口可见
param($hostname = "8.8.8.8")
Write-Host "Pinging $hostname..." -Foreground Cyan
Test-Connection -ComputerName $hostname -Count 4
Write-Host "Done." -Foreground Green
```

### .bat — 用默认浏览器打开网址
```bat
start "" https://example.com
```

### .ps1 — 打开 Windows 运行对话框
```powershell
(New-Object -ComObject "Shell.Application").FileRun()
```

### .vbs — 创建快捷方式
```vbs
Set ws = CreateObject("WScript.Shell")
Set sc = ws.CreateShortcut(ws.SpecialFolders("Desktop") & "\MyApp.lnk")
sc.TargetPath = "D:\tools\myapp.exe"
sc.Save
```

## 注意事项

- 同名文件只保留先扫描到的，避免重名
- 脚本放入 `scripts\` 后打开 Settings 窗口自动生效（`Scripts_ScanAndSync` 在刷新时调用）
- `.ps1` 执行策略已设为 `Bypass`
- 脚本命令在 Commands 列表中以 `*` 标记，不可手动删除（删除文件即移除命令）
- GUI 程序不受 `_Show` 后缀影响——`start ""` 和 `Start-Process` 启动的窗口始终可见
