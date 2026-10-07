#pragma once
#include <QByteArray>
#include <QString>

struct AudioSettings {
    QByteArray microphoneId;
    QByteArray outputId;
    QString modelPath;
    QString language = QStringLiteral("auto");
    bool operator==(const AudioSettings &other) const {
        return microphoneId == other.microphoneId && outputId == other.outputId
            && modelPath == other.modelPath && language == other.language;
    }
};
