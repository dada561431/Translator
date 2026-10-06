#pragma once

#include <QObject>
#include <QString>
#include <QAbstractNativeEventFilter>
#include <array>
#include <functional>
#include "config/GlobalHotkeyConfig.h"

class GlobalShortcutManager final : public QObject, public QAbstractNativeEventFilter
{
    Q_OBJECT
public:
    enum class Action { ToggleInteraction, SelectRegion, ToggleRealtime };
    struct Backend {
        std::function<bool(int id, unsigned key, QString &error)> registerKey;
        std::function<void(int id)> unregisterKey;
        std::function<bool(int id, unsigned modifiers, unsigned key, QString &error)> registerChord;
    };
    explicit GlobalShortcutManager(QObject *parent = nullptr, Backend backend = {});
    ~GlobalShortcutManager() override;
    void registerDefaults();
    void registerConfigured(const GlobalHotkeyConfig &config);
    bool replace(const GlobalHotkeyConfig &config, QString &error,
                 std::function<void(const GlobalHotkeyConfig &)> persist = {});
    GlobalHotkeyConfig configuration() const { return config_; }
    int registeredId(Action action) const;
    void unregisterAll();
    bool isRegistered(Action action) const;
    bool dispatchHotkey(int id);
    bool nativeEventFilter(const QByteArray &type, void *message, qintptr *result) override;
    static int hotkeyId(Action action);

signals:
    void activated(GlobalShortcutManager::Action action);
    void registrationFailed(GlobalShortcutManager::Action action, const QString &diagnostic);

private:
    Backend backend_;
    struct Registration { int id = 0; GlobalHotkeyChord chord; };
    std::array<Registration, 3> registrations_{};
    GlobalHotkeyConfig config_;
    int nextId_ = 0x6a04;
    bool registerChord(int id, const GlobalHotkeyChord &chord, QString &error);
    bool filterInstalled_ = false;
};
