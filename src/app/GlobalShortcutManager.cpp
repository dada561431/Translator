#include "app/GlobalShortcutManager.h"
#include <QCoreApplication>
#include <QDebug>
#include <utility>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

GlobalShortcutManager::GlobalShortcutManager(QObject *parent, Backend backend)
    : QObject(parent), backend_(std::move(backend))
{
    if ((!backend_.registerChord && !backend_.registerKey) || !backend_.unregisterKey) {
        backend_.registerChord = [](int id, unsigned modifiers, unsigned key, QString &error) {
#ifdef Q_OS_WIN
            if (RegisterHotKey(nullptr, id, modifiers | MOD_NOREPEAT, key)) return true;
            error = QStringLiteral("RegisterHotKey failed (Win32 error %1)").arg(GetLastError());
#else
            Q_UNUSED(id)
            Q_UNUSED(modifiers)
            Q_UNUSED(key)
            error = QStringLiteral("Global shortcuts are only supported on Windows");
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
bool GlobalShortcutManager::registerChord(int id, const GlobalHotkeyChord &chord, QString &error)
{
    if (backend_.registerChord) return backend_.registerChord(id, chord.modifiers, chord.key, error);
    if (chord.modifiers == 3) return backend_.registerKey(id, chord.key, error);
    error = QStringLiteral("Legacy shortcut backend does not support these modifiers"); return false;
}
GlobalShortcutManager::~GlobalShortcutManager() { unregisterAll(); }
int GlobalShortcutManager::hotkeyId(Action action) { return 0x6a01 + int(action); }
int GlobalShortcutManager::registeredId(Action action) const {
    const int index = int(action);
    return index >= 0 && index < 3 ? registrations_[index].id : 0;
}
void GlobalShortcutManager::registerDefaults() { registerConfigured({}); }
void GlobalShortcutManager::registerConfigured(const GlobalHotkeyConfig &input)
{
    unregisterAll();
    std::array<GlobalHotkeyChord, 3> chords;
    QString error;
    GlobalHotkeyConfig canonical;
    if (!parseGlobalHotkeys(input, canonical, chords, error)) {
        qWarning().noquote() << "[GlobalShortcut] Invalid stored settings; runtime defaults:" << error;
        parseGlobalHotkeys({}, canonical, chords, error);
    }
    config_ = canonical;
    QCoreApplication::instance()->installNativeEventFilter(this);
    filterInstalled_ = true;
    for (int i = 0; i < 3; ++i) {
        bool ok = registerChord(hotkeyId(Action(i)), chords[i], error);
        if (!ok) {
            qWarning().noquote() << "[GlobalShortcut]" << config_.shortcuts[i] << error;
            emit registrationFailed(Action(i), QStringLiteral("Shortcut %1 is unavailable or already in use.").arg(config_.shortcuts[i]));
            if (i == 0 && config_.shortcuts[0] != GlobalHotkeyConfig{}.shortcuts[0]) {
                const GlobalHotkeyChord fallback{3, 'T'};
                ok = registerChord(hotkeyId(Action(i)), fallback, error);
                if (ok) {
                    chords[i] = fallback;
                    config_.shortcuts[i] = GlobalHotkeyConfig{}.shortcuts[i];
                    qInfo() << "[GlobalShortcut] Runtime recovery fallback Ctrl+Alt+T";
                } else qWarning().noquote() << "[GlobalShortcut] Recovery fallback failed:" << error;
            }
        }
        if (ok) registrations_[i] = {hotkeyId(Action(i)), chords[i]};
    }
}
bool GlobalShortcutManager::replace(const GlobalHotkeyConfig &input, QString &error,
                                   std::function<void(const GlobalHotkeyConfig &)> persist)
{
    GlobalHotkeyConfig canonical;
    std::array<GlobalHotkeyChord, 3> chords;
    if (!parseGlobalHotkeys(input, canonical, chords, error)) return false;
    std::array<Registration, 3> staged{};
    std::array<bool, 3> newlyOwned{};
    for (int i = 0; i < 3; ++i) {
        for (const auto &old : registrations_) if (old.id && old.chord == chords[i]) staged[i] = old;
        if (staged[i].id) continue; // Reuse unchanged chords and permutations without self-conflicts.
        int id = 0;
        for (int attempts = 0; attempts < 0x3000; ++attempts) {
            if (nextId_ > 0x9fff) nextId_ = 0x6a04;
            const int candidate = nextId_++;
            bool used = false;
            for (const auto &old : registrations_) used |= old.id == candidate;
            for (const auto &item : staged) used |= item.id == candidate;
            if (!used) { id = candidate; break; }
        }
        QString diagnostic;
        if (!id || !registerChord(id, chords[i], diagnostic)) {
            qWarning().noquote() << "[GlobalShortcut] Replacement failed:" << canonical.shortcuts[i] << diagnostic;
            for (int j = 0; j < i; ++j) if (newlyOwned[j]) backend_.unregisterKey(staged[j].id);
            error = QStringLiteral("Shortcut %1 is unavailable or already in use. Previous shortcuts remain active.").arg(canonical.shortcuts[i]);
            return false;
        }
        staged[i] = {id, chords[i]}; newlyOwned[i] = true;
    }
    const auto old = registrations_;
    registrations_ = staged;
    config_ = canonical;
    if (!filterInstalled_ && QCoreApplication::instance()) {
        QCoreApplication::instance()->installNativeEventFilter(this);
        filterInstalled_ = true;
    }
    if (persist) persist(config_);
    for (const auto &item : old) {
        bool retained = false;
        for (const auto &replacement : staged) retained |= item.id == replacement.id;
        if (item.id && !retained) backend_.unregisterKey(item.id);
    }
    error.clear(); return true;
}
void GlobalShortcutManager::unregisterAll()
{
    for (auto &item : registrations_) { if (item.id) backend_.unregisterKey(item.id); item = {}; }
    if (filterInstalled_ && QCoreApplication::instance()) QCoreApplication::instance()->removeNativeEventFilter(this);
    filterInstalled_ = false;
}
bool GlobalShortcutManager::isRegistered(Action action) const { return registeredId(action) != 0; }
bool GlobalShortcutManager::dispatchHotkey(int id)
{
    if (!id) return false;
    for (int i = 0; i < 3; ++i) if (registrations_[i].id == id) { emit activated(Action(i)); return true; }
    return false;
}
bool GlobalShortcutManager::nativeEventFilter(const QByteArray &type, void *message, qintptr *result)
{
#ifdef Q_OS_WIN
    if ((type != "windows_generic_MSG" && type != "windows_dispatcher_MSG") || !message) return false;
    const auto *native = static_cast<MSG *>(message);
    if (native->message != WM_HOTKEY || native->hwnd) return false;
    for (const auto &item : registrations_) {
        if (item.id && native->wParam == WPARAM(item.id) && HIWORD(native->lParam) == item.chord.key
            && LOWORD(native->lParam) == item.chord.modifiers && dispatchHotkey(item.id)) {
            if (result) *result = 0; return true;
        }
    }
#else
    Q_UNUSED(type)
    Q_UNUSED(message)
    Q_UNUSED(result)
#endif
    return false;
}
