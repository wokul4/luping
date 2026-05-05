# Game Capture 模式 — 技术设计与集成方案

## 1. 当前方案的限制

| 场景 | Desktop Duplication | Windows Graphics Capture | 问题 |
|------|-------------------|------------------------|------|
| 窗口化游戏 | ✅ 可用 | ✅ 更好 | DDA 在窗口化下 30fps 时延较高 |
| 无边框全屏 | ⚠️ 部分可用 | ✅ 可用 | DDA 可捕获但帧率受限 |
| 独占全屏 (DX11/12) | ❌ 不可用 | ⚠️ Win+C 可能工作 | DDA 完全失效，WGC 在 DX12 独占全屏不保证 |
| 硬件加速合成 | ⚠️ 固定 1 帧延迟 | ✅ 低延迟 | DDA 增加 ~16ms 延迟 |
| HDR 内容 | ⚠️ 需要额外处理 | ✅ 支持 HDR | DDA 无原生 HDR 支持 |

### 核心问题

1. **独占全屏不可捕获** — DDA 在 DXGI 交换链处于全屏状态时返回 ACCESS_LOST
2. **帧延迟** — DDA 额外增加 1-2 帧复制延迟，对 FPS 游戏不可接受
3. **帧率不匹配** — 捕获帧率受桌面刷新率限制，无法降采样
4. **性能损耗** — DDA 的 GPU staging → CPU readback → swscale 管线在 4K@60fps 时严重消耗 CPU
5. **Cursor 处理** — DDA 需要手动合成鼠标光标，WGC 自动包含

---

## 2. libobs Game Capture 方案

### 2.1 架构总览

```
┌──────────────────────────────────────────────────────────┐
│  ScreenRecorder.exe (UI / Controller / Encoder / Muxer) │
├──────────────────────────────────────────────────────────┤
│  libobs (obs.dll / obs.lib)                               │
│  ├─ obs_source_t "game_capture"                          │
│  │   ├─ hook DLL injection via CreateRemoteThread        │
│  │   ├─ graphics-hook64.dll → 目标游戏进程              │
│  │   └─ shared memory (pipe) → 传递纹理句柄             │
│  ├─ obs_output_t "ffmpeg_muxer" / 自定义 output          │
│  ├─ obs_encoder_t "obs_x264" / 自定义 encoder            │
│  └─ obs_video_info → 配置分辨率、fps                     │
├──────────────────────────────────────────────────────────┤
│  graphics-hook64.dll (注入游戏进程)                        │
│  ├─ Hook D3D11::Present / D3D12::Present / Vulkan        │
│  ├─ 通过共享 handle 传递 ID3D11Texture2D                  │
│  └─ 支持 DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH          │
└──────────────────────────────────────────────────────────┘
```

### 2.2 Hook 机制

OBS game capture 的核心：**DLL 注入 + Detour hook**

1. **graphics-hook64.dll** 编译成独立的 DLL
2. ScreenRecorder 通过 `CreateRemoteThread` + `LoadLibrary` 注入到目标游戏进程
3. hook DLL 在目标进程中用 Microsoft Detours / minhook 拦截 `IDXGISwapChain::Present`
4. 在 Present 调用到达 GPU 前，复制 backbuffer 到一个共享 `ID3D11Texture2D`
5. 通过命名 pipe 通知 ScreenRecorder 纹理句柄 (shared handle)
6. ScreenRecorder 在自己的 D3D11 device 上 `OpenSharedResource` 获取纹理
7. 纹理可以直接送入 NVENC/QSV 编码器（无需 CPU 回读！）

### 2.3 游戏窗口选择

OBS 通过窗口类名或 exe 名匹配目标窗口：

```cpp
struct GameCaptureConfig {
    enum CaptureMode { Window, Process };
    CaptureMode mode;
    std::wstring className;  // 例如 L"UnityWndClass", L"CEF-OSC-WIDGET"
    std::wstring executable; // 例如 L"explorer.exe", L"notepad.exe"
    bool hookRateLimit = false;  // 限帧
    bool captureOverlays = false;  // 捕获覆盖层 (steam 等)
};
```

---

## 3. libobs 集成设计

### 3.1 构建 libobs

libobs 使用 CMake + Windows SDK 构建：

```bash
git clone --recursive https://github.com/obsproject/obs-studio.git
cd obs-studio
cmake -S . -B build \
  -G "Visual Studio 17 2022" -A x64 \
  -DBUILD_BROWSER=OFF \
  -DBUILD_VSTOOLS=OFF \
  -DENABLE_SCRIPTING=OFF \
  -DCMAKE_INSTALL_PREFIX=./install
cmake --build build --config Release --target obs-frontend-api
cmake --install build --config Release
```

需要的关键产物：
- `obs.lib` + `obs.dll` — 核心库
- `obsapi.lib` — 插件 API
- `graphics-hook64.dll` — 用于注入游戏的 hook DLL
- 头文件：`libobs/obs.h`, `libobs/obs-source.h` 等

### 3.2 CMake 集成

