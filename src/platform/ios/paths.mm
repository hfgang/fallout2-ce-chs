#include "paths.h"

#include <Foundation/Foundation.h>

#include <SDL.h>

// Modelled after SDL_AndroidGetExternalStoragePath.
const char* iOSGetDocumentsPath()
{
    static char* s_iOSDocumentsPath = nullptr;

    if (s_iOSDocumentsPath == nullptr) {
        @autoreleasepool {
            NSArray* array = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES);

            if ([array count] > 0) {
                NSString* str = [array objectAtIndex:0];
                const char* base = [str fileSystemRepresentation];
                if (base) {
                    const size_t len = SDL_strlen(base) + 2;
                    s_iOSDocumentsPath = (char*)SDL_malloc(len);
                    if (s_iOSDocumentsPath == nullptr) {
                        SDL_OutOfMemory();
                    } else {
                        SDL_snprintf(s_iOSDocumentsPath, len, "%s/", base);
                    }
                }
            }
        }
    }

    return s_iOSDocumentsPath;
}

// Resources copied into the app bundle by CMake (see CMakeLists.txt) live
// next to the executable, which is what NSBundle calls the resource path.
const char* iOSGetResourcesPath()
{
    static char* s_iOSResourcesPath = nullptr;

    if (s_iOSResourcesPath == nullptr) {
        @autoreleasepool {
            NSString* str = [[NSBundle mainBundle] resourcePath];
            const char* base = [str fileSystemRepresentation];
            if (base) {
                const size_t len = SDL_strlen(base) + 2;
                s_iOSResourcesPath = (char*)SDL_malloc(len);
                if (s_iOSResourcesPath == nullptr) {
                    SDL_OutOfMemory();
                } else {
                    SDL_snprintf(s_iOSResourcesPath, len, "%s/", base);
                }
            }
        }
    }

    return s_iOSResourcesPath;
}
