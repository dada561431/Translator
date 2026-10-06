#include "platform/WindowCaptureExclusion.h"
#include <QWidget>
#include <utility>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

bool WindowCaptureExclusion::platformSupported()
{
#ifdef Q_OS_WIN
    // RtlGetVersion avoids the legacy application-manifest version lie.
    using VersionFunction = LONG (WINAPI *)(OSVERSIONINFOW *);
    const auto module = GetModuleHandleW(L"ntdll.dll");
    const auto version = module ? reinterpret_cast<VersionFunction>(
        GetProcAddress(module, "RtlGetVersion")) : nullptr;
    OSVERSIONINFOW info{};
    info.dwOSVersionInfoSize = sizeof(info);
    return version && version(&info) == 0
        && (info.dwMajorVersion > 10 || (info.dwMajorVersion == 10 && info.dwBuildNumber >= 19041));
#else
    return false;
#endif
}

WindowCaptureExclusion::WindowCaptureExclusion(Backend backend) : backend_(std::move(backend))
{
    if (!backend_.supported) backend_.supported = &platformSupported;
    if (!backend_.apply) backend_.apply = [](quintptr handle, bool excluded, QString &error) {
#ifdef Q_OS_WIN
        constexpr DWORD excludeFromCapture = 0x00000011;
        if (SetWindowDisplayAffinity(reinterpret_cast<HWND>(handle), excluded ? excludeFromCapture : WDA_NONE))
            return true;
        error = QStringLiteral("SetWindowDisplayAffinity failed (Win32 error %1)").arg(GetLastError());
#else
        Q_UNUSED(handle)
        Q_UNUSED(excluded)
        error = QStringLiteral("Windows capture exclusion unsupported");
#endif
        return false;
    };
}

bool WindowCaptureExclusion::supported() const { return backend_.supported(); }

bool WindowCaptureExclusion::setExcluded(QWidget *window, bool excluded)
{
    // effectiveWinId never forces HWND creation; show/WinIdChange will retry.
    return setExcludedHandle(window && window->isWindow() ? quintptr(window->effectiveWinId()) : 0, excluded);
}

bool WindowCaptureExclusion::setExcludedHandle(quintptr handle, bool excluded)
{
    error_.clear();
    if (!supported()) {
        status_ = Status::Unsupported;
        return false;
    }
    if (!handle) {
        status_ = Status::AwaitingWindow;
        return false;
    }
    const bool success = backend_.apply(handle, excluded, error_);
    status_ = success ? Status::Applied : Status::Failed;
    return success;
}
