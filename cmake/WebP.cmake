# libwebp (BSD), decoder only: the extracted images are lossless WebP. Downloaded and built with the project, linked statically.
include(FetchContent)
set(SBSO_WEBP_VERSION 1.5.0)
FetchContent_Declare(libwebp URL https://github.com/webmproject/libwebp/archive/refs/tags/v${SBSO_WEBP_VERSION}.tar.gz)
foreach(_opt WEBP_BUILD_ANIM_UTILS WEBP_BUILD_CWEBP WEBP_BUILD_DWEBP WEBP_BUILD_GIF2WEBP WEBP_BUILD_IMG2WEBP WEBP_BUILD_VWEBP
             WEBP_BUILD_WEBPINFO WEBP_BUILD_WEBPMUX WEBP_BUILD_EXTRAS WEBP_BUILD_LIBWEBPMUX WEBP_BUILD_FUZZTEST)
  set(${_opt} OFF CACHE BOOL "" FORCE)
endforeach()
set(WEBP_BUILD_WEBP_JS OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS_SAVED ${BUILD_SHARED_LIBS})
set(BUILD_SHARED_LIBS OFF)
FetchContent_MakeAvailable(libwebp)
set(BUILD_SHARED_LIBS ${BUILD_SHARED_LIBS_SAVED})
