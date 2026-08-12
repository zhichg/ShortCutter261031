# ShortCutter 4.0 — 鼠标手势工具

## 概述

ShortCutter 是一款 Windows 全局鼠标手势工具。按住鼠标右键（或中键）绘制任意形状手势，即可触发对应命令。同时支持全局热键、命令弹框启动器、脚本扩展、已装软件一键导入。纯 C + Win32 API，单 exe 静态链接，无外部依赖。

## 核心功能

| 功能 | 说明 |
|------|------|
| **自定义手势** | 录制任意形状轨迹，方向链编码 + LCS 匹配，支持多点采样提高识别率 |
| **区域手势** | 屏幕分 5 区（四角 + 主区），同一手势不同位置绑不同命令 |
| **实时轨迹** | 拖动时显示蓝色轨迹线，可选绿色分区边界线 |
| **内置命令** | 27 个：窗口管理/标签切换 (Built-in:Keys) + 系统工具 (Built-in:Run) |
| **Scan Apps** | Commands > Scan Apps... 扫描桌面和开始菜单快捷方式，勾选一键导入 |
| **仪表盘** | Windows 风格仪表盘弹窗，搜索/常用排行/自定义 Dock，拖拽排列 |
| **全局热键** | 独立快捷键绑定，直接触发命令 |
| **命令弹框** | 输入关键字搜索执行：内置指令 → 脚本 → Commands 列表 |
| **脚本扩展** | `scripts\` 内 `.bat`/`.cmd`/`.ps1`/`.vbs` 自动注册，默认静默执行 |
| **脚本窗口控制** | 文件名以 `_Show` 结尾 → 显示控制台，其余隐藏 |
| **脚本改名同步** | Settings 中修改脚本 Name → `MoveFile` 同步重命名实际文件 |
| **命令分组** | 每个命令可分配 Group（Built-in:Keys / Built-in:Run / Scripts / 自定义） |
| **筛选系统** | Commands 标签页 Type + Group 双层筛选，Gestures 标签页 Zone 筛选 |
| **单实例** | 同名互斥体保证只运行一个进程 |
| **DPI 感知** | 手势轨迹自适应 DPI，高分辨率屏正常显示 |
| **开机自启** | 可选 HKCU Run 注册表启动项 |

## 文件结构

```
ShortCutter\
├── ShortCutter.exe       ← 编译产物，独立运行（~200KB 静态链接）
├── ShortCutter.ini       ← UTF-16 LE 编码，保存所有配置
├── README.md             ← 首次运行时自动生成
└── scripts\              ← 脚本扩展目录（自动创建）
    ├── AI_PROMPT.md      ← AI 编写脚本的指南（首次运行自动生成）
    ├── Display.bat       ← 打开显示设置
    └── run.ps1           ← 打开运行对话框
```

## 源码结构

```
src\
├── main.h        ← 所有公共定义：常量、枚举、结构体、全局变量声明
├── main.c        ← WinMain 入口、托盘图标、消息循环、全局热键、文档生成
├── gesture.c     ← 鼠标钩子（WH_MOUSE_LL）、方向链编码、LCS 匹配、轨迹窗口、区域判定
├── actions.c     ← 动作执行：命令弹框、脚本扫描、内置指令、按键序列
├── config.c      ← INI 配置读写（UTF-16 LE）、预置命令创建、脚本同步
├── dashboard.c   ← 仪表盘弹窗：搜索、常用排行、自定义命令 Dock
├── ui.c          ← 设置对话框（4 标签页）、录制弹窗、Help 弹窗、Scan Apps 弹窗
├── resource.rc   ← 版本信息、图标资源
└── resource.h    ← 资源 ID
```

## 关键数据结构

```
AppConfig
├── Command commands[100]       ← 独立命令列表
│   ├── id, name, group
│   ├── CmdType type            ← CMD_RUN | CMD_KEYS | CMD_SCRIPT
│   ├── runPath[512]            ← 程序路径或脚本路径
│   ├── keySeq[512]             ← 按键序列
│   └── locked                  ← 1=不可删除
├── GestureDef gestures[100]    ← 自定义手势列表
│   ├── name, modifier, zone, enabled
│   ├── chains[3][64]           ← 多采样方向链
│   └── ptsStr[3][1024]         ← 采样点数据
├── HotkeyDef hotkeys[100]      ← 独立快捷键
│   ├── mods, vk, label, description
│   └── commandId, enabled
├── 设置项：minGesture, matchThreshold, mouseButton
│           trailEnabled, zoneLinesEnabled, autoStart, enabled
└── toggleMods, toggleVk, toggleLabel  ← 开关快捷键
```

## 手势识别算法

**方向链编码（Direction Chain Encoding）**：

```
原始轨迹 → 降采样 → 逐段量化到 8 方向 → 合并连续重复 → 方向链字符串
例：画 "W" → 先右下、再右上、再右下 → 方向链 "515"
```

**匹配**：LCS（最长公共子序列）计算输入链与模板链的相似度百分比，达到 matchThreshold（默认 70%）即匹配。

**自适应降采样**：降采样距离 = max(包围盒宽, 包围盒高) / 20，限制范围 [5, 50]px。

## 区域划分

```
┌──────────┬──────────┬──────────┐
│ ↖ TL     │          │ ↗ TR     │
│  (1)     │   Main   │  (2)     │
│          │   (0)    │          │
├──────────┤          ├──────────┤  ← 1/3 屏幕高
│          │          │          │
│          │          │          │
├──────────┤          ├──────────┤  ← 2/3 屏幕高
│ ↙ BL     │          │ ↘ BR     │
│  (3)     │   Main   │  (4)     │
│          │   (0)    │          │
└──────────┴──────────┴──────────┘
```

匹配优先级：精确 zone 匹配 > Main(zone=0) 作为 fallback。

## 按键序列格式

```
{Key down}   按下并保持
{Key up}     释放
{Key}        按下并释放（默认）
{Sleep N}    延迟 N 毫秒

