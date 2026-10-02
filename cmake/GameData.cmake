# Game data built from the user's own copy of the game (never part of the repository): see game/README.md.
#
#   SBSO_GAME_DIR       installed game folder (data/, maps/ ...). Default: ./game, or the legacy "SpongeBob Atlantis SquareOff - WildGames".
#   SBSO_MAIN_SWF       main SWF dumped from the running game (tools/dump). Default: <game>/sbso_main.swf.
#   SBSO_EXTRACTED_DIR  extractor output, shared by every build directory (default ./extracted).
#
# Target game_data (part of ALL when the game files are present): runs tools/game_data.py, which re-extracts only when the extractor
# changed and writes ${SBSO_GAME_PAK} and the icons in ${SBSO_ICON_DIR}.
if(EXISTS "${CMAKE_SOURCE_DIR}/game/data")
  set(_sbso_default_game "${CMAKE_SOURCE_DIR}/game")
else()
  set(_sbso_default_game "${CMAKE_SOURCE_DIR}/SpongeBob Atlantis SquareOff - WildGames")
endif()
set(SBSO_GAME_DIR "${_sbso_default_game}" CACHE PATH "Folder with your installed copy of the game (see game/README.md)")
set(SBSO_MAIN_SWF "" CACHE FILEPATH "Main SWF dumped from the running game (default: <game>/sbso_main.swf)")
set(SBSO_EXTRACTED_DIR "${CMAKE_SOURCE_DIR}/extracted" CACHE PATH "Where the extracted assets go (git-ignored)")
set(SBSO_GAME_PAK "${CMAKE_BINARY_DIR}/game.pak")
set(SBSO_ICON_DIR "${CMAKE_BINARY_DIR}/icons")
set(SBSO_MAPS_DIR "${SBSO_GAME_DIR}/maps")

set(SBSO_HAVE_GAME OFF)
if(EXISTS "${SBSO_GAME_DIR}/data/titlescreen.swf" AND EXISTS "${SBSO_MAPS_DIR}/SBSO2_CONFIG.xml")
  set(SBSO_HAVE_GAME ON)
endif()
option(SBSO_BUILD_GAME_DATA "Extract the assets and build game.pak during the build (needs your game files)" ${SBSO_HAVE_GAME})

if(SBSO_BUILD_GAME_DATA)
  find_package(Python3 3.8 REQUIRED COMPONENTS Interpreter)
  set(_sbso_main_arg "")
  if(SBSO_MAIN_SWF)
    set(_sbso_main_arg --main-swf "${SBSO_MAIN_SWF}")
  endif()
  # fail early, at configure time, with a readable message when something is missing
  execute_process(COMMAND ${Python3_EXECUTABLE} "${CMAKE_SOURCE_DIR}/tools/game_data.py" --game "${SBSO_GAME_DIR}" ${_sbso_main_arg} --check
                  RESULT_VARIABLE _sbso_check OUTPUT_VARIABLE _sbso_check_out ERROR_VARIABLE _sbso_check_err)
  if(NOT _sbso_check EQUAL 0)
    message(FATAL_ERROR "${_sbso_check_out}${_sbso_check_err}\n(configure with -DSBSO_BUILD_GAME_DATA=OFF to build only the code)")
  endif()
  message(STATUS "Game data: ${SBSO_GAME_DIR}")
  add_custom_target(game_data ALL
    COMMAND ${Python3_EXECUTABLE} "${CMAKE_SOURCE_DIR}/tools/game_data.py" --game "${SBSO_GAME_DIR}" ${_sbso_main_arg}
            --extracted "${SBSO_EXTRACTED_DIR}" --pak "${SBSO_GAME_PAK}" --icons "${SBSO_ICON_DIR}"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    COMMENT "Game data from your copy of the game (extracts only when needed)"
    USES_TERMINAL VERBATIM)
elseif(SBSO_HAVE_GAME)
  message(STATUS "Game data: disabled (SBSO_BUILD_GAME_DATA=OFF). Only the code is built.")
else()
  message(STATUS "Game data: not built (no game files in ${SBSO_GAME_DIR}; see game/README.md). Only the code is built.")
endif()
