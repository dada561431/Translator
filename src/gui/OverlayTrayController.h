#pragma once
#include <QObject>
#include <functional>
#include <memory>
class QAction;
class QMenu;
class QSystemTrayIcon;
class TranslationWindow;
class OverlayInteractionController;

class OverlayTrayController final : public QObject
{
public:
    OverlayTrayController(OverlayInteractionController &controller, TranslationWindow &window,
        QObject *parent = nullptr, std::function<bool()> availability = {}, bool showNativeIcon = true);
    ~OverlayTrayController() override;
    bool available() const { return available_; }
    QMenu *menu() const { return menu_.get(); }
    void refresh();
    void shutdown();
private:
    OverlayInteractionController &controller_;
    TranslationWindow &window_;
    bool available_ = false;
    std::unique_ptr<QMenu> menu_;
    std::unique_ptr<QSystemTrayIcon> icon_;
    QAction *visibility_, *interaction_, *lock_, *region_, *realtime_, *settings_, *exit_;
};
