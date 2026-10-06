#pragma once

#include <QObject>
#include <QString>
#include <QAbstractNativeEventFilter>
#include <array>
#include <functional>

class GlobalShortcutManager final : public QObject, public QAbstractNativeEventFilter
{
    Q_OBJECT
public:
    enum class Action { ToggleInteraction, SelectRegion, ToggleRealtime };
    struct Backend {
        std::function<bool(int id, unsigned key, QString &error)> registerKey;
        std::function<void(int id)> unregisterKey;
    };
    explicit GlobalShortcutManager(QObject *parent = nullptr, Backend backend = {});
    ~GlobalShortcutManager() override;
    void registerDefaults();
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
    std::array<bool, 3> registered_{};
    bool filterInstalled_ = false;
};
