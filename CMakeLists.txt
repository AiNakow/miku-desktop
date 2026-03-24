cmake_minimum_required(VERSION 3.21)

project(MikuPet
    VERSION 0.1.0
    DESCRIPTION "Hatsune Miku Desktop Pet"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# ── Qt 6 ─────────────────────────────────────────────────────────────────────
find_package(Qt6 REQUIRED COMPONENTS Widgets)
# qt_standard_project_setup() requires Qt 6.3+; AUTOMOC/AUTORCC are set above.

# ── Collect sprite frames (only animation frames, no UI assets) ───────────────
foreach(anim
    angry bye cry def dizzy drink eatcake excited fall hairflip heart hello
    jump lift listenmusic notlistenmusic punching question rolling rotate shy
    sleep watch)
  file(GLOB _files CONFIGURE_DEPENDS
      "${CMAKE_SOURCE_DIR}/drawable-hdpi/${anim}[0-9][0-9].png")
  list(APPEND SPRITE_FILES ${_files})
endforeach()

# ── Platform-specific executable flags ───────────────────────────────────────
if(WIN32)
  set(PLATFORM_SOURCES src/platform/windows.rc)
  qt_add_executable(MikuPet WIN32
      src/main.cpp
      src/AnimationEngine.h
      src/AnimationEngine.cpp
      src/PetWindow.h
      src/PetWindow.cpp
      ${PLATFORM_SOURCES})
elseif(APPLE)
  set(MACOSX_BUNDLE_GUI_IDENTIFIER "com.mikupet.app")
  set(MACOSX_BUNDLE_BUNDLE_VERSION  ${PROJECT_VERSION})
  set(MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION})
  qt_add_executable(MikuPet MACOSX_BUNDLE
      src/main.cpp
      src/AnimationEngine.h
      src/AnimationEngine.cpp
      src/PetWindow.h
      src/PetWindow.cpp)
else()
  qt_add_executable(MikuPet
      src/main.cpp
      src/AnimationEngine.h
      src/AnimationEngine.cpp
      src/PetWindow.h
      src/PetWindow.cpp)
endif()

# ── Embed sprites into the binary via Qt Resources ────────────────────────────
qt_add_resources(MikuPet "sprites"
    PREFIX  "/sprites"
    BASE    "${CMAKE_SOURCE_DIR}/drawable-hdpi"
    FILES   ${SPRITE_FILES})

target_link_libraries(MikuPet PRIVATE Qt6::Widgets)

target_compile_definitions(MikuPet PRIVATE
    APP_VERSION="${PROJECT_VERSION}")

# ── Install rules ─────────────────────────────────────────────────────────────
include(GNUInstallDirs)

if(APPLE)
  install(TARGETS MikuPet BUNDLE DESTINATION .)
else()
  install(TARGETS MikuPet RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

# ── Qt runtime deployment (Qt 6.3+) ──────────────────────────────────────────
if(NOT CMAKE_CROSSCOMPILING)
  qt_generate_deploy_app_script(
      TARGET MikuPet
      OUTPUT_SCRIPT _deploy_script
      NO_UNSUPPORTED_PLATFORM_ERROR)
  install(SCRIPT ${_deploy_script})
endif()

# ── CPack packaging ───────────────────────────────────────────────────────────
set(CPACK_PACKAGE_NAME             "MikuPet")
set(CPACK_PACKAGE_VERSION          "${PROJECT_VERSION}")
set(CPACK_PACKAGE_VENDOR           "MikuPet")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Hatsune Miku Desktop Pet")
set(CPACK_PACKAGE_INSTALL_DIRECTORY   "MikuPet")
set(CPACK_VERBATIM_VARIABLES          ON)

if(WIN32)
  set(CPACK_GENERATOR "NSIS;ZIP")
  set(CPACK_NSIS_DISPLAY_NAME     "MikuPet")
  set(CPACK_NSIS_PACKAGE_NAME     "MikuPet")
  set(CPACK_NSIS_INSTALL_ROOT     "$PROGRAMFILES64")
  set(CPACK_NSIS_CREATE_ICONS_EXTRA
    "CreateShortCut '$SMPROGRAMS\\\\MikuPet\\\\MikuPet.lnk' '$INSTDIR\\\\bin\\\\MikuPet.exe'")
  set(CPACK_NSIS_DELETE_ICONS_EXTRA
    "Delete '$SMPROGRAMS\\\\MikuPet\\\\MikuPet.lnk'")
elseif(APPLE)
  set(CPACK_GENERATOR "DragNDrop")
  set(CPACK_DMG_VOLUME_NAME "MikuPet ${PROJECT_VERSION}")
else()
  set(CPACK_GENERATOR "DEB;RPM;TXZ")
  set(CPACK_DEBIAN_PACKAGE_MAINTAINER  "MikuPet <noreply@mikupet>")
  set(CPACK_DEBIAN_PACKAGE_DEPENDS     "libqt6widgets6 (>= 6.4)")
  set(CPACK_DEBIAN_PACKAGE_SECTION     "misc")
  set(CPACK_RPM_PACKAGE_REQUIRES       "qt6-qtbase >= 6.4")
  set(CPACK_RPM_PACKAGE_LICENSE        "MIT")
endif()

include(CPack)
