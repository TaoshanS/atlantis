# SDL3: an installed package (find_package) or, when missing / when a self-contained binary is wanted, the release tarball built
# along with the project. Defines SDL3_FOUND and the SDL3::SDL3 target.
option(SBSO_FETCH_SDL3 "Download and build SDL3 when it is not installed" ON)
option(SBSO_STATIC_SDL3 "Ignore an installed SDL3: download it and link it statically (self-contained app)" OFF)
set(SBSO_SDL3_VERSION 3.2.20)

if(IOS OR ANDROID)
  set(SBSO_STATIC_SDL3 ON)
endif()
if(NOT SBSO_STATIC_SDL3)
  find_package(SDL3 QUIET)
endif()
if((NOT SDL3_FOUND AND SBSO_FETCH_SDL3) OR SBSO_STATIC_SDL3)
  include(FetchContent)
  FetchContent_Declare(SDL3 URL https://github.com/libsdl-org/SDL/releases/download/release-${SBSO_SDL3_VERSION}/SDL3-${SBSO_SDL3_VERSION}.tar.gz)
  set(SDL_TESTS OFF CACHE BOOL "" FORCE)
  set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
  # a downloaded SDL is always linked statically: the executable / app bundle then needs nothing from the build directory
  set(SDL_SHARED OFF CACHE BOOL "" FORCE)
  set(SDL_STATIC ON CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(SDL3)
  set(SDL3_FOUND TRUE)
endif()
