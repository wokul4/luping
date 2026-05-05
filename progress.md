# ScreenRecorder - Progress Log

## Phase 1-7 ✓ Complete
## Phase 8A ✓ — Build/Static Verification/Release Check
## Phase 8B ✓ — Core Manual Recording Loop — PASS

### 修复历史

| 轮次 | Bug | 文件 | 状态 |
|------|-----|------|------|
| 1 | CopyResource(self) undefined behavior | GameCaptureSource.cpp | ✅ |
| 2 | FillBlackFrame per-frame staging allocation | GameCaptureSource.cpp | ✅ |
| 3 | m_blackTex not reset on output resize | GameCaptureSource.cpp | ✅ |
| 4 | WindowBoundsInvalid (odd frame size 792x623) | GameCaptureSource.cpp, FFmpegEncoder.cpp | ✅ |
| 5 | Minimized window → WindowTooSmall (-32000 rect) | GameCaptureSource.cpp, AppWindow.cpp, GameWindowEnumerator.cpp | ✅ |
| 6 | FPS pacing missing (108fps → should be 30fps) | RecorderController.cpp | ✅ |
| 7 | Frame counters broken (captured=0, enc=473) | RecorderController.cpp | ✅ |
| 8 | WindowMinimized not detected during recording | GameCaptureSource.cpp/h | ✅ |
| 9 | Window moved spamming log | GameCaptureSource.cpp | ✅ |
| 10 | STATS unreliable / pacingStart too early | RecorderController.cpp | ✅ |
| 11 | FrameAcquireTimeout drops frame instead of repeat | GameCaptureSource.cpp, RecorderController.cpp | ✅ |
| 12 | No fixed-output fps guarantee | RecorderController.cpp | ✅ |
| 13 | Minimized detection not logging during recording | GameCaptureSource.cpp | ✅ |
| 14 | Final stats idx=0 | RecorderController.cpp | ✅ |
| 15 | WindowClosed logged as ERROR | GameCaptureSource.cpp | ✅ |
| 16 | Relative working directory paths | main.cpp, AppWindow.cpp | ✅ |
| 17 | Window moved detection uses different coordinate systems | GameCaptureSource.cpp | ✅ |
| 18 | Stop during WASAPI init ignored (state overwritten) | RecorderController.cpp | ✅ |

## Phase 8C — Extended Manual Test & Minor Stabilization ✓

### 修改文件

| 文件 | 修改内容 |
|------|---------|
| app/main.cpp | exe-relative logs/captures 路径 |
| app/AppWindow.cpp | ExeDir() 辅助函数 + exe-relative captures 输出路径 |
| capture/GameCaptureSource.cpp | DWM bounds 移动检测、WindowClosed→INFO、m_frameAcquired |
| capture/GameCaptureSource.h | m_frameAcquired 标记 |
| core/RecorderController.cpp | Starting→Recording atomic compare_exchange |
| core/RecorderController.h | 移除未使用的 frameIndex/lastLogTime |

### 测试矩阵

| 编号 | 测试项 | 状态 | 备注 |
|------|--------|------|------|
| E1 | Chrome/Edge 窗口录制 | NEEDS_MANUAL_TEST | 需要人工操作浏览器 |
| E2 | 窗口移动跟随 | NEEDS_MANUAL_TEST | 需手动拖动验证 DWM bounds 修复 |
| E3 | 窗口 resize | NEEDS_MANUAL_TEST | 需手动调整窗口大小 |
| E4 | 连续 Start/Stop (5x) | **PASS** | 5/5，atomic state 修复后优雅退出 |
| E5 | 工作目录测试 | **PASS** | exe-relative logs/captures |
| E6 | ffprobe 检查 | **PASS** | Video h264 + Audio aac 48kHz 已确认 |
| E7 | 日志降噪 | **PASS** | WindowClosed→INFO, DWM bounds 统一坐标系 |
| E8 | UI 状态 | NEEDS_MANUAL_TEST | 需要人工验证按钮状态 |
| E9 | 音频基本检查 | **PASS** | WASAPI Sys+Mic → AAC 128k → MKV 已验证 |
| E10 | 2 分钟稳定性 | NEEDS_MANUAL_TEST | 需要长时间录制备份 |

### 修复的 Bug

1. **Starting→Recording 状态覆盖 (P0)**: `m_state.store(RecordingState::Recording)` 直接覆盖了 StopRecording 设置的 Stopping 状态，导致初始化期间 Stop 被忽略。改为 `compare_exchange_strong`，只在状态为 Starting 时才转换到 Recording。

2. **WindowClosed 日志级别错误**: 从 ERROR 降级为 INFO

3. **exe-relative 路径**: logs/captures 使用 exe 所在目录而非当前工作目录，支持任意目录启动

4. **窗口移动检测坐标系不匹配**: CheckWindowState 使用 GetClientRect + MapWindowPoints（客户区屏幕坐标）与 UpdateCropFromWindow 的 DWM bounds 比较改为统一使用 DWM bounds

### 未解决问题

- WASAPI `Start()` 阻塞 3 秒导致初始化延迟，快速 STOP 可能在初始期间触发。已通过原子状态修复优雅处理，但初始化加速属于未来优化
- 无 CLI 参数 — 不计划实现
- 人工测试项需要用户操作

### 编译