> **注意：** libobs 有 50+ 个编译依赖，包括 FFmpeg、zlib、curl、jansson 等，完整构建 libobs 本身就是一个大工程。
>
> **最小替代方案：** 不从源码构建 libobs，而是直接复用 OBS 安装目录的 DLL + 头文件；或仅使用 obs-studio 的 hook 机制，绕开 obs 核心库。

对于当前项目，建议采用**渐进式集成**：

1. **第一步** — 仅提取 `graphics-hook` 子项目，实现 DLL 注入 + 纹理共享（不需要完整的 libobs）
2. **第二步** — 封装 hook 纹理数据到自定义 `GameCaptureSource` 类
3. **第三步** — 可选：完整引入 libobs 使用其 output/encoder 管线

### 3.3 最小 hook 方案（不依赖 libobs）

绕过 obs 核心，直接使用其 hook 机制：

```cpp
// GameCapture.h — 注入 + 纹理共享管理器
class GameCapture {
public:
    enum Status { Idle, Injecting, Running, Error };
    
    bool Inject(DWORD processId);  // 注入 hook DLL
    void Stop();
    
    // 纹理访问（帧数据就绪时调用）
    ID3D11Texture2D* GetFrame();  // 返回共享纹理
    void ReleaseFrame();
    
    Status GetStatus() const;
    
private:
    // 命名 pipe 监听线程
    void PipeThread();
    
    HANDLE m_pipe = INVALID_HANDLE_VALUE;
    HANDLE m_thread = nullptr;
    DWORD  m_processId = 0;
    
    ComPtr<ID3D11Device> m_device;
    ComPtr<ID3D11Texture2D> m_sharedTexture;
};
```

---

## 4. 可能的实现路径

### 路径 A：完整 libobs（推荐，工程量大）

**步骤：**
1. 构建完整的 libobs 及其所有依赖
2. 使用 `obs_source_create("game_capture", ...)` 创建游戏源
3. 设置 `obs_output_t` + `obs_encoder_t` 完成 OBS 管线
4. 从 `obs_source_frame` 或 `obs_source_texture` 获取帧数据

**优点：** 官方维护，功能完整（hook + anti-cheat 兼容 + 设置面板）
**缺点：** 构建复杂，依赖链长 (50+ 第三方库)，二进制体积大

### 路径 B：仅 graphics-hook（推荐，工程中等）

**步骤：**
1. 从 obs-studio 源码提取 `libobs/graphics-hook/` 子项目
2. 编译 `graphics-hook64.dll` 用于注入
3. 在 ScreenRecorder 中实现命名 pipe 服务器 + 共享纹理接收
4. 自定义 capture source 类封装 hook 纹理

**优点：** 无需 obs 核心库，二进制小巧，直接复用已有编码器
**缺点：** 需要实现 pipe 协议解析（OBS 未公开文档，需逆向）

### 路径 C：自研 Detour hook（推荐，技术难度高）

**步骤：**
1. 使用 Microsoft Detours / minhook 拦截 D3D11 Present
2. 在 hook 中复制 backbuffer → staging → 共享纹理
3. 通过自定义 IPC（pipe + shared handle）传递给主进程

**优点：** 无外部依赖，完全可控
**缺点：** 需要深入 DXGI 编程，处理 DX12/Vulkan 需要额外工作

---

## 5. 最小 demo 代码（路径 C + 路径 B 混合）

以下代码演示如何通过窗口句柄找到目标进程、注入 hook，并通过共享纹理获取帧数据。

### 5.1 查找游戏窗口

```cpp
// 通过 exe 名或窗口类名查找进程
DWORD FindGameProcess(const wchar_t* exeName) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    
    PROCESSENTRY32W entry = { sizeof(entry) };
    DWORD pid = 0;
    
    if (Process32FirstW(snap, &entry)) {
        do {
            if (_wcsicmp(entry.szExeFile, exeName) == 0) {
                pid = entry.th32ProcessID;
                break;
            }
        } while (Process32NextW(snap, &entry));
    }
    CloseHandle(snap);
    return pid;
}
```

### 5.2 DLL 注入

```cpp
bool InjectDLL(DWORD pid, const wchar_t* dllPath) {
    HANDLE hProcess = OpenProcess(
        PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
        PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ,
        FALSE, pid);
    if (!hProcess) return false;
    
    size_t pathLen = (wcslen(dllPath) + 1) * sizeof(wchar_t);
    void* remoteMem = VirtualAllocEx(hProcess, nullptr, pathLen,
                                      MEM_COMMIT, PAGE_READWRITE);
    if (!remoteMem) { CloseHandle(hProcess); return false; }
    
    WriteProcessMemory(hProcess, remoteMem, dllPath, pathLen, nullptr);
    
    HANDLE hThread = CreateRemoteThread(hProcess, nullptr, 0,
        (LPTHREAD_START_ROUTINE)LoadLibraryW, remoteMem, 0, nullptr);
    if (!hThread) { VirtualFreeEx(hProcess, remoteMem, 0, MEM_RELEASE);
                    CloseHandle(hProcess); return false; }
    
    WaitForSingleObject(hThread, 10000);
    CloseHandle(hThread);
    VirtualFreeEx(hProcess, remoteMem, 0, MEM_RELEASE);
    CloseHandle(hProcess);
    return true;
}
```

