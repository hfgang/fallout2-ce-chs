# Build FreeType as a static library without optional dependencies
# (HarfBuzz, PNG, Brotli, BZip2, zlib) to keep the build self-contained.
set(FT_DISABLE_HARFBUZZ ON)
set(FT_DISABLE_PNG ON)
set(FT_DISABLE_BROTLI ON)
set(FT_DISABLE_BZIP2 ON)
set(FT_DISABLE_ZLIB ON)
set(FT_CONFIG_OPTION_USE_ZLIB OFF)
set(FT_ENABLE_ERROR_STRINGS OFF)

# FreeType 2.13.x declares cmake_minimum_required(VERSION 3.0), which CMake 4+
# rejects ("Compatibility with CMake < 3.5 has been removed from CMake").
# CMAKE_POLICY_VERSION_MINIMUM (which must be >= 3.5) opts out of that check.
# A normal variable is sufficient: it is inherited by the dependency's scope
# added via FetchContent below. On CMake < 4 the variable is simply unused.
# An explicit -DCMAKE_POLICY_VERSION_MINIMUM=<v> from the user wins.
set(_fallout_ft_min_policy "3.5")
if(CMAKE_POLICY_VERSION_MINIMUM)
    set(_fallout_ft_min_policy "${CMAKE_POLICY_VERSION_MINIMUM}")
endif()
set(CMAKE_POLICY_VERSION_MINIMUM "${_fallout_ft_min_policy}")

include(FetchContent)

FetchContent_Declare(freetype
    GIT_REPOSITORY "https://github.com/freetype/freetype"
    GIT_TAG "VER-2-13-2"
    GIT_SHALLOW TRUE
    GIT_PROGRESS TRUE
    EXCLUDE_FROM_ALL
)

FetchContent_MakeAvailable(freetype)

set(FREETYPE_LIBRARIES "freetype" PARENT_SCOPE)
set(FREETYPE_INCLUDE_DIRS ${freetype_SOURCE_DIR} ${freetype_BINARY_DIR} PARENT_SCOPE)