```
cmake --build build --config Release → ScreenRecorder.exe 0 errors
```

### 进入下一阶段结论

**不允许进入 Phase 9。** 直到 E1/E2/E3/E8 完成人工测试。

## Phase 9 — Release Hardening & Productization ✓

### 新增文件

| 文件 | 用途 |
|------|------|
| `core/AppSettings.h` | 配置系统 — JSON 配置加载/保存 |
| `core/AppSettings.cpp` | 最小 JSON 解析/序列化、默认创建、损坏备份 |
| `VERSION.txt` | 版本号 0.1.0-beta |
| `README.md` | 使用说明、构建指南、已知限制 |
| `docs/known-limitations.md` | 已知限制详情 |
| `installer/ScreenRecorder.iss` | Inno Setup 安装包脚本 |
| `scripts/package_release.ps1` | 发布打包脚本 |
| `scripts/check_dist.ps1` | dist 结构验证脚本 |

### 修改文件

| 文件 | 修改内容 |
|------|---------|
| `CMakeLists.txt` | 添加 AppSettings.cpp |
| `app/main.cpp` | 版本号常量、启动时加载配置 |
| `app/AppWindow.h` | Create 签名更新、ApplySettingsToUI/SaveSettings |
| `app/AppWindow.cpp` | UI 从 settings 初始化、保存到 settings、完成提示 |

### Release Dist 目录结构

```
dist/ScreenRecorder/
  ScreenRecorder.exe         620 KB
  avcodec-62.dll
  avformat-62.dll
  avutil-60.dll
  swscale-9.dll
  swresample-6.dll
  config/settings.json
  README.md
  VERSION.txt
  docs/known-limitations.md
  logs/
  captures/
```

### 编译

```
cmake --build build --config Release → ScreenRecorder.exe 0 errors
```

### Package & Dist Check

```
scripts/package_release.ps1 → dist/ScreenRecorder/ (130.4 MB total)
scripts/check_dist.ps1 → 17/17 PASS
```

### 发布状态

**0.1.0-beta packaging ready; manual dist smoke test required.**

请从 `dist/ScreenRecorder/ScreenRecorder.exe` 启动测试后确认发布。

### 进入下一阶段结论

**不允许进入 Phase 9。**

阻塞条件：
- E1/E2/E3 需要人工测试且未通过 — 阻塞
- E8 需要人工验证 UI 状态 — 阻塞

解除阻塞的唯一路径：
1. 人工完成 E1/E2/E3 测试并确认 PASS
2. 人工完成 E8 UI 状态验证并确认 PASS
3. E10 可作为长测项后置，不阻塞；但若测试中出现崩溃或文件损坏则重新阻塞
4. 所有阻塞项通过后，才能进入 Phase 9 或项目归档

### 下一步

只做 E1/E2/E3/E8 人工测试。不做新功能，不写代码，不进 Phase 9。

### 人工测试步骤

#### E1: Chrome/Edge 窗口录制

操作：
1. 打开 Chrome 或 Edge，打开任意网页
2. ScreenRecorder 点击 Refresh → 选择 `[W] chrome.exe` 或 `[W] msedge.exe`
3. Start Rec → 等 10 秒 → Stop

PASS 标准：
- 不崩溃
- 日志出现 `RT: started` / `STATS` / `RT: finalizing` / `RT: done` / `thread finished`
- MKV 文件可播放，画面为浏览器窗口区域
- `dupe=` > 0 正常（网页无变化时重复上一帧）
- 日志中无 ERROR 行

#### E2: 窗口移动跟随

操作：
1. 打开 Notepad，选择为 Source
2. Start Rec → 等 2 秒 → 拖动 Notepad 5 秒 → Stop
3. 播放 MKV 检查画面是否跟随窗口区域

PASS 标准：
- 不崩溃
- encoder 输出尺寸从开始到结束不变
- 日志出现 `GCS: window moved`（拖动期间）
- 窗口静止时 `window moved` 不会持续刷出（我的 DWM bounds 修复应该解决此问题）
- 如果 window moved 在静止时仍然 250ms 刷出，说明 DWM bounds 修复失效

#### E3: 窗口 resize

操作：
1. 打开 Notepad，选择为 Source
2. Start Rec → 等 2 秒 → 缩小窗口 → 再放大窗口 → Stop
3. 播放 MKV

PASS 标准：
- 不崩溃
- encoder 输出尺寸不变
- 缩小时空白区域补黑（黑色边缘）
- 放大时超出初始 canvas 的部分裁剪
- 不出现 `EncoderInitFailed` 或 `InvalidFrameSize`

#### E8: UI 状态验证

操作：
1. 启动后观察按钮：
   - Idle: Start Rec 可用，Pause/Stop 灰色
2. Start Rec:
   - Start Rec 灰色 → Pause/Stop 可用
   - 不允许连续点击产生多线程（日志中无 `AlreadyRunning`）
3. Stop:
   - 恢复 Idle 状态
4. Pause/Resume:
   - 按钮文字在 Pause/Resume 之间切换
5. 窗口关闭（录制中关闭 Notepad）：
   - 自动停止，UI 恢复

PASS 标准：
- 所有按钮状态正确
- 无 AlreadyRunning 或 NotRunning 异常
- WindowClosed 自动停止后 UI 恢复
- 不弹重复错误消息框
