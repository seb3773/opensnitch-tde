#include "icon_theme.h"

#include "config.h"
#include "embedded_icons.h"

#include <ntqimage.h>
#include <ntqiconset.h>
#include <ntqcolor.h>

#include <map>

namespace IconTheme
{
    struct CacheKey {
        const unsigned char* data;
        int len;
        int size;
        int invert;

        bool operator<(const CacheKey& o) const {
            if (data != o.data) return data < o.data;
            if (len != o.len) return len < o.len;
            if (size != o.size) return size < o.size;
            return invert < o.invert;
        }
    };

    static std::map<CacheKey, TQPixmap> s_cache;

    bool darkModeEnabled()
    {
        Config* cfg = Config::get();
        if (!cfg)
            return false;
        return cfg->getBool(Config::KEY_UI_DARK_MODE, false);
    }

    static inline int shouldInvertForIcon(const unsigned char* data, int len)
    {
        (void)data;
        if (!darkModeEnabled())
            return 0;
        // embedded_icons.h defines icons as static arrays, so pointer identity
        // differs across translation units. Decide exceptions by length.
        if (len == (int)icon_alert_png_len)
            return 0;
        if (len == (int)quickhelp_png_len)
            return 0;
        if (len == (int)about_opensnitchtde_png_len)
            return 0;
        return 1;
    }

    static inline void invert_rgb_keep_alpha(TQImage* img)
    {
        if (!img)
            return;

        if (img->depth() != 32)
            *img = img->convertDepth(32);

        const int w = img->width();
        const int h = img->height();
        if (w <= 0 || h <= 0)
            return;

        for (int y = 0; y < h; ++y) {
            unsigned int* p = (unsigned int*)img->scanLine(y);
            for (int x = 0; x < w; ++x) {
                const unsigned int px = p[x];
                const unsigned int a = px & 0xff000000U;
                const unsigned int rgb = px & 0x00ffffffU;
                p[x] = a | (~rgb & 0x00ffffffU);
            }
        }
    }

    TQPixmap loadEmbeddedPixmap(const unsigned char* data, int len, int size)
    {
        if (!data || len <= 0)
            return TQPixmap();

        const int invert = shouldInvertForIcon(data, len);
        const CacheKey k = { data, len, size, invert };

        std::map<CacheKey, TQPixmap>::const_iterator it = s_cache.find(k);
        if (it != s_cache.end())
            return it->second;

        TQImage img;
        if (!img.loadFromData(data, len))
            return TQPixmap();

        if (size > 0)
            img = img.smoothScale(size, size);

        if (invert)
            invert_rgb_keep_alpha(&img);

        TQPixmap pm;
        pm.convertFromImage(img, TQt::AutoColor);
        s_cache.insert(std::make_pair(k, pm));
        return pm;
    }

    TQIconSet loadEmbeddedIconSet(const unsigned char* data, int len, int size)
    {
        return TQIconSet(loadEmbeddedPixmap(data, len, size), TQIconSet::Small);
    }

    void clearCache()
    {
        s_cache.clear();
    }
}
