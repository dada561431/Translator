#pragma once

#include <QString>
#include <functional>

class QWidget;

// Best-effort display affinity, independent from mouse passthrough.
class WindowCaptureExclusion final
{
public:
    enum class Status { AwaitingWindow, Unsupported, Applied, Failed };
    struct Backend {
        std::function<bool()> supported;
        std::function<bool(quintptr handle, bool excluded, QString &error)> apply;
    };
    explicit WindowCaptureExclusion(Backend backend = {});
    static bool platformSupported();
    bool supported() const;
    bool setExcluded(QWidget *window, bool excluded);
    bool setExcludedHandle(quintptr handle, bool excluded);
    Status status() const { return status_; }
    QString lastError() const { return error_; }

private:
    Backend backend_;
    Status status_ = Status::AwaitingWindow;
    QString error_;
};