支持: Ctrl Alt Shift Win Tab Enter Space Esc Backspace Delete
      Insert Home End PgUp PgDn Up Down Left Right
      F1-F12 Apps PrtSc A-Z 0-9
```

例：`{Ctrl down}{C}{Ctrl up}` = Ctrl+C

## 仪表盘 (Dashboard)

ShortCutter 4.0 新增仪表盘弹窗，类似 Windows 开始菜单风格，提供命令搜索、使用统计和自定义快捷方式。

### 布局 (960x825 px)

```
┌──────────────────────────────────────────┐
│  Dashboard                              │ ← 标题栏 + 统计
├──────────────────┬───────────────────────┤
│  Search          │  Most Used            │
│  [搜索框 50px]   │  1. Notepad      12x  │
│  ┌────────────┐  │  2. Calculator    8x  │
│  │ 历史/匹配  │  │  3. ...               │
│  │ (行高45px) │  │  (Top 8, 次数右对齐)  │
│  └────────────┘  │                       │
├──────────────────┴───────────────────────┤
│  Custom (double-click to add)           │ ← 双击添加推荐列表高亮项
│  [Icon] [Icon] [Icon] [Icon] ...        │ ← 6列×4行, 120px图标, 可拖拽
└──────────────────────────────────────────┘
```

### 功能说明

| 区域 | 功能 |
|------|------|
| **Search** | 模糊搜索命令名/脚本，Space 键补全并执行第一匹配项，Enter 执行选中项，Tab 补全到搜索框 |
| **Most Used** | 按 `useCount` 排序，显示 Top 8 命令及使用次数，点击执行 |
| **Custom Dock** | 自定义命令快捷方式，拖拽排列到网格位置，双击 Custom 添加，右键图标可移除 |
| **历史/匹配** | 输入为空时显示最近使用的命令；有输入时按模糊匹配排序显示 |
| **统计栏** | 顶栏显示 `Cmd:N Gest:N Hk:N Scpt:N` |

### 配置持久化

退出时自动保存到 `ShortCutter.ini`：
- `[Dashboard]` — DockCount, DockCmdN, DockPosN, DockIconN
- `[Usage]` — 每个命令的使用次数和最后使用时间
- `[History]` — 最近 20 条命令记录

## 脚本系统

### 命名约定

| 文件名 | 效果 |
|--------|------|
| `run.ps1` | 🔇 静默执行，无窗口 |
| `Ping_Show.ps1` | 📺 显示 PowerShell 窗口 |
| `Backup.bat` | 🔇 静默执行 |
| `Install_Show.cmd` | 📺 显示 CMD 窗口 |

### 改名同步

Settings → Commands → 选中脚本 → Edit → 修改 Name → OK。实际文件被 `MoveFile` 重命名，扩展名不变。

## 配置文件（ShortCutter.ini）

UTF-16 LE 编码，包含 `[Settings]` `[Commands]` `[Gestures]` `[Hotkeys]` `[Dashboard]` `[Usage]` `[History]` 七个 section。保存时全量重写，加载时自动增量补充缺省的预置命令。

## 构建方式

- **编译器**：MinGW-w64 gcc（静态链接）
- **依赖**：仅 Win32 API（`comctl32 gdi32 user32 kernel32 shell32 imm32 advapi32 ole32 uuid`）
- **编译**：
  ```
  windres src/resource.rc -o src/resource.o
  gcc -static -Os -s -Wall -Wextra -o ShortCutter.exe src/*.c src/resource.o \
      -lcomctl32 -lgdi32 -luser32 -lkernel32 -lshell32 -limm32 -ladvapi32 -lole32 -luuid -mwindows
  ```
- **产物**：约 200KB，无额外 DLL

## 设计原则

- **零依赖**：纯 Win32 API，单 exe 文件即可运行
- **无管理员权限**：配置写在 exe 同目录 INI，注册表仅用于开机自启（可选）
- **UTF-8 内部存储**：所有文本字段存为 UTF-8（char[]），UI 用 Wide API 做转换
- **进程去重**：单实例互斥体，重复启动静默退出
- **静默优先**：脚本默认隐藏控制台窗口，GUI 程序不受影响
