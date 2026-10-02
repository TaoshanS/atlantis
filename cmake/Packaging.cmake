# Per-platform app packaging (files in platforms/<os>/). The game data (game.pak) and the icons are made from the user's own game files
# at build time (target game_data) and copied into the package: a package built this way must not be shared.
set(SBSO_APP_NAME "Atlantis SquareOff")
set(SBSO_BUNDLE_ID "io.github.sbso.atlantis" CACHE STRING "Bundle identifier of the macOS / iOS app")
set(SBSO_VERSION "${PROJECT_VERSION}")

if(APPLE AND NOT IOS)
  option(SBSO_BUILD_APP "Build the double-clickable macOS app (${SBSO_APP_NAME}.app)" ON)
  if(SBSO_BUILD_APP)
    add_executable(sbso_app MACOSX_BUNDLE src/app/main.cpp)
    target_link_libraries(sbso_app sbso_appcore)
    set_target_properties(sbso_app PROPERTIES
      OUTPUT_NAME "${SBSO_APP_NAME}"
      MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/platforms/macos/Info.plist.in"
      MACOSX_BUNDLE_GUI_IDENTIFIER "${SBSO_BUNDLE_ID}"
      MACOSX_BUNDLE_BUNDLE_NAME "${SBSO_APP_NAME}"
      MACOSX_BUNDLE_BUNDLE_VERSION "${SBSO_VERSION}"
      MACOSX_BUNDLE_SHORT_VERSION_STRING "${SBSO_VERSION}")
    # Data, icon and signature are refreshed on every build (a POST_BUILD step only runs when the executable relinks, so a new
    # game.pak would never reach the bundle). The signature goes last: it seals the resources.
    set(_res "$<TARGET_BUNDLE_CONTENT_DIR:sbso_app>/Resources")
    set(_bundle_cmds)
    if(TARGET game_data)
      list(APPEND _bundle_cmds COMMAND ${CMAKE_COMMAND} -E copy_if_different "${SBSO_GAME_PAK}" "${_res}/game.pak")
      find_program(SBSO_ICONUTIL iconutil)
      if(SBSO_ICONUTIL)
        list(APPEND _bundle_cmds COMMAND ${SBSO_ICONUTIL} -c icns "${SBSO_ICON_DIR}/AppIcon.iconset" -o "${_res}/AppIcon.icns")
      endif()
    endif()
    add_custom_target(sbso_app_bundle ALL ${_bundle_cmds}
      COMMAND codesign --force --deep --sign - "$<TARGET_BUNDLE_DIR:sbso_app>"
      COMMENT "Bundling ${SBSO_APP_NAME}.app (game data, icon, ad-hoc signature)" VERBATIM)
    add_dependencies(sbso_app_bundle sbso_app)
    if(TARGET game_data)
      add_dependencies(sbso_app_bundle game_data)
    endif()
  endif()
elseif(IOS)
  # sbso itself is the app bundle (see CMakeLists.txt). Xcode signs it with the team given in CMAKE_XCODE_ATTRIBUTE_DEVELOPMENT_TEAM.
  set_target_properties(sbso PROPERTIES
    OUTPUT_NAME "AtlantisSquareOff"
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/platforms/ios/Info.plist.in"
    MACOSX_BUNDLE_GUI_IDENTIFIER "${SBSO_BUNDLE_ID}"
    MACOSX_BUNDLE_BUNDLE_NAME "${SBSO_APP_NAME}"
    MACOSX_BUNDLE_BUNDLE_VERSION "${SBSO_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${SBSO_VERSION}"
    XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "${SBSO_BUNDLE_ID}"
    XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
    XCODE_ATTRIBUTE_IPHONEOS_DEPLOYMENT_TARGET "15.0"
    XCODE_ATTRIBUTE_CODE_SIGN_STYLE "Automatic")
  target_sources(sbso PRIVATE "${CMAKE_SOURCE_DIR}/platforms/ios/LaunchScreen.storyboard")
  set_source_files_properties("${CMAKE_SOURCE_DIR}/platforms/ios/LaunchScreen.storyboard" PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
  # game.pak is a bundle resource (copied by Xcode before it signs the app; a post-build copy would break the signature). The icon goes in an
  # asset catalog made here; game_data drops the 1024 px icon (made from your game files) into it before Xcode compiles the catalog.
  if(TARGET game_data)
    add_dependencies(sbso game_data)
    set(_xcassets "${CMAKE_BINARY_DIR}/Assets.xcassets")
    file(WRITE "${_xcassets}/Contents.json" "{ \"info\" : { \"author\" : \"xcode\", \"version\" : 1 } }\n")
    file(WRITE "${_xcassets}/AppIcon.appiconset/Contents.json"
      "{ \"images\" : [ { \"filename\" : \"icon_ios_1024.png\", \"idiom\" : \"universal\", \"platform\" : \"ios\", \"size\" : \"1024x1024\" } ],\n  \"info\" : { \"author\" : \"xcode\", \"version\" : 1 } }\n")
    add_custom_command(TARGET game_data POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different "${SBSO_ICON_DIR}/icon_ios_1024.png" "${_xcassets}/AppIcon.appiconset/icon_ios_1024.png" VERBATIM)
    set_source_files_properties("${SBSO_GAME_PAK}" PROPERTIES GENERATED TRUE MACOSX_PACKAGE_LOCATION Resources)
    target_sources(sbso PRIVATE "${SBSO_GAME_PAK}" "${_xcassets}")
    set_source_files_properties("${_xcassets}" PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    set_target_properties(sbso PROPERTIES XCODE_ATTRIBUTE_ASSETCATALOG_COMPILER_APPICON_NAME "AppIcon")
  endif()
elseif(WIN32)
  set_target_properties(sbso PROPERTIES WIN32_EXECUTABLE $<CONFIG:Release>)
  if(TARGET game_data)
    configure_file("${CMAKE_SOURCE_DIR}/platforms/windows/sbso.rc.in" "${CMAKE_BINARY_DIR}/sbso.rc" @ONLY)
    target_sources(sbso PRIVATE "${CMAKE_BINARY_DIR}/sbso.rc")
    add_dependencies(sbso game_data)
  endif()
  install(TARGETS sbso RUNTIME DESTINATION .)
  if(TARGET game_data)
    install(FILES "${SBSO_GAME_PAK}" DESTINATION .)
  endif()
else()  # Linux and other freedesktop systems
  include(GNUInstallDirs)
  install(TARGETS sbso RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
  install(FILES platforms/linux/io.github.sbso.atlantis.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
  if(TARGET game_data)
    install(FILES "${SBSO_GAME_PAK}" DESTINATION ${CMAKE_INSTALL_DATADIR}/sbso)
    install(FILES "${SBSO_ICON_DIR}/icon_512.png" DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/512x512/apps RENAME io.github.sbso.atlantis.png)
  endif()
endif()
