#include <QApplication>
#include <QComboBox>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPoint>
#include <QPixmap>
#include <QPushButton>
#include <QRect>
#include <QScreen>
#include <QSettings>
#include <QTemporaryDir>
#include <QWidget>

#include <iostream>

#include "config/SettingsManager.h"
#include "gui/SettingsDialog.h"
#include "gui/TranslationWindow.h"

namespace {

int failures = 0;

void check(bool condition, const char *message)
{
    if (condition) {
        return;
    }

    std::cerr << "FAIL: " << message << '\n';
    ++failures;
}

template<typename Widget>
Widget *requiredChild(QObject &parent, const char *objectName)
{
    Widget *widget = parent.findChild<Widget *>(QString::fromLatin1(objectName));
    check(widget != nullptr, objectName);
    return widget;
}

SettingsDialog *requiredSettingsDialog(QObject &parent)
{
    auto *dialog = parent.findChild<QDialog *>(QStringLiteral("settingsDialog"));
    auto *settingsDialog = dynamic_cast<SettingsDialog *>(dialog);
    check(settingsDialog != nullptr, "settingsDialog");
    return settingsDialog;
}

bool intersectsAvailableScreen(const QWidget &window)
{
    for (const QScreen *screen : QGuiApplication::screens()) {
        if (screen->availableGeometry().intersects(window.frameGeometry())) {
            return true;
        }
    }
    return false;
}

} // namespace