### 5.3 枚举窗口与进程匹配

```cpp
struct GameWindow {
    HWND hwnd;
    DWORD pid;
    std::wstring title;
    std::wstring exe;
    int width, height;
};

std::vector<GameWindow> EnumerateGameWindows() {
    std::vector<GameWindow> result;
    
    EnumWindows([](HWND hwnd, LPARAM lparam) -> BOOL {
        auto* vec = reinterpret_cast<std::vector<GameWindow>*>(lparam);
        
        if (!IsWindowVisible(hwnd)) return TRUE;
        if (GetWindowTextLengthW(hwnd) == 0) return TRUE;
        
        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        
        HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,
                                       FALSE, pid);
        if (!hProcess) return TRUE;
        
        wchar_t exe[MAX_PATH];
        DWORD size = MAX_PATH;
        QueryFullProcessImageNameW(hProcess, 0, exe, &size);
        CloseHandle(hProcess);
        
        std::wstring exeName = std::filesystem::path(exe).filename().wstring();
        
        RECT rc;
        GetClientRect(hwnd, &rc);
        
        vec->push_back({ hwnd, pid, L"", exeName,
                         (int)(rc.right - rc.left),
                         (int)(rc.bottom - rc.top) });
        return TRUE;
    }, reinterpret_cast<LPARAM>(&result));
    
    return result;
}
```

---

## 6. Fallback 策略

在 game capture 不可用时，自动降级为更可靠的捕获方式：

```cpp
enum CaptureBackend {
    GameHook,       // libobs hook → 最优 (低延迟, 独占全屏)
    GraphicsCapture, // Windows.Graphics.Capture → 次优 (Win10+, 无独占全屏)
    DesktopDuplication, // DXGI Desktop Dupl → 最后手段
};

CaptureBackend SelectBestBackend(HWND targetWindow) {
    DWORD pid = 0;
    GetWindowThreadProcessId(targetWindow, &pid);
    
    // 检查是否支持 game hook
    if (IsSupportedD3DGame(pid)) return GameHook;
    
    // Windows 10 1803+ 推荐 WGC
    if (IsWindows10OrGreater(1803)) return GraphicsCapture;
    
    // 最后 fallback
    return DesktopDuplication;
}
```

---

## 7. 已知问题与解决方案

| 问题 | 原因 | 解决方案 |
|------|------|----------|
| **反作弊踢出** (EAC/BattlEye) | 检测到 DLL 注入标记 | hook 前置在游戏启动前加载；签名 graphics-hook DLL；使用微软签名证书 |
| **DX12 独占全屏** | DXGI 交换链 flag FLIP_DISCARD | 需要在创建 Hook 替换 CreateSwapChain |
| **Vulkan 游戏** | Vulkan 无 Present 可拦截 | 使用 Vulkan layer 捕获 (VK_LAYER_LUNARG_api_dump) |
| **黑屏** | 权限不足 / hook 时机过早 | 管理员运行；延迟 hook 直到 Present 首次调用 |
| **管理员权限** | 低权限进程无法注入高权限 | ScreenRecorder 需要管理员运行；或使用后台服务注入 |
| **UWP / Microsoft Store 游戏** | 受保护进程 (PPL) | 只能使用 Windows.Graphics.Capture API |
| **延迟过高** | GPU → CPU 回读 | 直接使用 GPU 纹理编码 (NVENC 共享 handle) |

### 权限解决方案

```cpp
// 检测是否管理员运行
bool IsElevated() {
    HANDLE hToken = nullptr;
    OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken);
    TOKEN_ELEVATION elev;
    DWORD size = sizeof(elev);
    GetTokenInformation(hToken, TokenElevation, &elev, size, &size);
    CloseHandle(hToken);
    return elev.TokenIsElevated != 0;
}

// 请求管理员权限重启
void RestartElevated() {
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    ShellExecuteW(nullptr, L"runas", exePath, nullptr, nullptr, SW_SHOW);
}
```

---

## 8. 建议的实现顺序

```
Phase 5.1 — 调研阶段 ✓ (当前)
  ├── 分析现有方案的局限性
  ├── 设计 libobs 集成方案
  └── 确定技术路径

Phase 5.2 — Fallback 增强
  ├── Window Enumerator (枚举所有可捕获窗口)
  ├── 标题栏录制的窗口选择 UI
  └── 利用 DDA + SetForegroundWindow 实现"跟随窗口"

Phase 5.3 — graphics-hook 集成（推荐）
  ├── 构建 graphics-hook64.dll
  ├── 实现 Pipe 服务器 + SharedTexture 接收
  └── GameCaptureSource 类封装

Phase 5.4 — 反作弊兼容（可选）
  ├── 安装时注入 vs 运行时注入
  ├── 签名 hook DLL
  └── Windows-Graphics-Capture 前置降级

Phase 5.5 — NVENC 直通（可选）
  ├── 从 hook 获取的纹理直接送入 NVENC
  └── 跳过 staging → CPU → swscale 管线
```
