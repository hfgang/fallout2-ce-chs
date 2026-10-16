#include "freetype_manager.h"

#include <ctype.h>
#include <iconv.h>
#include <stdio.h>
#include <string.h>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

#if defined(__APPLE__) && TARGET_OS_IOS
#include "platform/ios/paths.h"
#endif

#include <map>
#include <string>
#include <vector>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_GLYPH_H
#include FT_BITMAP_H

#include "color.h"
#include "config.h"
#include "db.h"
#include "debug.h"
#include "game_config.h"
#include "memory_manager.h"
#include "message.h"
#include "settings.h"
#include "word_wrap.h"

// Fallback encoding name used when font.ini does not specify one.
#define FT_FONT_DEFAULT_ENCODING "GBK"

// The maximum number of FreeType fonts.
#define FT_FONT_MAX (16)

// The maximum number of glyphs cached per font descriptor. When exceeded, no
// new glyphs are cached (blank glyphs are returned instead) to prevent
// unbounded memory growth.
#define FT_FONT_GLYPH_CACHE_MAX (8192)

// Size of the conversion buffer used to decode multi-byte strings into
// Unicode code points.
#define FT_FONT_OUTPUT_MAX (4096)

namespace fallout {

typedef struct FtFontGlyph {
    short width;
    short rows;
    short left;
    short top;
    unsigned char* buffer;
} FtFontGlyph;

typedef struct FtFontDescriptor {
    FT_Face face;

    // Shared with other descriptors (and the font file cache) when the same
    // file is referenced by multiple font configs. Owned by FtFontCacheList.
    unsigned char* filebuffer;

    int maxHeight;
    int maxWidth;
    int letterSpacing;
    int wordSpacing;
    int lineSpacing;
    int heightOffset;
    int warpMode;
    char encoding[64];

    std::map<uint32_t, FtFontGlyph> map;
} FtFontDescriptor;

static int FtFontLoad(int font);
static void FtFontSetCurrentImpl(int font);
static int FtFontGetLineHeightImpl();
static int FtFontGetStringWidthImpl(const char* string);
static int FtFontGetCharacterWidthImpl(int ch);
static int FtFontGetMonospacedStringWidthImpl(const char* string);
static int FtFontGetLetterSpacingImpl();
static int FtFontGetBufferSizeImpl(const char* string);
static int FtFontGetMonospacedCharacterWidthImpl();
static void FtFontDrawImpl(unsigned char* buf, const char* string, int length, int pitch, ColorWithFlags color);

static int FtFonteWordWrapImpl(const char* string, int width, short* breakpoints, short* breakpointsLengthPtr);

static bool gFtFontsInitialized = false;

static int gFtFontsLength = 0;

// Single FreeType library instance shared by all fonts.
static FT_Library gFtLibrary = NULL;

// Directory (relative to the game directory) holding "font.ini" and font
// files, for example "fonts/english" or "fonts/chs".
static std::string gFtFontsDir;

// 0x518688
FontManager gFtFontManager = {
    100,
    110,
    FtFontSetCurrentImpl,
    FtFontDrawImpl,
    FtFontGetLineHeightImpl,
    FtFontGetStringWidthImpl,
    FtFontGetCharacterWidthImpl,
    FtFontGetMonospacedStringWidthImpl,
    FtFontGetLetterSpacingImpl,
    FtFontGetBufferSizeImpl,
    FtFontGetMonospacedCharacterWidthImpl,
    FtFonteWordWrapImpl,
};

// 0x586838
static FtFontDescriptor gFtFontDescriptors[FT_FONT_MAX];

// 0x58E93C
static FtFontDescriptor* current;

static uint32_t output[FT_FONT_OUTPUT_MAX] = {
    0x0,
};

// The "arrow" glyph (0x95) is used by the original game to draw a pointing
// hand in dialogue. Since it's not a regular character in any encoding it's
// rendered from this hardcoded bitmap.
static int knobWidth = 5;
static int knobHeight = 7;

static unsigned char knobDump[35] = {
    0x34, 0x82, 0xb6, 0x82,
    0x34, 0x82, 0xb6, 0xb6,
    0xb6, 0x82, 0xb6, 0xb6,
    0xb6, 0xb6, 0xb6, 0x82,
    0xb6, 0xb6, 0xb6, 0x82,
    0x34, 0x82, 0xb6, 0x82,
    0x34, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0
};

// Converts a multi-byte string into Unicode code points stored in [output].
// Returns the number of code points.
static int LtoU(const char* input, size_t charInPutLen)
{
    if (current == NULL) {
        return 0;
    }

    if (input[0] == '\x95') {
        size_t output_size = sizeof(output);
        iconv_t cd = iconv_open("UCS-4-INTERNAL", current->encoding);
        char* tmp = (char*)(output + 1);
        const char* tmp2 = (const char*)(input + 1);
        charInPutLen -= 1;
        iconv(cd, &tmp2, &charInPutLen, &tmp, &output_size);
        iconv_close(cd);

        output[0] = '\x95';
        return (sizeof(output) - output_size) / 4 + 1;
    } else {
        size_t output_size = sizeof(output);
        iconv_t cd = iconv_open("UCS-4-INTERNAL", current->encoding);
        char* tmp = (char*)output;
        iconv(cd, &input, &charInPutLen, &tmp, &output_size);
        iconv_close(cd);

        return (sizeof(output) - output_size) / 4;
    }
}

// Converts a single Unicode code point back into the multi-byte encoding.
// Returns the number of bytes.
static int UtoL(const char* input, size_t charInPutLen)
{
    if (current == NULL) {
        return 0;
    }

    char encoded[16];
    size_t output_size = sizeof(encoded);
    iconv_t cd = iconv_open(current->encoding, "UCS-4-INTERNAL");
    char* tmp = encoded;
    iconv(cd, &input, &charInPutLen, &tmp, &output_size);
    iconv_close(cd);

    return sizeof(encoded) - output_size;
}

static FtFontGlyph GetFtFontGlyph(uint32_t unicode)
{
    if (current == NULL) {
        FtFontGlyph zeroGlyph = { 0, 0, 0, 0, NULL };
        return zeroGlyph;
    }

    if (current->map.count(unicode) > 0) {
        return current->map[unicode];
    } else {
        // If cache is full, don't insert new glyphs - return blank instead.
        // This avoids heap allocation during font rendering which could
        // corrupt memory if the renderer holds dangling buffer pointers.
        if (current->map.size() >= FT_FONT_GLYPH_CACHE_MAX) {
            FtFontGlyph zeroGlyph = { 0, 0, 0, 0, NULL };
            return zeroGlyph;
        }

        if (unicode == '\x95') {
            current->map[unicode].left = 0;
            current->map[unicode].top = current->maxHeight / 2;
            current->map[unicode].width = knobWidth;
            current->map[unicode].rows = knobHeight;
            current->map[unicode].buffer = knobDump;
        } else {
            if (current->face == NULL) {
                current->map[unicode].left = 0;
                current->map[unicode].top = 0;
                current->map[unicode].width = 0;
                current->map[unicode].rows = 0;
                current->map[unicode].buffer = NULL;
            } else {
                FT_Load_Glyph(current->face, FT_Get_Char_Index(current->face, unicode), FT_LOAD_DEFAULT | FT_LOAD_NO_BITMAP);
                if (FT_Render_Glyph(current->face->glyph, FT_RENDER_MODE_NORMAL) == 0) {
                    int ftWidth = current->face->glyph->bitmap.width;
                    int ftRows = current->face->glyph->bitmap.rows;

                    // Validate bitmap dimensions: reject stale/bogus data from failed renders.
                    if (ftWidth < 0 || ftRows < 0
                        || ftWidth > current->maxWidth * 2
                        || ftRows > current->maxHeight * 2) {
                        FtFontGlyph zeroGlyph = { 0, 0, 0, 0, NULL };
                        return zeroGlyph;
                    }

                    current->map[unicode].left = current->face->glyph->bitmap_left;
                    current->map[unicode].top = current->face->glyph->bitmap_top;
                    current->map[unicode].width = ftWidth;
                    current->map[unicode].rows = ftRows;

                    int count = ftWidth * ftRows;

                    if (count > 0) {
                        current->map[unicode].buffer = (unsigned char*)internal_malloc_safe(count, __FILE__, __LINE__);
                        memcpy(current->map[unicode].buffer, current->face->glyph->bitmap.buffer, count);
                    } else {
                        current->map[unicode].buffer = NULL;
                    }
                } else {
                    // Render failed - don't cache, return zero glyph.
                    FtFontGlyph zeroGlyph = { 0, 0, 0, 0, NULL };
                    return zeroGlyph;
                }
            }
        }

        return current->map[unicode];
    }
}

// Font files referenced by multiple font configs are loaded once and shared.
struct FtFontCache {
    std::string fontFileName;
    int fontFileSize;
    unsigned char* fontFilebuffer;
};

static std::vector<FtFontCache> FtFontCacheList;

static unsigned char* FtFontLoadFile(const std::string& fontFileName, int* fileSize)
{
    for (size_t i = 0; i < FtFontCacheList.size(); i++) {
        if (fontFileName.compare(FtFontCacheList[i].fontFileName) == 0) {
            debugPrint("FtFontLoadFile font %s reused\n", fontFileName.c_str());
            *fileSize = FtFontCacheList[i].fontFileSize;
            return FtFontCacheList[i].fontFilebuffer;
        }
    }

    File* stream = fileOpen(fontFileName.c_str(), "rb");
    if (stream == NULL) {
        return NULL;
    }

    *fileSize = fileGetSize(stream);

    unsigned char* filebuffer = (unsigned char*)internal_malloc_safe(*fileSize, __FILE__, __LINE__);

    int readleft = *fileSize;
    unsigned char* ptr = filebuffer;

    while (readleft > 10000) {
        int readsize = fileRead(ptr, 1, 10000, stream);
        if (readsize != 10000) {
            fileClose(stream);
            return NULL;
        }
        readleft -= 10000;
        ptr += 10000;
    }

    if (fileRead(ptr, 1, readleft, stream) != readleft) {
        fileClose(stream);
        return NULL;
    }

    fileClose(stream);

    FtFontCache newFtFontCache = { fontFileName, *fileSize, filebuffer };
    FtFontCacheList.push_back(newFtFontCache);

    return filebuffer;
}

// Determines whether [encoding] denotes a multi-byte encoding (e.g. GBK,
// GB18030 or Big5) as opposed to a single-byte one (e.g. ASCII, CP1252).
static bool FtFontsIsMultiByteEncoding(const char* encoding)
{
    char uppercase[64];
    size_t i = 0;
    for (; encoding[i] != '\0' && i < sizeof(uppercase) - 1; i++) {
        uppercase[i] = (char)toupper((unsigned char)encoding[i]);
    }
    uppercase[i] = '\0';

    return strstr(uppercase, "GB") != NULL
        || strstr(uppercase, "BIG5") != NULL
        || strstr(uppercase, "936") != NULL
        || strstr(uppercase, "950") != NULL;
}

// Determines the directory holding "font.ini" and the font files. Prefers the
// directory matching the configured language, falling back to English and
// finally to the Chinese ("chs") fonts.
// Case-insensitive substring search (there is no portable strcasestr).
static const char* FtFontsContains(const char* haystack, const char* needle)
{
    if (haystack == NULL || needle == NULL || *needle == '\0') {
        return NULL;
    }

    for (const char* p = haystack; *p != '\0'; p++) {
        const char* h = p;
        const char* n = needle;
        while (*h != '\0' && *n != '\0' && tolower((unsigned char)*h) == tolower((unsigned char)*n)) {
            h++;
            n++;
        }
        if (*n == '\0') {
            return p;
        }
    }

    return NULL;
}

// True for the well-known spellings used by Fallout 2 Chinese localizations:
// "chs", "chinese", "zh", "zh_CN", ... (Traditional "cht" is excluded: its
// text is Big5-encoded and needs its own font.ini with encoding=BIG5.)
static bool FtFontsIsChineseLanguage(const char* language)
{
    return FtFontsContains(language, "chin") != NULL
        || FtFontsContains(language, "chs") != NULL
        || FtFontsContains(language, "zh") != NULL;
}

// Looks for "font.ini" under [root], which is either empty (the game
// directory) or an absolute directory ending with a path separator.
static bool FtFontsResolveDirFromRoot(const char* root, std::string* resolvedDir)
{
    const char* language = settings.system.language.c_str();

    char path[COMPAT_MAX_PATH];

    // 1. Explicit per-language configuration, e.g. "fonts/chs/font.ini" for
    //    language=chs. Any language can provide its own directory this way.
    snprintf(path, sizeof(path), "%sfonts/%s/font.ini", root, language);
    File* stream = fileOpen(path, "rb");
    if (stream != NULL) {
        fileClose(stream);
        *resolvedDir = std::string(root) + "fonts/" + language;
        return true;
    }

    // 2. The bundled Chinese (GBK) configuration. Chinese Fallout 2 installs
    //    commonly keep language=english because the translated messages live
    //    inside master.dat, so english maps here as well. English text works
    //    through this configuration too because GBK is a superset of ASCII.
    //    Other languages keep the original bitmap fonts instead: their
    //    encodings (e.g. CP1251) are not GBK-compatible.
    if (compat_stricmp(language, ENGLISH) == 0 || FtFontsIsChineseLanguage(language)) {
        snprintf(path, sizeof(path), "%sfonts/chs/font.ini", root);
        stream = fileOpen(path, "rb");
        if (stream != NULL) {
            fileClose(stream);
            *resolvedDir = std::string(root) + "fonts/chs";
            return true;
        }
    }

    return false;
}

// Directory inside the app bundle holding the fonts shipped with the build
// (iOS only). Returns null when there is no bundle to look into.
static const char* FtFontsBundledRoot()
{
#if defined(__APPLE__) && TARGET_OS_IOS
    return iOSGetResourcesPath();
#else
    return NULL;
#endif
}

// Fonts next to the game data always win, so players can drop their own
// "fonts" directory in to override the bundled one.
static bool FtFontsResolveDir()
{
    if (FtFontsResolveDirFromRoot("", &gFtFontsDir)) {
        return true;
    }

    const char* bundledRoot = FtFontsBundledRoot();
    if (bundledRoot != NULL && bundledRoot[0] != '\0') {
        if (FtFontsResolveDirFromRoot(bundledRoot, &gFtFontsDir)) {
            debugPrint("FtFontsInit: using fonts bundled with the app\n");
            return true;
        }
    }

    return false;
}

// 0x441C80
int FtFontsInit()
{
    if (FT_Init_FreeType(&gFtLibrary) != 0) {
        return -1;
    }

    if (!FtFontsResolveDir()) {
        debugPrint("FtFontsInit: no font.ini found, FreeType fonts disabled\n");
        return -1;
    }

    debugPrint("FtFontsInit: using fonts from %s\n", gFtFontsDir.c_str());

    int currentFont = -1;

    for (int font = 0; font < FT_FONT_MAX; font++) {
        if (FtFontLoad(font) == -1) {
            gFtFontDescriptors[font].face = NULL;
            gFtFontDescriptors[font].filebuffer = NULL;
        } else {
            ++gFtFontsLength;

            if (currentFont == -1) {
                currentFont = font;
            }

            gFtFontManager.maxFont = gFtFontsLength + 100;
        }
    }

    if (currentFont == -1) {
        return -1;
    }

    gFtFontsInitialized = true;

    // Message files share the font encoding. Enable multi-byte aware parsing
    // (e.g. GBK) so that trail bytes are not mistaken for the '}' delimiter.
    messageListSetMultiByte(FtFontsIsMultiByteEncoding(gFtFontDescriptors[currentFont].encoding));

    FtFontSetCurrentImpl(currentFont + 100);

    return 0;
}

// 0x441CEC
void FtFontsExit()
{
    for (int font = 0; font < FT_FONT_MAX; font++) {
        FtFontDescriptor* desc = &(gFtFontDescriptors[font]);

        for (auto& it : desc->map) {
            // Glyphs without a buffer (blank glyphs) and the hardcoded knob
            // bitmap don't own their memory.
            if (it.second.buffer != NULL && it.second.buffer != knobDump) {
                internal_free_safe(it.second.buffer, __FILE__, __LINE__);
            }
        }
        desc->map.clear();

        if (desc->face != NULL) {
            FT_Done_Face(desc->face);
            desc->face = NULL;
        }

        desc->filebuffer = NULL;
    }

    for (size_t i = 0; i < FtFontCacheList.size(); i++) {
        internal_free_safe(FtFontCacheList[i].fontFilebuffer, __FILE__, __LINE__);
    }
    FtFontCacheList.clear();

    if (gFtLibrary != NULL) {
        FT_Done_FreeType(gFtLibrary);
        gFtLibrary = NULL;
    }

    gFtFontsInitialized = false;
    gFtFontsLength = 0;
    gFtFontsDir.clear();
    current = NULL;
}

// 0x441D20
static int FtFontLoad(int font_index)
{
    char string[56];
    FtFontDescriptor* desc = &(gFtFontDescriptors[font_index]);

    Config config;
    if (!configInit(&config)) {
        return -1;
    }

    snprintf(string, sizeof(string), "%s/font.ini", gFtFontsDir.c_str());
    if (!configRead(&config, string, false)) {
        configFree(&config);
        return -1;
    }

    snprintf(string, sizeof(string), "font%d", font_index);

    if (!configGetInt(&config, string, "maxHeight", &desc->maxHeight)) {
        configFree(&config);
        return -1;
    }
    if (!configGetInt(&config, string, "maxWidth", &desc->maxWidth)) {
        desc->maxWidth = desc->maxHeight;
    }
    if (!configGetInt(&config, string, "lineSpacing", &desc->lineSpacing)) {
        configFree(&config);
        return -1;
    }
    if (!configGetInt(&config, string, "wordSpacing", &desc->wordSpacing)) {
        configFree(&config);
        return -1;
    }
    if (!configGetInt(&config, string, "letterSpacing", &desc->letterSpacing)) {
        configFree(&config);
        return -1;
    }
    if (!configGetInt(&config, string, "heightOffset", &desc->heightOffset)) {
        configFree(&config);
        return -1;
    }
    if (!configGetInt(&config, string, "warpMode", &desc->warpMode)) {
        configFree(&config);
        return -1;
    }

    char* encoding = NULL;
    if (!configGetString(&config, string, "encoding", &encoding) || encoding == NULL || encoding[0] == '\0') {
        snprintf(desc->encoding, sizeof(desc->encoding), "%s", FT_FONT_DEFAULT_ENCODING);
    } else {
        snprintf(desc->encoding, sizeof(desc->encoding), "%s", encoding);
    }

    char* fontFileName = NULL;
    if (!configGetString(&config, string, "fileName", &fontFileName)) {
        configFree(&config);
        return -1;
    }

    snprintf(string, sizeof(string), "%s/%s", gFtFontsDir.c_str(), fontFileName);

    int fileSize = 0;
    unsigned char* filebuffer = NULL;

    filebuffer = FtFontLoadFile(std::string(string), &fileSize);
    if (filebuffer == NULL || fileSize == 0) {
        debugPrint("FtFontLoad: font file %s failed\n", string);
        configFree(&config);
        return -1;
    }

    desc->filebuffer = filebuffer;

    if (FT_New_Memory_Face(gFtLibrary, desc->filebuffer, fileSize, 0, &desc->face) != 0) {
        debugPrint("FtFontLoad: FT_New_Memory_Face failed for %s\n", string);
        desc->face = NULL;
        configFree(&config);
        return -1;
    }

    FT_Select_Charmap(desc->face, FT_ENCODING_UNICODE);
    FT_Set_Pixel_Sizes(desc->face, desc->maxWidth, desc->maxHeight);

    configFree(&config);
    return 0;
}

// 0x442120
static void FtFontSetCurrentImpl(int font)
{
    if (!gFtFontsInitialized) {
        return;
    }

    font -= 100;

    if (font >= 0 && font < FT_FONT_MAX && gFtFontDescriptors[font].face != NULL) {
        current = &(gFtFontDescriptors[font]);
    }
}

// 0x442168
static int FtFontGetLineHeightImpl()
{
    if (!gFtFontsInitialized || current == NULL) {
        return 0;
    }

    return current->lineSpacing + current->maxHeight + current->heightOffset;
}

// 0x442188
static int FtFontGetStringWidthImpl(const char* string)
{
    if (!gFtFontsInitialized || current == NULL) {
        return 0;
    }

    if (strlen(string) == 1 && string[0] < 0) {
        return current->wordSpacing;
    } else {
        int width = 0;

        int count = LtoU((char*)string, strlen(string));

        for (int i = 0; i < count; i++) {
            uint32_t ch = output[i];

            if (ch == L'\n' || ch == L'\r') {
                continue;
            }

            if (ch == '\x95') {
                FtFontGlyph g = GetFtFontGlyph(ch);
                width += g.width + current->letterSpacing + 2;
            } else if (ch == L' ' || (ch < 256 && ch > 128)) {
                width += current->wordSpacing + current->letterSpacing;
            } else {
                FtFontGlyph g = GetFtFontGlyph(ch);
                width += g.width + current->letterSpacing;
            }
        }

        return width;
    }
}

// 0x4421DC
static int FtFontGetCharacterWidthImpl(int ch)
{
    if (!gFtFontsInitialized || current == NULL) {
        return 0;
    }

    return current->wordSpacing;
}

// 0x442210
static int FtFontGetMonospacedStringWidthImpl(const char* str)
{
    if (!gFtFontsInitialized || current == NULL) {
        return 0;
    }

    int count = LtoU((char*)str, strlen(str));
    return FtFontGetMonospacedCharacterWidthImpl() * count;
}

// 0x442240
static int FtFontGetLetterSpacingImpl()
{
    if (!gFtFontsInitialized || current == NULL) {
        return 0;
    }

    return current->letterSpacing;
}

// 0x442258
static int FtFontGetBufferSizeImpl(const char* str)
{
    if (!gFtFontsInitialized || current == NULL) {
        return 0;
    }

    return FtFontGetStringWidthImpl(str) * (current->lineSpacing + current->maxHeight + current->heightOffset);
}

// 0x442278
static int FtFontGetMonospacedCharacterWidthImpl()
{
    if (!gFtFontsInitialized || current == NULL) {
        return 0;
    }

    return current->lineSpacing + current->maxHeight;
}

// 0x4422B4
static void FtFontDrawImpl(unsigned char* buf, const char* string, int length, int pitch, ColorWithFlags color)
{
    if (!gFtFontsInitialized || current == NULL) {
        return;
    }

    if ((color & DRAW_TEXT_FLAG_SHADOWED) != 0) {
        // NOTE: Other font options preserved. This is different from text font
        // shadows.
        FtFontDrawImpl(buf + pitch + 1, string, length, pitch, COLOR_BLACK | static_cast<DrawTextFlags>(color & ~0xFF));
    }

    Color* palette = _getColorBlendTable(static_cast<Color>(color & COLOR_LAST));
    int count = LtoU((char*)string, strlen(string));
    int maxTop = -1;

    for (int i = 0; i < count; i++) {
        uint32_t ch = output[i];

        FtFontGlyph g = GetFtFontGlyph(ch);
        if (maxTop < g.top)
            maxTop = g.top;
    }

    maxTop = current->maxHeight - maxTop;

    int monospacedCharacterWidth = 0;
    if ((color & DRAW_TEXT_FLAG_MONOSPACED) != 0) {
        monospacedCharacterWidth = FtFontGetMonospacedCharacterWidthImpl();
    }

    unsigned char* ptr = buf;
    for (int i = 0; i < count; i++) {
        uint32_t ch = output[i];

        FtFontGlyph g = GetFtFontGlyph(ch);

        int characterWidth;
        if (ch == L'\n' || ch == L'\r') {
            continue;
        } else if (ch == L' ') {
            characterWidth = current->wordSpacing;
        } else if (ch == '\x95') {
            characterWidth = g.width + 2;
        } else {
            characterWidth = g.width;
        }

        unsigned char* end;
        if ((color & DRAW_TEXT_FLAG_MONOSPACED) != 0) {
            end = ptr + monospacedCharacterWidth;
            ptr += (monospacedCharacterWidth - characterWidth - current->letterSpacing) / 2;
        } else {
            end = ptr + characterWidth + current->letterSpacing;
        }
        if (end - buf > length) {
            break;
        }

        int yOffset = current->maxHeight - g.top - maxTop;
        if (yOffset < 0) {
            yOffset = 0;
        }

        unsigned char* glyphOrigin = ptr + yOffset * pitch;
        unsigned char* glyphDataPtr = g.buffer;

        int maxRows = g.rows;
        if (maxRows > current->maxHeight - yOffset) {
            maxRows = current->maxHeight - yOffset;
        }

        for (int y = 0; y < maxRows; y++) {
            unsigned char* rowPtr = glyphOrigin + y * pitch;
            for (int x = 0; x < g.width; x++) {
                unsigned char byte = *glyphDataPtr++;
                byte /= 26;

                *rowPtr++ = palette[(byte << 8) + *rowPtr];
            }
        }

        ptr = end;
    }

    if ((color & DRAW_TEXT_FLAG_UNDERLINED) != 0) {
        int length = ptr - buf;
        unsigned char* underlinePtr = buf + pitch * (current->maxHeight - 1);
        for (int index = 0; index < length; index++) {
            *underlinePtr++ = color & COLOR_LAST;
        }
    }

    _freeColorBlendTable(static_cast<Color>(color & COLOR_LAST));
}

// CJK-aware word wrap. Unlike the legacy implementation it breaks lines at
// character boundaries when there is no space to break at, which is required
// for Chinese text.
static int FtFonteWordWrapImpl(const char* string, int width, short* breakpoints, short* breakpointsLengthPtr)
{
    breakpoints[0] = 0;
    *breakpointsLengthPtr = 1;

    for (int index = 1; index < WORD_WRAP_MAX_COUNT; index++) {
        breakpoints[index] = -1;
    }

    if (fontGetStringWidth(string) < width) {
        breakpoints[*breakpointsLengthPtr] = (short)strlen(string);
        *breakpointsLengthPtr += 1;
        return 0;
    }

    int accum = 0;
    int prevSpaceOrHyphen = -1;
    int prevSpaceOrHyphenIndex = -1;
    int lineStart = 0;

    int count = LtoU((char*)string, strlen(string));
    int CharIndex = 0;

    for (int i = 0; i < count;) {
        const uint32_t ch = output[i];
        int charStart = CharIndex;
        int charLength;

        if (ch == L'\n' || ch == L'\r') {
            charLength = 1;
            CharIndex += charLength;
            if (*breakpointsLengthPtr == WORD_WRAP_MAX_COUNT) {
                return -1;
            }
            if (CharIndex > breakpoints[*breakpointsLengthPtr - 1]) {
                breakpoints[*breakpointsLengthPtr] = CharIndex;
                *breakpointsLengthPtr += 1;
            }
            lineStart = CharIndex;
            accum = 0;
            prevSpaceOrHyphen = -1;
            prevSpaceOrHyphenIndex = -1;
            i++;
            continue;
        }

        FtFontGlyph g = GetFtFontGlyph(ch);
        int characterWidth;

        if (ch == L' ' || (ch > 128 && ch < 256)) {
            characterWidth = current->letterSpacing + current->wordSpacing;
            charLength = 1;
        } else {
            characterWidth = current->letterSpacing + g.width;
            if (ch == '\x95') {
                characterWidth += 2;
            }

            if ((ch > 0 && ch < 128) || ch == '\x95') {
                charLength = 1;
            } else {
                charLength = UtoL((char*)(&(output[i])), sizeof(uint32_t));
                if (charLength <= 0) {
                    return -1;
                }
            }
        }

        if (accum + characterWidth <= width || charStart == lineStart) {
            accum += characterWidth;
            CharIndex += charLength;

            // NOTE: quests.txt #807 uses extended ascii.
            if (ch == L' ' || ch == L'-') {
                prevSpaceOrHyphen = CharIndex;
                prevSpaceOrHyphenIndex = i;
            } else if (current->warpMode == 1) {
                prevSpaceOrHyphen = -1;
                prevSpaceOrHyphenIndex = -1;
            }
            i++;
        } else {
            if (*breakpointsLengthPtr == WORD_WRAP_MAX_COUNT) {
                return -1;
            }

            if (prevSpaceOrHyphen > lineStart) {
                // Word wrap.
                breakpoints[*breakpointsLengthPtr] = prevSpaceOrHyphen;
                i = prevSpaceOrHyphenIndex + 1;
                CharIndex = prevSpaceOrHyphen;
            } else {
                // CJK text has no spaces, so wrap at the previous character boundary.
                breakpoints[*breakpointsLengthPtr] = charStart;
                CharIndex = charStart;
            }

            lineStart = breakpoints[*breakpointsLengthPtr];
            *breakpointsLengthPtr += 1;
            prevSpaceOrHyphen = -1;
            prevSpaceOrHyphenIndex = -1;
            accum = 0;
        }
    }

    if (*breakpointsLengthPtr == WORD_WRAP_MAX_COUNT) {
        return -1;
    }

    if (CharIndex > breakpoints[*breakpointsLengthPtr - 1]) {
        breakpoints[*breakpointsLengthPtr] = CharIndex;
        *breakpointsLengthPtr += 1;
    }

    return 0;
}

} // namespace fallout
