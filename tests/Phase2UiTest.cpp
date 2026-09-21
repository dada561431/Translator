#include <QApplication>
#include <QComboBox>
#include <QCoreApplication>
#include <QPlainTextEdit>
#include <QPoint>
#include <QPixmap>
#include <QPushButton>
#include <QRect>
#include <QSettings>
#include <QStatusBar>
#include <QTemporaryDir>

#include <iostream>

#include "config/SettingsManager.h"
#include "gui/MainWindow.h"

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
Widget *requiredChild(MainWindow &window, const char *objectName)
{
    Widget *widget = window.findChild<Widget *>(QString::fromLatin1(objectName));
    check(widget != nullptr, objectName);
    return widget;
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

    {
        SettingsManager settings;
        MainWindow window(settings);
        window.show();
        application.processEvents();

        auto *source = requiredChild<QComboBox>(window, "sourceLanguageCombo");
        auto *target = requiredChild<QComboBox>(window, "targetLanguageCombo");
        auto *ocr = requiredChild<QComboBox>(window, "ocrEngineCombo");
        auto *translator = requiredChild<QComboBox>(window, "translatorCombo");
        auto *selectRegion = requiredChild<QPushButton>(window, "selectRegionButton");
        auto *start = requiredChild<QPushButton>(window, "startTranslationButton");
        auto *stop = requiredChild<QPushButton>(window, "stopButton");
        auto *original = requiredChild<QPlainTextEdit>(window, "originalTextEdit");
        auto *translation = requiredChild<QPlainTextEdit>(window, "translationTextEdit");

        check(window.windowTitle() == QStringLiteral("Translator"), "window title is Translator");
        check(source && source->currentData().toString() == QStringLiteral("auto"), "source default uses auto ID");
        check(source && source->currentText() == QStringLiteral("Auto Detect"), "source default text is Auto Detect");
        check(source && source->findData(QStringLiteral("zh")) >= 0, "source includes zh ID");
        check(source && source->findData(QStringLiteral("en")) >= 0, "source includes en ID");
        check(source && source->findData(QStringLiteral("ja")) >= 0, "source includes ja ID");
        check(source && source->findData(QStringLiteral("ko")) >= 0, "source includes ko ID");
        check(target && target->currentData().toString() == QStringLiteral("zh"), "target default uses zh ID");
        check(target && target->currentText() == QStringLiteral("Chinese"), "target default text is Chinese");
        check(target && target->findData(QStringLiteral("en")) >= 0, "target includes en ID");
        check(target && target->findData(QStringLiteral("ja")) >= 0, "target includes ja ID");
        check(target && target->findData(QStringLiteral("ko")) >= 0, "target includes ko ID");
        check(ocr && ocr->currentData().toString() == QStringLiteral("windows_ocr"), "OCR default uses windows_ocr ID");
        check(ocr && ocr->currentText() == QStringLiteral("Windows OCR"), "OCR display text is Windows OCR");
        check(translator && translator->currentData().toString() == QStringLiteral("none"), "translator default uses none ID");
        check(translator && translator->currentText() == QStringLiteral("Not Configured"),
              "translator display text is Not Configured");
        check(original && original->isReadOnly(), "original text is read-only");
        check(translation && translation->isReadOnly(), "translation text is read-only");
        check(start && start->isEnabled(), "Start is initially enabled");
        check(stop && !stop->isEnabled(), "Stop is initially disabled");
        check(window.statusBar()->currentMessage() == QStringLiteral("Ready"), "initial status is Ready");

        const QString screenshotPath = qEnvironmentVariable("TRANSLATOR_SCREENSHOT_PATH");
        if (!screenshotPath.isEmpty()) {
            check(window.grab().save(screenshotPath), "UI screenshot is saved");
        }

        if (selectRegion) {
            selectRegion->click();
            check(window.statusBar()->currentMessage() == QStringLiteral("Region selection is not implemented yet."),
                  "Select Region reports unimplemented state");
        }
        if (start && stop) {
            start->click();
            check(!start->isEnabled() && stop->isEnabled(), "Start switches to running button state");
            check(window.statusBar()->currentMessage()
                      == QStringLiteral("Translation UI started. Backend is not implemented yet."),
                  "Start reports backend status");
            stop->click();
            check(start->isEnabled() && !stop->isEnabled(), "Stop restores idle button state");
            check(window.statusBar()->currentMessage() == QStringLiteral("Stopped."), "Stop reports stopped state");
        }

        if (source && target) {
            source->setCurrentIndex(source->findData(QStringLiteral("ja")));
            target->setCurrentIndex(target->findData(QStringLiteral("en")));
            application.processEvents();
        }

        window.resize(640, 480);
        application.processEvents();
        check(original && original->isVisible() && original->width() > 100 && original->height() > 40,
              "original text remains usable after resize");
        check(translation && translation->isVisible() && translation->width() > 100 && translation->height() > 40,
              "translation text remains usable after resize");
        if (original && translation) {
            const QRect originalRect(original->mapTo(&window, QPoint(0, 0)), original->size());
            const QRect translationRect(translation->mapTo(&window, QPoint(0, 0)), translation->size());
            check(!originalRect.intersects(translationRect), "text areas do not overlap after resize");
        }
    }

    {
        QSettings persistedSettings;
        check(persistedSettings.value(QStringLiteral("language/source")).toString() == QStringLiteral("ja"),
              "source ja is stored by stable ID");
        check(persistedSettings.value(QStringLiteral("language/target")).toString() == QStringLiteral("en"),
              "target en is stored by stable ID");

        SettingsManager restoredSettings;
        MainWindow restoredWindow(restoredSettings);
        auto *source = requiredChild<QComboBox>(restoredWindow, "sourceLanguageCombo");
        auto *target = requiredChild<QComboBox>(restoredWindow, "targetLanguageCombo");

        check(source && source->currentData().toString() == QStringLiteral("ja"), "source ja persists across restart");
        check(target && target->currentData().toString() == QStringLiteral("en"), "target en persists across restart");
        check(restoredSettings.ocrEngine() == QStringLiteral("windows_ocr"), "OCR engine persists as windows_ocr");
        check(restoredSettings.translator() == QStringLiteral("none"), "translator persists as none");
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
        check(correctedSettings.ocrEngine() == QStringLiteral("windows_ocr"), "invalid OCR falls back to windows_ocr");
        check(correctedSettings.translator() == QStringLiteral("none"), "invalid translator falls back to none");

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

    if (failures == 0) {
        std::cout << "All Phase 2 UI and settings checks passed.\n";
    }
    return failures == 0 ? 0 : 1;
}
