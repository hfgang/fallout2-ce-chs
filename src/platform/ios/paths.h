#ifndef FALLOUT_PLATFORM_IOS_PATHS_H_
#define FALLOUT_PLATFORM_IOS_PATHS_H_

const char* iOSGetDocumentsPath();

// Path of the app bundle's resources directory (where the bundled "fonts"
// directory lives). Always ends with a path separator, or is null when the
// bundle cannot be located.
const char* iOSGetResourcesPath();

#endif /* FALLOUT_PLATFORM_IOS_PATHS_H_ */
