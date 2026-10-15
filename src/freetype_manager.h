#ifndef FREETYPE_MANAGER_H
#define FREETYPE_MANAGER_H

#include "text_font.h"

namespace fallout {

// Font manager backed by FreeType. It replaces the built-in interface font
// manager (fonts 100-110) when font files described by "font.ini" are
// available in the game directory (see "fonts/<language>/font.ini").
//
// Unlike the original single-byte bitmap fonts it supports multi-byte
// encodings (e.g. GBK) which makes it possible to render Chinese text.
extern FontManager gFtFontManager;

// Loads fonts described by "fonts/<language>/font.ini" (with fallbacks to
// "fonts/english/font.ini" and "fonts/chs/font.ini"). Returns 0 on success,
// in which case [gFtFontManager] should be registered with fontManagerAdd.
int FtFontsInit();
void FtFontsExit();

} // namespace fallout

#endif /* FREETYPE_MANAGER_H */
