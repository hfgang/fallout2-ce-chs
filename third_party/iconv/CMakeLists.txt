# GNU libiconv, used for converting multi-byte encoded text (e.g. GBK for
# Chinese) to Unicode for FreeType rendering. The system provides iconv on
# most platforms, but Windows does not, so it is vendored here for all
# platforms to keep the build uniform.
#
# The vendored build system declares cmake_minimum_required(VERSION 3.1),
# which CMake 4+ rejects ("Compatibility with CMake < 3.5 has been removed
# from CMake"). CMAKE_POLICY_VERSION_MINIMUM (which must be >= 3.5) opts out
# of that check. A normal variable is sufficient: it is inherited by the
# dependency's scope added via FetchContent below. On CMake < 4 the variable
# is simply unused. An explicit -DCMAKE_POLICY_VERSION_MINIMUM=<v> wins.
set(_fallout_iconv_min_policy "3.5")
if(CMAKE_POLICY_VERSION_MINIMUM)
    set(_fallout_iconv_min_policy "${CMAKE_POLICY_VERSION_MINIMUM}")
endif()
set(CMAKE_POLICY_VERSION_MINIMUM "${_fallout_iconv_min_policy}")

include(FetchContent)

FetchContent_Declare(iconv
    GIT_REPOSITORY "https://github.com/sonilyan/libiconv_for_fallout2ce.git"
    GIT_SHALLOW TRUE
    GIT_PROGRESS TRUE
    EXCLUDE_FROM_ALL
)

FetchContent_MakeAvailable(iconv)

set(ICONV_LIBRARIES "iconv-static" PARENT_SCOPE)
set(ICONV_INCLUDE_DIRS ${iconv_SOURCE_DIR} ${iconv_BINARY_DIR}/include PARENT_SCOPE)
