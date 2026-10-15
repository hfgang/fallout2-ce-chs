#ifndef WORD_WRAP_H
#define WORD_WRAP_H

namespace fallout {

#define WORD_WRAP_MAX_COUNT (64)

// Legacy (single-byte, English-oriented) word wrap implementation.
//
// Callers should prefer the [wordWrap] function pointer declared in
// text_font.h, which resolves to the current font manager's implementation
// (e.g. CJK-aware wrapping for FreeType fonts).
int leagcyWordWrap(const char* string, int width, short* breakpoints, short* breakpointsLengthPtr);

} // namespace fallout

#endif /* WORD_WRAP_H */
