cmake_minimum_required(VERSION 3.21)
# SpongeBob SquarePants: Atlantis SquareOff - native port. Code only: the game data comes from your own copy (game/README.md).
project(sbso VERSION 0.1.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

option(SBSO_BUILD_TESTS "Build the tests and the developer tools" ON)
if(IOS OR ANDROID)
  set(SBSO_BUILD_TESTS OFF)
endif()
include(GameData)
set(SBSO_WARNINGS $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra>)

# ---- libraries: core, game logic, saves, engine, audio (no SDL) ----
add_library(sbso_core src/core/resample.cpp src/core/layout.cpp src/core/vfs.cpp)
target_include_directories(sbso_core PUBLIC src third_party)
target_compile_options(sbso_core PRIVATE ${SBSO_WARNINGS})

add_library(pugixml STATIC third_party/pugixml.cpp)
target_include_directories(pugixml PUBLIC third_party)

add_library(sbso_game src/game/database.cpp src/game/level.cpp src/game/board.cpp src/game/config.cpp src/game/dialogues.cpp src/game/cutscene.cpp src/game/sim.cpp src/game/sim_snapshot.cpp)
target_include_directories(sbso_game PUBLIC src)
target_link_libraries(sbso_game PUBLIC pugixml sbso_core)
target_compile_options(sbso_game PRIVATE ${SBSO_WARNINGS})

add_library(sbso_save src/save/save_data.cpp src/save/store.cpp src/save/progression.cpp src/save/sol_import.cpp)
target_include_directories(sbso_save PUBLIC src third_party)
target_compile_options(sbso_save PRIVATE ${SBSO_WARNINGS})

add_library(sbso_engine src/engine/text.cpp src/engine/library.cpp src/engine/movieclip.cpp src/engine/soft_renderer.cpp src/engine/stb_impl.cpp src/engine/stb_write_impl.cpp)
target_include_directories(sbso_engine PUBLIC src third_party)
target_link_libraries(sbso_engine PUBLIC sbso_core)
target_compile_options(sbso_engine PRIVATE ${SBSO_WARNINGS})
set_source_files_properties(src/engine/stb_impl.cpp src/engine/stb_write_impl.cpp PROPERTIES COMPILE_OPTIONS "-w")

add_library(sbso_levelview src/engine/level_view.cpp)
target_link_libraries(sbso_levelview PUBLIC sbso_engine sbso_game)

add_library(sbso_audio src/audio/mixer.cpp src/audio/sound_bank.cpp src/audio/sound_manager.cpp)
target_include_directories(sbso_audio PUBLIC src third_party)
target_link_libraries(sbso_audio PUBLIC sbso_core)
target_compile_options(sbso_audio PRIVATE ${SBSO_WARNINGS})

# ---- the game (SDL3 backend) ----
include(SDL3)
if(SDL3_FOUND)
  add_library(sbso_sdl src/engine/sdl_renderer.cpp)
  target_link_libraries(sbso_sdl PUBLIC sbso_engine sbso_core SDL3::SDL3)
  add_library(sbso_sdl_audio src/audio/sdl_audio.cpp)
  target_link_libraries(sbso_sdl_audio PUBLIC sbso_audio SDL3::SDL3)
  add_library(sbso_i18n src/i18n/strings.cpp)
  target_include_directories(sbso_i18n PUBLIC src third_party)
  target_link_libraries(sbso_i18n PUBLIC sbso_core)
  add_library(sbso_appcore
    src/app/app.cpp src/app/assets.cpp src/app/flow.cpp
    src/app/ui/hud.cpp src/app/ui/action_bar.cpp src/app/ui/battle_animator.cpp src/app/ui/dialogue_screen.cpp src/app/ui/gauge_screen.cpp
    src/app/ui/clip_panel.cpp src/app/ui/skin.cpp src/app/ui/menu_screens.cpp src/app/ui/world_map.cpp src/app/ui/clip_screens.cpp
    src/app/ui/minigame_screen.cpp src/app/ui/card_carousel.cpp src/app/ui/help_screen.cpp
    src/app/screens/chest_screen.cpp src/app/screens/scene_screens.cpp src/app/screens/profile_screen.cpp src/app/screens/saves_screen.cpp
    src/app/screens/level_select_screen.cpp src/app/screens/title_screen.cpp src/app/screens/battle_screen.cpp src/app/screens/options_screen.cpp)
  target_link_libraries(sbso_appcore PUBLIC sbso_sdl sbso_sdl_audio sbso_i18n sbso_save sbso_game sbso_levelview)
  # sbso: the game executable with the developer / test options (--headless, --shot, --battle ...). On iOS it is the app bundle.
  add_executable(sbso src/app/main.cpp)
  target_link_libraries(sbso sbso_appcore)
  include(Packaging)
else()
  message(STATUS "SDL3 not found: skipping the game (only the libraries and their tests are built)")
endif()

# ---- tests and developer tools ----
if(SBSO_BUILD_TESTS)
  enable_testing()
  # Tests that need your game files (maps/ or the extracted assets) print "skipped" and pass when they are not there.
  set(SBSO_TEST_DEFS MAPS_DIR="${SBSO_MAPS_DIR}" EXTRACTED_DIR="${SBSO_EXTRACTED_DIR}" TEST_TMP_DIR="${CMAKE_BINARY_DIR}" SOURCE_DIR="${CMAKE_SOURCE_DIR}")
  function(sbso_test name lib)
    add_executable(test_${name} tests/test_${name}.cpp)
    target_link_libraries(test_${name} ${lib} ${ARGN})
    target_compile_definitions(test_${name} PRIVATE ${SBSO_TEST_DEFS})
    add_test(NAME ${name} COMMAND test_${name})
  endfunction()
  sbso_test(core sbso_core)
  sbso_test(vfs sbso_core)
  sbso_test(game sbso_game)
  sbso_test(sim sbso_game)
  sbso_test(save sbso_save sbso_game)
  sbso_test(sol sbso_save)
  sbso_test(engine sbso_engine)
  sbso_test(audio sbso_audio)

  add_executable(sbso_shot tools/shot/shot.cpp)
  target_link_libraries(sbso_shot sbso_engine)
  add_executable(sbso_shot_level tools/shot/shot_level.cpp)
  target_link_libraries(sbso_shot_level sbso_levelview)

  if(SDL3_FOUND)
    add_executable(sbso_shot_sdl tools/shot/shot_sdl.cpp)
    target_link_libraries(sbso_shot_sdl sbso_sdl)
    # smoke tests of the whole game, headless (need the extracted assets)
    if(EXISTS "${SBSO_EXTRACTED_DIR}/characters/titlescreen.json" AND EXISTS "${SBSO_MAPS_DIR}")
      set(_run sbso --extracted ${SBSO_EXTRACTED_DIR} --maps ${SBSO_MAPS_DIR})
      add_test(NAME app_title COMMAND ${_run} --saves ${CMAKE_BINARY_DIR}/smoke_saves --headless 300 --shot ${CMAKE_BINARY_DIR}/smoke_title.png)
      add_test(NAME app_battle COMMAND ${_run} --saves ${CMAKE_BINARY_DIR}/smoke_saves --battle 2,2 --skip-dialogues --headless 330 --act MOVEMENT_SPRINT_1,7,8@150 --act ATTACK_BUBBLE_PUNCH_1,7,9@240 --end-turn@300 --shot ${CMAKE_BINARY_DIR}/smoke_battle.png)
      add_test(NAME app_map COMMAND ${_run} --saves ${CMAKE_BINARY_DIR}/smoke_saves --map --cleared 1,1 --just-won --headless 200 --click 115,318@120 --shot ${CMAKE_BINARY_DIR}/smoke_map.png)
      add_test(NAME app_chest COMMAND ${_run} --saves ${CMAKE_BINARY_DIR}/smoke_saves --chest --cleared 1,2 --coins 230 --headless 200 --click 300,430@80 --click 282,55@120 --shot ${CMAKE_BINARY_DIR}/smoke_chest.png)
      add_test(NAME app_flow COMMAND ${_run} --saves ${CMAKE_BINARY_DIR}/smoke_flow --profile FLOW --flow-battle 1,1 --skip-dialogues --headless 600 --win 80 --click 320,240@300 --shot ${CMAKE_BINARY_DIR}/smoke_flow.png)
      set_tests_properties(app_title app_battle app_map app_chest app_flow PROPERTIES FAIL_REGULAR_EXPRESSION "refused;failed")
    endif()
  endif()
endif()
