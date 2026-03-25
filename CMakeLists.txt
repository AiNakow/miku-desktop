cmake_minimum_required(VERSION 3.21)

project(MikuPet
    VERSION 0.1.0
    DESCRIPTION "Hatsune Miku Desktop Pet"
    LANGUAGES CXX)

set(MIKUPET_VERSION_OVERRIDE "" CACHE STRING "Override app/package version (e.g. 0.1.1)")
set(MIKUPET_TAG "" CACHE STRING "Build tag for package naming (e.g. v0.1.1)")

set(MIKUPET_VERSION "${PROJECT_VERSION}")
if(MIKUPET_VERSION_OVERRIDE)
  set(MIKUPET_VERSION "${MIKUPET_VERSION_OVERRIDE}")
endif()

set(MIKUPET_ARCH "${CMAKE_SYSTEM_PROCESSOR}")
set(MIKUPET_DEB_ARCH "${CMAKE_SYSTEM_PROCESSOR}")
set(MIKUPET_RPM_ARCH "${CMAKE_SYSTEM_PROCESSOR}")
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|AMD64)$")
  set(MIKUPET_ARCH "x86_64")
  set(MIKUPET_DEB_ARCH "amd64")
  set(MIKUPET_RPM_ARCH "x86_64")
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64|ARM64)$")
  set(MIKUPET_ARCH "aarch64")
  set(MIKUPET_DEB_ARCH "arm64")
  set(MIKUPET_RPM_ARCH "aarch64")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# ── App icon generation ──────────────────────────────────────────────────────
set(MIKUPET_APP_ICON_SOURCE "${CMAKE_SOURCE_DIR}/assets/app-icon.png" CACHE FILEPATH
    "Source PNG used to generate Windows/macOS/Linux app icons")
set(_MIKUPET_ICON_SOURCE_EFFECTIVE "${MIKUPET_APP_ICON_SOURCE}")
if(NOT EXISTS "${_MIKUPET_ICON_SOURCE_EFFECTIVE}")
  set(_MIKUPET_ICON_SOURCE_EFFECTIVE "${CMAKE_SOURCE_DIR}/drawable-hdpi/def01.png")
  message(WARNING
    "Icon source ${MIKUPET_APP_ICON_SOURCE} was not found. Falling back to ${_MIKUPET_ICON_SOURCE_EFFECTIVE}")
endif()

set(MIKUPET_ICON_OUTPUT_DIR "${CMAKE_BINARY_DIR}/generated-icons")
set(MIKUPET_WINDOWS_ICON   "${MIKUPET_ICON_OUTPUT_DIR}/windows/mikupet.ico")
set(MIKUPET_MACOS_ICON     "${MIKUPET_ICON_OUTPUT_DIR}/macos/mikupet.icns")

find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
  execute_process(
      COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/generate_icons.py"
              --input "${_MIKUPET_ICON_SOURCE_EFFECTIVE}"
              --output-dir "${MIKUPET_ICON_OUTPUT_DIR}"
              --name "mikupet"
      WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
      RESULT_VARIABLE _icon_gen_result
      OUTPUT_VARIABLE _icon_gen_stdout
      ERROR_VARIABLE _icon_gen_stderr)
  if(NOT _icon_gen_result EQUAL 0)
    message(WARNING "Icon generation failed: ${_icon_gen_stderr}")
  endif()
else()
  message(WARNING "Python3 interpreter not found; app icon files will not be generated automatically.")
endif()

set(PLATFORM_SOURCES)
if(WIN32)
  list(APPEND PLATFORM_SOURCES src/platform/windows.rc)
  if(EXISTS "${MIKUPET_WINDOWS_ICON}")
    set(_MIKUPET_WINDOWS_ICON_RC "${CMAKE_BINARY_DIR}/mikupet-appicon.rc")
    file(GENERATE OUTPUT "${_MIKUPET_WINDOWS_ICON_RC}"
      CONTENT "IDI_APP_ICON ICON \"${MIKUPET_WINDOWS_ICON}\"\n")
    list(APPEND PLATFORM_SOURCES "${_MIKUPET_WINDOWS_ICON_RC}")
  else()
    message(WARNING "Windows icon file not found: ${MIKUPET_WINDOWS_ICON}")
  endif()
