#ifndef OPENSNITCH_ICON_THEME_H
#define OPENSNITCH_ICON_THEME_H

#include <ntqpixmap.h>

class TQIconSet;

// Complexity: O(1) average due to cache, O(w*h) on first invert per size
// Dependencies: Config, TQt3 (TQImage/TQPixmap)
// Alignment: none required
// CPU-bound: only on first transform per icon
namespace IconTheme
{
    bool darkModeEnabled();

    TQPixmap loadEmbeddedPixmap(const unsigned char* data, int len, int size);
    TQIconSet loadEmbeddedIconSet(const unsigned char* data, int len, int size);
    void clearCache();
}

#endif // OPENSNITCH_ICON_THEME_H
