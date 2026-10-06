#pragma once
#include <QKeySequence>
#include <QString>
#include <array>

struct GlobalHotkeyConfig {
    std::array<QString, 3> shortcuts{{QStringLiteral("Ctrl+Alt+T"), QStringLiteral("Ctrl+Alt+R"), QStringLiteral("Ctrl+Alt+S")}};
};
struct GlobalHotkeyChord {
    unsigned modifiers = 0, key = 0;
    bool operator==(const GlobalHotkeyChord &other) const { return modifiers == other.modifiers && key == other.key; }
};
// Win32-compatible chord values without a Windows header dependency.
inline bool parseGlobalHotkeys(const GlobalHotkeyConfig &input, GlobalHotkeyConfig &canonical,
                              std::array<GlobalHotkeyChord, 3> &chords, QString &error)
{
    for (int i = 0; i < 3; ++i) {
        const QKeySequence sequence(input.shortcuts[i], QKeySequence::PortableText);
        if (sequence.count() != 1 || sequence.isEmpty()) {
            error = QStringLiteral("Each shortcut must contain exactly one key combination."); return false;
        }
        const auto modifiers = sequence[0].keyboardModifiers();
        const auto allowed = Qt::ControlModifier | Qt::AltModifier | Qt::ShiftModifier | Qt::MetaModifier;
        if (!modifiers || (modifiers & ~allowed)) {
            error = QStringLiteral("Use Ctrl, Alt, Shift or Win with a supported key."); return false;
        }
        unsigned key = 0;
        const int qtKey = sequence[0].key();
        if ((qtKey >= Qt::Key_A && qtKey <= Qt::Key_Z) || (qtKey >= Qt::Key_0 && qtKey <= Qt::Key_9)) key = unsigned(qtKey);
        else if (qtKey >= Qt::Key_F1 && qtKey <= Qt::Key_F24) key = 0x70 + qtKey - Qt::Key_F1;
        else switch (qtKey) {
        case Qt::Key_Space: key = 0x20; break;
        case Qt::Key_Tab: key = 0x09; break;
        case Qt::Key_Return: key = 0x0d; break;
        case Qt::Key_Escape: key = 0x1b; break;
        case Qt::Key_Backspace: key = 0x08; break;
        case Qt::Key_Insert: key = 0x2d; break;
        case Qt::Key_Delete: key = 0x2e; break;
        case Qt::Key_Home: key = 0x24; break;
        case Qt::Key_End: key = 0x23; break;
        case Qt::Key_PageUp: key = 0x21; break;
        case Qt::Key_PageDown: key = 0x22; break;
        case Qt::Key_Left: key = 0x25; break;
        case Qt::Key_Up: key = 0x26; break;
        case Qt::Key_Right: key = 0x27; break;
        case Qt::Key_Down: key = 0x28; break;
        default: break;
        }
        if (!key) { error = QStringLiteral("Shortcut needs a supported key, not modifiers alone."); return false; }
        chords[i] = {unsigned((modifiers.testFlag(Qt::AltModifier) ? 1 : 0)
            | (modifiers.testFlag(Qt::ControlModifier) ? 2 : 0) | (modifiers.testFlag(Qt::ShiftModifier) ? 4 : 0)
            | (modifiers.testFlag(Qt::MetaModifier) ? 8 : 0)), key};
        canonical.shortcuts[i] = sequence.toString(QKeySequence::PortableText);
        for (int j = 0; j < i; ++j) if (chords[j] == chords[i]) {
            error = QStringLiteral("The three shortcuts must be different."); return false;
        }
    }
    error.clear(); return true;
}
