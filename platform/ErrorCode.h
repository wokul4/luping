#pragma once
#include <cstdint>
#include <string>

// ============================================================
// ScrError — project-wide error codes
// Extends the original enum while keeping old values intact.
// ============================================================
enum class ScrError : uint32_t {
    // Generic (keep original values for backward compat)
    Success               = 0,  // alias: Ok
    Ok                    = 0,
    DeviceCreateFailed    = 1,
    AdapterNotFound       = 2,
    OutputNotFound        = 3,
    DuplicationFailed     = 4,
    FrameAcquireFailed    = 5,
    FrameAcquireTimeout   = 6,
    TextureCreateFailed   = 7,
    MapFailed             = 8,
    SaveFrameFailed       = 9,
    InvalidParam          = 10,
    AlreadyInitialized    = 11,
    NotInitialized        = 12,
    EnumerationFailed     = 13,
    ComInitFailed         = 14,
    WindowClosed          = 15,
    WindowMinimized       = 16,

    // === Extended codes (Phase 6/7) ===
    // D3D / DXGI
    D3DDeviceCreationFailed   = 20,
    D3DContextFailed          = 21,
    DXGIAdapterNotFound       = 22,
    DXGIOutputNotFound        = 23,
    DuplicationAccessLost     = 24,
    DuplicationRecreateFailed = 25,
    FrameAcquireTimeout2      = 26,   // alias for FrameAcquireTimeout
    TextureCopyFailed         = 27,
    TextureMapFailed          = 28,

    // Monitor
    MonitorNotFound           = 30,
    InvalidMonitorIndex       = 31,
    MonitorBoundsInvalid      = 32,

    // Window
    WindowNotFound            = 40,
    WindowInvisible           = 41,
    WindowCloaked             = 42,
    WindowTooSmall            = 43,
    WindowBoundsInvalid       = 44,
    WindowOutsideMonitor      = 45,
    WindowCrossedMonitor      = 46,
    WindowCropOutOfBounds     = 47,
    WindowCaptureUnsupported  = 48,

    // Encoder
    EncoderInitFailed         = 50,
    EncoderWriteFailed        = 51,
    EncoderFlushFailed        = 52,
    InvalidFrameSize          = 53,
    PixelFormatUnsupported    = 54,

    // Audio
    AudioDeviceNotFound       = 60,
    AudioInitFailed           = 61,
    AudioCaptureFailed        = 62,
    AudioMixFailed            = 63,

    // Output / muxer
    OutputPathInvalid         = 70,
    OutputFileCreateFailed    = 71,
    MuxerInitFailed           = 72,
    MuxerWriteFailed          = 73,

    // State
    AlreadyRunning            = 80,
    NotRunning                = 81,

    Unknown                   = 99,
};