elseif(APPLE)
  if(EXISTS "${MIKUPET_MACOS_ICON}")
    list(APPEND PLATFORM_SOURCES "${MIKUPET_MACOS_ICON}")
  else()
    message(WARNING "macOS icon file not found: ${MIKUPET_MACOS_ICON}")
  endif()
endif()

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
  file(GLOB _files_hidpi CONFIGURE_DEPENDS
      "${CMAKE_SOURCE_DIR}/drawable-hdpi/${anim}[0-9][0-9]@*.png")
  list(APPEND SPRITE_FILES ${_files})
  list(APPEND SPRITE_FILES ${_files_hidpi})
endforeach()

# ── Platform-specific executable flags ───────────────────────────────────────
if(WIN32)
  qt_add_executable(MikuPet WIN32
      src/main.cpp
      src/AnimationEngine.h
      src/AnimationEngine.cpp
      src/PetWindow.h
      src/PetWindow.cpp
      ${PLATFORM_SOURCES})
elseif(APPLE)
  set(MACOSX_BUNDLE_GUI_IDENTIFIER "com.mikupet.app")
  set(MACOSX_BUNDLE_BUNDLE_VERSION  ${MIKUPET_VERSION})
  set(MACOSX_BUNDLE_SHORT_VERSION_STRING ${MIKUPET_VERSION})
  qt_add_executable(MikuPet MACOSX_BUNDLE
      src/main.cpp
      src/AnimationEngine.h
      src/AnimationEngine.cpp
      src/PetWindow.h
      src/PetWindow.cpp
      ${PLATFORM_SOURCES})
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
  APP_VERSION="${MIKUPET_VERSION}")

if(APPLE AND EXISTS "${MIKUPET_MACOS_ICON}")
  set_source_files_properties("${MIKUPET_MACOS_ICON}" PROPERTIES
      MACOSX_PACKAGE_LOCATION "Resources")
  set_target_properties(MikuPet PROPERTIES
      MACOSX_BUNDLE_ICON_FILE "mikupet.icns")
endif()

if(WIN32)
  get_target_property(_qmake_executable Qt6::qmake IMPORTED_LOCATION)
  if(_qmake_executable)
    get_filename_component(_qt_bin_dir "${_qmake_executable}" DIRECTORY)
    find_program(WINDEPLOYQT_EXECUTABLE
      NAMES windeployqt
      HINTS "${_qt_bin_dir}")
    if(WINDEPLOYQT_EXECUTABLE)
      add_custom_command(TARGET MikuPet POST_BUILD
        COMMAND "${WINDEPLOYQT_EXECUTABLE}"
                --no-translations
                --verbose 0
                "$<TARGET_FILE:MikuPet>"
        COMMENT "Deploying Qt runtime (windeployqt)"
        VERBATIM)
    else()
      message(WARNING "windeployqt not found; MikuPet.exe may fail to run outside a Qt-configured environment.")
    endif()
  endif()
endif()

# ── Install rules ─────────────────────────────────────────────────────────────
include(GNUInstallDirs)

if(APPLE)
  install(TARGETS MikuPet BUNDLE DESTINATION .)
