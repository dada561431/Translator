#pragma once

#include <QtGlobal>

struct OverlayAppearance
{
    qreal translationFontSize = 16;
    qreal originalFontSize = 12;
    int backgroundOpacity = 0;
    bool showTranslation = true;
    bool showOriginal = true;

    bool operator==(const OverlayAppearance &other) const
    {
        return translationFontSize == other.translationFontSize
            && originalFontSize == other.originalFontSize
            && backgroundOpacity == other.backgroundOpacity
            && showTranslation == other.showTranslation && showOriginal == other.showOriginal;
    }
};