// ============================================================
// ToString — English identifier for logs
// ============================================================
inline const char* ScrErrorToString(ScrError code) {
    switch (code) {
    case ScrError::Ok:                         return "Ok";
    case ScrError::DeviceCreateFailed:        return "DeviceCreateFailed";
    case ScrError::AdapterNotFound:           return "AdapterNotFound";
    case ScrError::OutputNotFound:            return "OutputNotFound";
    case ScrError::DuplicationFailed:         return "DuplicationFailed";
    case ScrError::FrameAcquireFailed:        return "FrameAcquireFailed";
    case ScrError::FrameAcquireTimeout:       return "FrameAcquireTimeout";
    case ScrError::TextureCreateFailed:       return "TextureCreateFailed";
    case ScrError::MapFailed:                 return "MapFailed";
    case ScrError::SaveFrameFailed:           return "SaveFrameFailed";
    case ScrError::InvalidParam:              return "InvalidParam";
    case ScrError::AlreadyInitialized:        return "AlreadyInitialized";
    case ScrError::NotInitialized:            return "NotInitialized";
    case ScrError::EnumerationFailed:         return "EnumerationFailed";
    case ScrError::ComInitFailed:             return "ComInitFailed";
    case ScrError::WindowClosed:              return "WindowClosed";
    case ScrError::WindowMinimized:           return "WindowMinimized";
    case ScrError::D3DDeviceCreationFailed:   return "D3DDeviceCreationFailed";
    case ScrError::D3DContextFailed:          return "D3DContextFailed";
    case ScrError::DXGIAdapterNotFound:       return "DXGIAdapterNotFound";
    case ScrError::DXGIOutputNotFound:        return "DXGIOutputNotFound";
    case ScrError::DuplicationAccessLost:     return "DuplicationAccessLost";
    case ScrError::DuplicationRecreateFailed: return "DuplicationRecreateFailed";
    case ScrError::FrameAcquireTimeout2:      return "FrameAcquireTimeout";
    case ScrError::TextureCopyFailed:         return "TextureCopyFailed";
    case ScrError::TextureMapFailed:          return "TextureMapFailed";
    case ScrError::MonitorNotFound:           return "MonitorNotFound";
    case ScrError::InvalidMonitorIndex:       return "InvalidMonitorIndex";
    case ScrError::MonitorBoundsInvalid:      return "MonitorBoundsInvalid";
    case ScrError::WindowNotFound:            return "WindowNotFound";
    case ScrError::WindowInvisible:           return "WindowInvisible";
    case ScrError::WindowCloaked:             return "WindowCloaked";
    case ScrError::WindowTooSmall:            return "WindowTooSmall";
    case ScrError::WindowBoundsInvalid:       return "WindowBoundsInvalid";
    case ScrError::WindowOutsideMonitor:      return "WindowOutsideMonitor";
    case ScrError::WindowCrossedMonitor:      return "WindowCrossedMonitor";
    case ScrError::WindowCropOutOfBounds:     return "WindowCropOutOfBounds";
    case ScrError::WindowCaptureUnsupported:  return "WindowCaptureUnsupported";
    case ScrError::EncoderInitFailed:         return "EncoderInitFailed";
    case ScrError::EncoderWriteFailed:        return "EncoderWriteFailed";
    case ScrError::EncoderFlushFailed:        return "EncoderFlushFailed";
    case ScrError::InvalidFrameSize:          return "InvalidFrameSize";
    case ScrError::PixelFormatUnsupported:    return "PixelFormatUnsupported";
    case ScrError::AudioDeviceNotFound:       return "AudioDeviceNotFound";
    case ScrError::AudioInitFailed:           return "AudioInitFailed";
    case ScrError::AudioCaptureFailed:        return "AudioCaptureFailed";
    case ScrError::AudioMixFailed:            return "AudioMixFailed";
    case ScrError::OutputPathInvalid:         return "OutputPathInvalid";
    case ScrError::OutputFileCreateFailed:    return "OutputFileCreateFailed";
    case ScrError::MuxerInitFailed:           return "MuxerInitFailed";
    case ScrError::MuxerWriteFailed:          return "MuxerWriteFailed";
    case ScrError::AlreadyRunning:            return "AlreadyRunning";
    case ScrError::NotRunning:                return "NotRunning";
    case ScrError::Unknown:                   return "UnknownError";
    default:                                  return "UnrecognizedError";
    }
}

