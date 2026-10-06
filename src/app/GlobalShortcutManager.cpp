#include "app/GlobalShortcutManager.h"
#include <QCoreApplication>
#include <QDebug>
#include <utility>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

namespace {
constexpr std::array<unsigned, 3> keys{'T', 'R', 'S'};
constexpr int firstId = 0x6a01;
}

GlobalShortcutManager::GlobalShortcutManager(QObject *parent, Backend backend)
    : QObject(parent), backend_(std::move(backend))
{
    if (!backend_.registerKey || !backend_.unregisterKey) {
        backend_.registerKey = [](int id, unsigned key, QString &error) {
#ifdef Q_OS_WIN
            if (RegisterHotKey(nullptr, id, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, key)) return true;
            error = QStringLiteral("RegisterHotKey failed (Win32 error %1)").arg(GetLastError());
#else
            Q_UNUSED(id)
            Q_UNUSED(key)
            error = QStringLiteral("System-wide shortcuts are only supported on Windows");
#endif
            return false;
        };
        backend_.unregisterKey = [](int id) {
#ifdef Q_OS_WIN
            UnregisterHotKey(nullptr, id);
#else
            Q_UNUSED(id)
#endif
        };
    }
}

GlobalShortcutManager::~GlobalShortcutManager() { unregisterAll(); }

int GlobalShortcutManager::hotkeyId(Action action) { return firstId + int(action); }

void GlobalShortcutManager::registerDefaults()
{
    unregisterAll();
    QCoreApplication::instance()->installNativeEventFilter(this);
    filterInstalled_ = true;
    for (int index = 0; index < int(keys.size()); ++index) {
        QString error;
        registered_[index] = backend_.registerKey(firstId + index, keys[index], error);
        const QString diagnostic = QStringLiteral("Ctrl+Alt+%1: %2")
            .arg(QChar(keys[index])).arg(registered_[index] ? QStringLiteral("registered") : error);
        if (registered_[index]) qInfo().noquote() << "[GlobalShortcut]" << diagnostic;
        else {
            qWarning().noquote() << "[GlobalShortcut]" << diagnostic;
            emit registrationFailed(Action(index), diagnostic);
        }
    }
}

void GlobalShortcutManager::unregisterAll()
{
    for (int index = 0; index < int(keys.size()); ++index) {
        if (registered_[index]) backend_.unregisterKey(firstId + index);
        registered_[index] = false;
    }
    if (filterInstalled_ && QCoreApplication::instance())
        QCoreApplication::instance()->removeNativeEventFilter(this);
    filterInstalled_ = false;
}

bool GlobalShortcutManager::isRegistered(Action action) const
{
    const int index = int(action);
    return index >= 0 && index < int(keys.size()) && registered_[index];
}

bool GlobalShortcutManager::dispatchHotkey(int id)
{
    if (id < firstId || id >= firstId + int(keys.size()) || !registered_[id - firstId]) return false;
    emit activated(Action(id - firstId));
    return true;
}

bool GlobalShortcutManager::nativeEventFilter(const QByteArray &type, void *message, qintptr *result)
{
#ifdef Q_OS_WIN
    if ((type != "windows_generic_MSG" && type != "windows_dispatcher_MSG") || !message) return false;
    auto *native = static_cast<MSG *>(message);
    const int id = int(native->wParam);
    if (native->message != WM_HOTKEY || native->hwnd || id < firstId || id >= firstId + int(keys.size())
        || HIWORD(native->lParam) != keys[id - firstId]
        || LOWORD(native->lParam) != (MOD_CONTROL | MOD_ALT)) return false;
    if (dispatchHotkey(id)) {
        if (result) *result = 0;
        return true;
    }
#else
    Q_UNUSED(type)
    Q_UNUSED(message)
    Q_UNUSED(result)
#endif
    return false;
}
