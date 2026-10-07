#include "gui/OverlayTrayController.h"
#include "gui/TranslationWindow.h"
#include "app/OverlayInteractionController.h"
#include <QAction>
#include <QApplication>
#include <QDebug>
#include <QMenu>
#include <QStyle>
#include <QSystemTrayIcon>
#include <QActionGroup>
#include <QComboBox>
#include "config/SettingsManager.h"

OverlayTrayController::OverlayTrayController(OverlayInteractionController &controller,
    TranslationWindow &window, QObject *parent, std::function<bool()> availability, bool showNativeIcon)
    : QObject(parent), controller_(controller), window_(window),
      available_(availability ? availability() : QSystemTrayIcon::isSystemTrayAvailable()),
      menu_(std::make_unique<QMenu>()), icon_(std::make_unique<QSystemTrayIcon>())
{
    icon_->setParent(this);
    controller_.setTrayAvailable(available_);
    auto add = [this](const char *name) {
        auto *action = menu_->addAction(QString());
        action->setObjectName(QString::fromLatin1(name));
        return action;
    };
    visibility_ = add("trayVisibility"); interaction_ = add("trayInteraction"); lock_ = add("trayLock");
    // Toolbar and tray share the same SettingsManager-backed mode selection.
    auto *modeCombo = window_.findChild<QComboBox *>(QStringLiteral("inputModeCombo"));
    auto *modeMenu = menu_->addMenu(tr("Input Mode"));
    auto *group = new QActionGroup(modeMenu);
    for (int i = 0; i < modeCombo->count(); ++i) {
        auto *action = modeMenu->addAction(modeCombo->itemText(i));
        action->setObjectName(QStringLiteral("trayMode%1").arg(i));
        action->setCheckable(true); group->addAction(action); modes_.append(action);
        connect(action, &QAction::triggered, this, [modeCombo, i] { modeCombo->setCurrentIndex(i); });
    }
    connect(modeCombo, &QComboBox::currentIndexChanged, this, &OverlayTrayController::refresh);
    menu_->addSeparator();
    region_ = add("trayRegion"); realtime_ = add("trayRealtime"); settings_ = add("traySettings");
    menu_->addSeparator(); exit_ = add("trayExit");
    connect(visibility_, &QAction::triggered, this, [this] {
        controller_.routeControl(window_.isVisible() ? OverlayControlAction::HideOverlay : OverlayControlAction::ShowOverlay);
    });
    connect(interaction_, &QAction::triggered, this, [this] { controller_.routeControl(OverlayControlAction::ToggleInteraction); });
    connect(lock_, &QAction::triggered, this, [this] { controller_.routeControl(OverlayControlAction::ToggleLock); });
    connect(region_, &QAction::triggered, this, [this] { controller_.routeControl(OverlayControlAction::SelectRegion); });
    connect(realtime_, &QAction::triggered, this, [this] { controller_.routeControl(OverlayControlAction::ToggleRealtime); });
    connect(settings_, &QAction::triggered, this, [this] { controller_.routeControl(OverlayControlAction::OpenSettings); });
    connect(exit_, &QAction::triggered, this, [this] { controller_.routeControl(OverlayControlAction::Exit); });
    connect(&controller_, &OverlayInteractionController::stateChanged, this, &OverlayTrayController::refresh);
    connect(menu_.get(), &QMenu::aboutToShow, this, &OverlayTrayController::refresh);
    connect(icon_.get(), &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::DoubleClick) controller_.routeControl(OverlayControlAction::RestoreInteractive);
    });
    auto icon = QApplication::windowIcon();
    if (icon.isNull()) icon = QApplication::style()->standardIcon(QStyle::SP_ComputerIcon);
    icon_->setIcon(icon);
    icon_->setToolTip(QStringLiteral("Translator"));
    icon_->setContextMenu(menu_.get());
    connect(qApp, &QCoreApplication::aboutToQuit, this, &OverlayTrayController::shutdown);
    if (available_ && showNativeIcon) icon_->show();
    if (!available_) qWarning() << "[OverlayTray] Notification area unavailable; overlay stays recoverable via window/shortcuts.";
    refresh();
}
OverlayTrayController::~OverlayTrayController() { shutdown(); }
void OverlayTrayController::shutdown() { icon_->hide(); controller_.setTrayAvailable(false); }
void OverlayTrayController::refresh()
{
    visibility_->setText(window_.isVisible() ? tr("Hide Overlay") : tr("Show Overlay"));
    const bool through = window_.interactionMode() == OverlayInteractionMode::ClickThrough;
    interaction_->setText(through ? tr("Switch to Interactive") : tr("Enable ClickThrough"));
    lock_->setText(window_.dragLocked() ? tr("Unlock Position") : tr("Lock Position"));
    lock_->setCheckable(true); lock_->setChecked(window_.dragLocked());
    region_->setText(tr("Region")); realtime_->setText(controller_.running() ? tr("Stop") : tr("Start"));
    settings_->setText(tr("Settings")); exit_->setText(tr("Exit"));
    const bool selecting = controller_.selecting();
    for (auto *action : {visibility_, interaction_, lock_, region_, realtime_, settings_}) action->setEnabled(!selecting);
    visibility_->setEnabled(!selecting && available_);
    interaction_->setEnabled(!selecting && (through || controller_.canClickThrough()));
    region_->setEnabled(!selecting && controller_.regionAvailable());
    const auto *combo = window_.findChild<QComboBox *>(QStringLiteral("inputModeCombo"));
    for (int i = 0; i < modes_.size(); ++i) { modes_[i]->setChecked(combo->currentIndex() == i); modes_[i]->setEnabled(!selecting); }
}
