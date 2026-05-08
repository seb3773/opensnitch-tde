#ifndef TQTCOMMON_P_H
#define TQTCOMMON_P_H

#include <ntqstring.h>

/**
 * @brief Case-insensitive substring search without TQString allocation.
 * @param hay    The string to search in.
 * @param needle The substring to find. MUST already be lowercase.
 * @return true if hay contains needle (case-insensitive).
 *
 * Avoids creating a lowercase copy of hay (unlike TQString::lower().contains()).
 * Each character is lowered inline during comparison.
 */
static inline bool containsCI(const TQString& hay, const TQString& needle)
{
    if (needle.isEmpty()) return true;
    const int hlen = (int)hay.length(), nlen = (int)needle.length();
    if (nlen > hlen) return false;
    const int lim = hlen - nlen;
    for (int i = 0; i <= lim; ++i) {
        int j = 0;
        while (j < nlen && hay[i + j].lower() == needle[j]) ++j;
        if (j == nlen) return true;
    }
    return false;
}

#endif // TQTCOMMON_P_H
