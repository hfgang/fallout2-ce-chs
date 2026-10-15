#include "word_wrap.h"

#include <ctype.h>
#include <stddef.h>
#include <string.h>

#include "text_font.h"

namespace fallout {

// 0x4BC6F0 word_wrap
//
// NOTE: This is the legacy (single-byte, English-oriented) implementation. It
// is exposed as [leagcyWordWrap] and wired into font managers that do not
// provide their own wrapping. Callers should use the [wordWrap] function
// pointer (see text_font.h) which resolves to the current font manager's
// implementation.
int leagcyWordWrap(const char* string, int width, short* breakpoints, short* breakpointsLengthPtr)
{
    breakpoints[0] = 0;
    *breakpointsLengthPtr = 1;

    for (int index = 1; index < WORD_WRAP_MAX_COUNT; index++) {
        breakpoints[index] = -1;
    }

    if (fontGetMonospacedCharacterWidth() > width) {
        return -1;
    }

    if (fontGetStringWidth(string) < width) {
        breakpoints[*breakpointsLengthPtr] = (short)strlen(string);
        *breakpointsLengthPtr += 1;
        return 0;
    }

    int gap = fontGetLetterSpacing();

    int accum = 0;
    const char* prevSpaceOrHyphen = nullptr;
    const char* pch = string;
    while (*pch != '\0') {
        accum += gap + fontGetCharacterWidth(*pch & 0xFF);
        if (accum <= width) {
            // NOTE: quests.txt #807 uses extended ascii.
            if (isspace(*pch & 0xFF) || *pch == '-') {
                prevSpaceOrHyphen = pch;
            }
        } else {
            if (*breakpointsLengthPtr == WORD_WRAP_MAX_COUNT) {
                return -1;
            }

            if (prevSpaceOrHyphen != nullptr) {
                // Word wrap.
                breakpoints[*breakpointsLengthPtr] = prevSpaceOrHyphen - string + 1;
                *breakpointsLengthPtr += 1;

                pch = prevSpaceOrHyphen;
            } else {
                // Character wrap.
                breakpoints[*breakpointsLengthPtr] = pch - string;
                *breakpointsLengthPtr += 1;

                pch--;
            }

            prevSpaceOrHyphen = nullptr;
            accum = 0;
        }
        pch++;
    }

    if (*breakpointsLengthPtr == WORD_WRAP_MAX_COUNT) {
        return -1;
    }

    breakpoints[*breakpointsLengthPtr] = pch - string + 1;
    *breakpointsLengthPtr += 1;

    return 0;
}

} // namespace fallout