int main(int argc, char *argv[])
{
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));
    }

    QApplication application(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("TranslatorProjectTests"));
    QCoreApplication::setApplicationName(QStringLiteral("TranslatorPhase2Tests"));

    QTemporaryDir settingsDirectory;
    check(settingsDirectory.isValid(), "temporary settings directory is valid");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDirectory.path());

    {
        QSettings cleanSettings;
        cleanSettings.clear();
        cleanSettings.sync();
    }

    QRect savedWindowGeometry;
    {
        SettingsManager settings;
        check(settings.sourceLanguage() == QStringLiteral("auto"), "source defaults to auto");
        check(settings.targetLanguage() == QStringLiteral("zh"), "target defaults to zh");
        check(settings.ocrEngine() == QStringLiteral("windows_ocr"), "OCR defaults to windows_ocr");
        check(settings.translator() == QStringLiteral("none"), "translator defaults to none");

        TranslationWindow window(settings);
        window.show();
        application.processEvents();

        check(window.isWindow(), "TranslationWindow is a top-level window");
        check(window.windowTitle() == QStringLiteral("Translator"), "window title is Translator");
        check(window.windowFlags().testFlag(Qt::FramelessWindowHint), "frameless flag is set");
        check(window.windowFlags().testFlag(Qt::WindowStaysOnTopHint), "always-on-top flag is set");
        check(window.testAttribute(Qt::WA_TranslucentBackground), "translucent background is enabled");

        auto *toolbar = requiredChild<QWidget>(window, "toolbar");
        auto *region = requiredChild<QPushButton>(window, "regionButton");
        auto *start = requiredChild<QPushButton>(window, "startButton");
        auto *stop = requiredChild<QPushButton>(window, "stopButton");
        auto *settingsButton = requiredChild<QPushButton>(window, "settingsButton");
        auto *closeButton = requiredChild<QPushButton>(window, "closeButton");
        auto *original = requiredChild<QPlainTextEdit>(window, "originalTextEdit");
        auto *translation = requiredChild<QPlainTextEdit>(window, "translationTextEdit");
        auto *status = requiredChild<QLabel>(window, "statusLabel");

        check(toolbar && toolbar->isVisible(), "toolbar is visible");
        check(region && region->isEnabled(), "Region is initially enabled");
        check(start && start->isEnabled(), "Start is initially enabled");
        check(stop && !stop->isEnabled(), "Stop is initially disabled");
        check(original && original->isReadOnly(), "original text is read-only");
        check(translation && translation->isReadOnly(), "translation text is read-only");
        check(status && status->text() == QStringLiteral("Ready"), "initial status is Ready");

        window.setOriginalText(QStringLiteral("original sample"));
        window.setTranslatedText(QStringLiteral("translated sample"));
        check(original && original->toPlainText() == QStringLiteral("original sample"),
              "original text display API works");
        check(translation && translation->toPlainText() == QStringLiteral("translated sample"),
              "translated text display API works");

        if (region && status) {
            region->click();
            check(status->text() == QStringLiteral("Region selection is not implemented yet."),
                  "Region reports unimplemented state");
        }
        if (start && stop && status) {
            start->click();
            check(!start->isEnabled() && stop->isEnabled(), "Start switches to running state");
            check(status->text()
                      == QStringLiteral("Translation UI started. Backend is not implemented yet."),
                  "Start reports backend status");
            stop->click();
            check(start->isEnabled() && !stop->isEnabled(), "Stop restores idle state");
            check(status->text() == QStringLiteral("Stopped."), "Stop reports stopped state");
        }

        SettingsDialog *settingsDialog = requiredSettingsDialog(window);
        if (settingsButton && settingsDialog) {
            settingsButton->click();
            application.processEvents();
            check(settingsDialog->isVisible(), "Settings button opens SettingsDialog");
            check(settingsDialog->isWindow(), "SettingsDialog is a window");

            auto *source = requiredChild<QComboBox>(*settingsDialog, "sourceLanguageCombo");
            auto *target = requiredChild<QComboBox>(*settingsDialog, "targetLanguageCombo");
            auto *ocr = requiredChild<QComboBox>(*settingsDialog, "ocrEngineCombo");
            auto *translator = requiredChild<QComboBox>(*settingsDialog, "translatorCombo");

            check(source && source->currentData().toString() == QStringLiteral("auto"),
                  "SettingsDialog loads source default");
            check(source && source->findData(QStringLiteral("zh")) >= 0, "source includes zh ID");
            check(source && source->findData(QStringLiteral("en")) >= 0, "source includes en ID");
            check(source && source->findData(QStringLiteral("ja")) >= 0, "source includes ja ID");
            check(source && source->findData(QStringLiteral("ko")) >= 0, "source includes ko ID");
            check(target && target->currentData().toString() == QStringLiteral("zh"),
                  "SettingsDialog loads target default");
            check(target && target->findData(QStringLiteral("en")) >= 0, "target includes en ID");
            check(target && target->findData(QStringLiteral("ja")) >= 0, "target includes ja ID");
            check(target && target->findData(QStringLiteral("ko")) >= 0, "target includes ko ID");
            check(ocr && ocr->currentData().toString() == QStringLiteral("windows_ocr"),
                  "OCR uses windows_ocr ID");
            check(translator && translator->currentData().toString() == QStringLiteral("none"),
                  "translator uses none ID");

            if (source && target) {
                source->setCurrentIndex(source->findData(QStringLiteral("ja")));
                target->setCurrentIndex(target->findData(QStringLiteral("en")));
                application.processEvents();
            }

            settingsDialog->close();
            settingsButton->click();
            application.processEvents();
            check(settingsDialog->isVisible(), "the same SettingsDialog can reopen");
            check(requiredSettingsDialog(window) == settingsDialog,
                  "SettingsDialog remains a single instance");
            settingsDialog->close();
        }

        window.resize(680, 440);
        window.move(40, 50);
        application.processEvents();
        savedWindowGeometry = window.geometry();

        if (original && translation) {
            const QRect originalRect(original->mapTo(&window, QPoint(0, 0)), original->size());
            const QRect translationRect(translation->mapTo(&window, QPoint(0, 0)), translation->size());
            check(original->width() > 100 && original->height() > 40,
                  "original text remains usable after resize");
            check(translation->width() > 100 && translation->height() > 40,
                  "translation text remains usable after resize");
            check(!originalRect.intersects(translationRect), "text areas do not overlap after resize");
        }

        const QString screenshotPath = qEnvironmentVariable("TRANSLATOR_SCREENSHOT_PATH");
        if (!screenshotPath.isEmpty()) {
            check(window.grab().save(screenshotPath), "UI screenshot is saved");
        }

        if (closeButton) {
            closeButton->click();
            application.processEvents();
            check(!window.isVisible(), "Close button closes TranslationWindow");
        }
    }

    {
        QSettings persistedSettings;
        check(persistedSettings.value(QStringLiteral("language/source")).toString() == QStringLiteral("ja"),
              "source ja is stored by stable ID");
        check(persistedSettings.value(QStringLiteral("language/target")).toString() == QStringLiteral("en"),
              "target en is stored by stable ID");
        check(!persistedSettings.value(QStringLiteral("window/geometry")).toByteArray().isEmpty(),
              "window geometry is stored");

        SettingsManager restoredSettings;
        TranslationWindow restoredWindow(restoredSettings);
        restoredWindow.show();
        application.processEvents();

        check(restoredSettings.sourceLanguage() == QStringLiteral("ja"), "source ja persists across restart");
        check(restoredSettings.targetLanguage() == QStringLiteral("en"), "target en persists across restart");
        check(restoredSettings.ocrEngine() == QStringLiteral("windows_ocr"),
              "OCR engine persists as windows_ocr");
        check(restoredSettings.translator() == QStringLiteral("none"), "translator persists as none");
        check(restoredWindow.size() == savedWindowGeometry.size(), "window size restores across restart");
        check(restoredWindow.pos() == savedWindowGeometry.topLeft(), "window position restores across restart");

        auto *settingsButton = requiredChild<QPushButton>(restoredWindow, "settingsButton");
        auto *settingsDialog = requiredSettingsDialog(restoredWindow);
        if (settingsButton && settingsDialog) {
            settingsButton->click();
            application.processEvents();
            auto *source = requiredChild<QComboBox>(*settingsDialog, "sourceLanguageCombo");
            auto *target = requiredChild<QComboBox>(*settingsDialog, "targetLanguageCombo");
            check(source && source->currentData().toString() == QStringLiteral("ja"),
                  "SettingsDialog restores Japanese");
            check(target && target->currentData().toString() == QStringLiteral("en"),
                  "SettingsDialog restores English");
            settingsDialog->close();
        }
        restoredWindow.close();
    }

    {
        QSettings invalidSettings;
        invalidSettings.setValue(QStringLiteral("language/source"), QStringLiteral("invalid"));
        invalidSettings.setValue(QStringLiteral("language/target"), QStringLiteral("invalid"));
        invalidSettings.setValue(QStringLiteral("ocr/engine"), QStringLiteral("removed"));
        invalidSettings.setValue(QStringLiteral("translator/engine"), QStringLiteral("removed"));
        invalidSettings.sync();

        SettingsManager correctedSettings;
        check(correctedSettings.sourceLanguage() == QStringLiteral("auto"), "invalid source falls back to auto");
        check(correctedSettings.targetLanguage() == QStringLiteral("zh"), "invalid target falls back to zh");
        check(correctedSettings.ocrEngine() == QStringLiteral("windows_ocr"),
              "invalid OCR falls back to windows_ocr");
        check(correctedSettings.translator() == QStringLiteral("none"),
              "invalid translator falls back to none");

        QSettings storedSettings;
        check(storedSettings.value(QStringLiteral("language/source")).toString() == QStringLiteral("auto"),
              "corrected source is written back");
        check(storedSettings.value(QStringLiteral("language/target")).toString() == QStringLiteral("zh"),
              "corrected target is written back");
        check(storedSettings.value(QStringLiteral("ocr/engine")).toString() == QStringLiteral("windows_ocr"),
              "corrected OCR engine is written back");
        check(storedSettings.value(QStringLiteral("translator/engine")).toString() == QStringLiteral("none"),
              "corrected translator is written back");
    }

    {
        SettingsManager geometrySettings;
        QWidget offscreenGeometrySource(nullptr, Qt::Window | Qt::FramelessWindowHint);
        offscreenGeometrySource.resize(600, 400);
        offscreenGeometrySource.move(5000, 5000);
        geometrySettings.setWindowGeometry(offscreenGeometrySource.saveGeometry());

        TranslationWindow fallbackWindow(geometrySettings);
        fallbackWindow.show();
        application.processEvents();
        check(intersectsAvailableScreen(fallbackWindow), "off-screen geometry falls back to a visible screen");
        fallbackWindow.close();
    }

    if (failures == 0) {
        std::cout << "All Phase 2.5 UI and settings checks passed.\n";
    }
    return failures == 0 ? 0 : 1;
}