// ============================================================
// ToUserMessage — Chinese user-facing message for UI
// ============================================================
inline std::wstring ScrErrorToUserMessage(ScrError code) {
    switch (code) {
    case ScrError::Ok:                      return L"";
    case ScrError::Unknown:                 return L"未知错误，请查看日志。";
    case ScrError::InvalidParam:            return L"参数无效，请检查配置。";
    case ScrError::NotInitialized:          return L"组件未初始化，请重启程序。";
    case ScrError::AlreadyRunning:          return L"录制已在进行中。";
    case ScrError::NotRunning:              return L"录制未启动。";
    case ScrError::AlreadyInitialized:      return L"组件已初始化。";

    case ScrError::DeviceCreateFailed:
    case ScrError::D3DDeviceCreationFailed: return L"D3D11 设备创建失败，请检查显卡驱动。";
    case ScrError::D3DContextFailed:        return L"D3D11 上下文初始化失败。";
    case ScrError::AdapterNotFound:
    case ScrError::DXGIAdapterNotFound:     return L"未找到可用显卡适配器。";
    case ScrError::OutputNotFound:
    case ScrError::DXGIOutputNotFound:      return L"未找到显示器输出。";

    case ScrError::DuplicationFailed:       return L"屏幕捕获初始化失败，请尝试以管理员身份运行。";
    case ScrError::DuplicationAccessLost:   return L"屏幕捕获会话已失效，正在尝试恢复。";
    case ScrError::DuplicationRecreateFailed: return L"屏幕捕获恢复失败，请重新开始录制。";
    case ScrError::FrameAcquireTimeout:
    case ScrError::FrameAcquireTimeout2:    return L"等待屏幕帧超时。";
    case ScrError::FrameAcquireFailed:      return L"屏幕帧获取失败。";
    case ScrError::TextureCopyFailed:       return L"纹理拷贝失败。";
    case ScrError::MapFailed:
    case ScrError::TextureMapFailed:        return L"纹理映射失败。";

    case ScrError::MonitorNotFound:         return L"未找到指定显示器。";
    case ScrError::InvalidMonitorIndex:     return L"显示器索引无效。";
    case ScrError::MonitorBoundsInvalid:    return L"显示器边界无效。";

    case ScrError::WindowNotFound:          return L"未找到目标窗口。";
    case ScrError::WindowClosed:            return L"目标窗口已关闭，录制已停止。";
    case ScrError::WindowMinimized:         return L"目标窗口已最小化，正在保持上一帧。";
    case ScrError::WindowInvisible:         return L"目标窗口不可见。";
    case ScrError::WindowCloaked:           return L"目标窗口被系统隐藏。";
    case ScrError::WindowTooSmall:          return L"目标窗口尺寸过小（最小 80x80）。";
    case ScrError::WindowBoundsInvalid:     return L"窗口边界无效。";
    case ScrError::WindowOutsideMonitor:    return L"窗口移出了显示器范围。";
    case ScrError::WindowCrossedMonitor:    return L"窗口已跨显示器移动，正在重新连接。";
    case ScrError::WindowCropOutOfBounds:   return L"窗口裁剪区域越界。";
    case ScrError::WindowCaptureUnsupported: return L"该窗口不支持捕获。";

    case ScrError::EncoderInitFailed:       return L"编码器初始化失败。";
    case ScrError::EncoderWriteFailed:      return L"编码器写入失败。";
    case ScrError::EncoderFlushFailed:      return L"编码器刷新失败。";
    case ScrError::InvalidFrameSize:        return L"无效的帧尺寸。";
    case ScrError::PixelFormatUnsupported:  return L"不支持的像素格式。";

    case ScrError::AudioDeviceNotFound:     return L"未找到音频设备。";
    case ScrError::AudioInitFailed:         return L"音频初始化失败。";
    case ScrError::AudioCaptureFailed:      return L"音频采集失败。";
    case ScrError::AudioMixFailed:          return L"音频混音失败。";

    case ScrError::OutputPathInvalid:       return L"输出路径无效，请检查保存位置。";
    case ScrError::OutputFileCreateFailed:  return L"无法创建输出文件，请检查保存路径和权限。";
    case ScrError::MuxerInitFailed:         return L"封装器初始化失败。";
    case ScrError::MuxerWriteFailed:        return L"封装器写入失败，输出文件可能不完整。";

    case ScrError::SaveFrameFailed:         return L"帧保存失败。";
    case ScrError::EnumerationFailed:       return L"枚举失败。";
    case ScrError::ComInitFailed:           return L"COM 初始化失败。";
    default:                                 return L"未知错误。";
    }
}

// Alias for transition convenience
using ErrorCode = ScrError;