else()
  install(TARGETS MikuPet RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

if(UNIX AND NOT APPLE)
  foreach(_size 16 24 32 48 64 128 256 512)
    set(_linux_icon "${MIKUPET_ICON_OUTPUT_DIR}/linux/hicolor/${_size}x${_size}/apps/mikupet.png")
    if(EXISTS "${_linux_icon}")
      install(FILES "${_linux_icon}"
          DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/${_size}x${_size}/apps")
    endif()
  endforeach()

  set(MIKUPET_DESKTOP_EXEC "MikuPet")
  set(MIKUPET_DESKTOP_ICON "mikupet")
  configure_file(
      "${CMAKE_SOURCE_DIR}/src/platform/mikupet.desktop.in"
      "${CMAKE_BINARY_DIR}/mikupet.desktop"
      @ONLY)
  install(FILES "${CMAKE_BINARY_DIR}/mikupet.desktop"
      DESTINATION "${CMAKE_INSTALL_DATADIR}/applications")
endif()

# ── Qt runtime deployment (Qt 6.3+ only) ─────────────────────────────────────
if(NOT CMAKE_CROSSCOMPILING AND Qt6_VERSION VERSION_GREATER_EQUAL "6.3.0")
  qt_generate_deploy_app_script(
      TARGET MikuPet
      OUTPUT_SCRIPT _deploy_script
      NO_UNSUPPORTED_PLATFORM_ERROR)
  install(SCRIPT ${_deploy_script})
endif()

# ── CPack packaging ───────────────────────────────────────────────────────────
set(CPACK_PACKAGE_NAME             "MikuPet")
set(CPACK_PACKAGE_VERSION          "${MIKUPET_VERSION}")
set(CPACK_PACKAGE_VENDOR           "MikuPet")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Hatsune Miku Desktop Pet")
set(CPACK_PACKAGE_INSTALL_DIRECTORY   "MikuPet")
set(CPACK_VERBATIM_VARIABLES          ON)

if(MIKUPET_TAG)
  set(CPACK_PACKAGE_FILE_NAME "MikuPet-${MIKUPET_TAG}-${CMAKE_SYSTEM_NAME}-${MIKUPET_ARCH}")
else()
  set(CPACK_PACKAGE_FILE_NAME "MikuPet-${MIKUPET_VERSION}-${CMAKE_SYSTEM_NAME}-${MIKUPET_ARCH}")
endif()

if(WIN32)
  set(CPACK_GENERATOR "NSIS;ZIP")
  set(CPACK_NSIS_DISPLAY_NAME     "MikuPet")
  set(CPACK_NSIS_PACKAGE_NAME     "MikuPet")
  set(CPACK_NSIS_INSTALL_ROOT     "$PROGRAMFILES64")
  set(CPACK_NSIS_CREATE_ICONS_EXTRA
    "CreateShortCut '$SMPROGRAMS\\\\MikuPet\\\\MikuPet.lnk' '$INSTDIR\\\\bin\\\\MikuPet.exe'")
  set(CPACK_NSIS_DELETE_ICONS_EXTRA
    "Delete '$SMPROGRAMS\\\\MikuPet\\\\MikuPet.lnk'")
  if(EXISTS "${MIKUPET_WINDOWS_ICON}")
    set(CPACK_NSIS_MUI_ICON "${MIKUPET_WINDOWS_ICON}")
    set(CPACK_NSIS_INSTALLED_ICON_NAME "bin/MikuPet.exe")
  endif()
elseif(APPLE)
  set(CPACK_GENERATOR "DragNDrop")
  set(CPACK_DMG_VOLUME_NAME "MikuPet ${MIKUPET_VERSION}")
else()
  set(CPACK_GENERATOR "DEB;RPM;TXZ")
  set(CPACK_DEBIAN_FILE_NAME            DEB-DEFAULT)
  set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "${MIKUPET_DEB_ARCH}")
  set(CPACK_DEBIAN_PACKAGE_MAINTAINER  "MikuPet <noreply@mikupet>")
  set(CPACK_DEBIAN_PACKAGE_DEPENDS     "libqt6widgets6 (>= 6.4)")
  set(CPACK_DEBIAN_PACKAGE_SECTION     "misc")
  set(CPACK_RPM_FILE_NAME               RPM-DEFAULT)
  set(CPACK_RPM_PACKAGE_ARCHITECTURE    "${MIKUPET_RPM_ARCH}")
  set(CPACK_RPM_PACKAGE_REQUIRES       "qt6-qtbase >= 6.4")
  set(CPACK_RPM_PACKAGE_LICENSE        "MIT")
endif()

include(CPack)
