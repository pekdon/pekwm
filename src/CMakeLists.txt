cmake_minimum_required(VERSION 3.5)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

configure_file(${CMAKE_SOURCE_DIR}/CMake/config.h.in
  ${CMAKE_CURRENT_BINARY_DIR}/config.h)

set(util_SOURCES
  CfgParser.cc
  CfgParserKey.cc
  CfgParserSource.cc
  Charset.cc
  Compat.cc
  Debug.cc
  Observable.cc
  RegexString.cc
  Util.cc)

set(x11_SOURCES
  PWinObj.cc
  X11.cc
  X11Util.cc)

set(texture_SOURCES
  Action.cc
  FontHandler.cc
  ImageHandler.cc
  PFont.cc
  PImage.cc
  PImageIcon.cc
  PTexture.cc
  PTexturePlain.cc
  Render.cc
  TextureHandler.cc
  Theme.cc
  ThemeGm.cc)

set(wm_SOURCES
  ActionHandler.cc
  ActionMenu.cc
  AutoProperties.cc
  Completer.cc
  Client.cc
  ClientMgr.cc
  CmdDialog.cc
  Config.cc
  DockApp.cc
  FocusToggleEventHandler.cc
  Frame.cc
  FrameListMenu.cc
  Globals.cc
  Harbour.cc
  InputDialog.cc
  KeyGrabber.cc
  KeyboardMoveResizeEventHandler.cc
  ManagerWindows.cc
  MenuHandler.cc
  MoveEventHandler.cc
  PDecor.cc
  PMenu.cc
  StatusWindow.cc
  SearchDialog.cc
  WORefMenu.cc
  WindowManager.cc
  WinLayouter.cc
  Workspaces.cc
  WorkspaceIndicator.cc)
set(common_INCLUDE_DIRS ${PROJECT_BINARY_DIR}/src ${ICONV_INCLUDE_DIR} ${X11_INCLUDE_DIR})
set(common_LIBRARIES ${ICONV_LIBRARIES} ${X11_LIBRARIES})

if (ENABLE_SHAPE AND X11_Xshape_FOUND)
  set(common_INCLUDE_DIRS ${common_INCLUDE_DIRS} ${X11_Xshape_INCLUDE_PATH})
  set(common_LIBRARIES ${common_LIBRARIES} ${X11_Xshape_LIB})
endif (ENABLE_SHAPE AND X11_Xshape_FOUND)

if (ENABLE_XINERAMA AND X11_Xinerama_FOUND)
  set(common_INCLUDE_DIRS ${common_INCLUDE_DIRS} ${X11_Xinerama_INCLUDE_PATH})
  set(common_LIBRARIES ${common_LIBRARIES} ${X11_Xinerama_LIB})
endif (ENABLE_XINERAMA AND X11_Xinerama_FOUND)

if (ENABLE_XFT AND X11_Xft_FOUND AND FREETYPE_FOUND)
  set(common_INCLUDE_DIRS ${common_INCLUDE_DIRS} ${X11_Xft_INCLUDE_PATH} ${FREETYPE_INCLUDE_DIRS})
  set(common_LIBRARIES ${common_LIBRARIES} ${X11_Xft_LIB} ${FREETYPE_LIBRARIES})
endif (ENABLE_XFT AND X11_Xft_FOUND AND FREETYPE_FOUND)

if (ENABLE_IMAGE_JPEG AND JPEG_FOUND)
  set(texture_SOURCES ${texture_SOURCES} PImageLoaderJpeg.cc)
  set(common_INCLUDE_DIRS ${common_INCLUDE_DIRS} ${JPEG_INCLUDE_DIR})
  set(common_LIBRARIES ${common_LIBRARIES} ${JPEG_LIBRARY})
endif (ENABLE_IMAGE_JPEG AND JPEG_FOUND)

if (ENABLE_IMAGE_PNG AND PNG_FOUND)
  set(texture_SOURCES ${texture_SOURCES} PImageLoaderPng.cc)
  set(common_INCLUDE_DIRS ${common_INCLUDE_DIRS} ${PNG_INCLUDE_DIR})
  set(common_LIBRARIES ${common_LIBRARIES} ${PNG_LIBRARY_RELEASE})
endif (ENABLE_IMAGE_PNG AND PNG_FOUND)

if (ENABLE_IMAGE_XPM AND X11_Xpm_FOUND)
  set(texture_SOURCES ${texture_SOURCES} PImageLoaderXpm.cc)
  set(common_INCLUDE_DIRS ${common_INCLUDE_DIRS} ${X11_Xpm_INCLUDE_PATH})
  set(common_LIBRARIES ${common_LIBRARIES} ${X11_Xpm_LIB})
endif (ENABLE_IMAGE_XPM AND X11_Xpm_FOUND)

if (ENABLE_RANDR AND X11_Xrandr_FOUND)
  set(common_INCLUDE_DIRS ${common_INCLUDE_DIRS} ${X11_Xrandr_INCLUDE_PATH})
  set(common_LIBRARIES ${common_LIBRARIES} ${X11_Xrandr_LIB})
endif (ENABLE_RANDR AND X11_Xrandr_FOUND)

add_library(util STATIC ${util_SOURCES})
target_include_directories(util PUBLIC ${common_INCLUDE_DIRS})

add_library(x11 STATIC ${x11_SOURCES})
target_include_directories(x11 PUBLIC ${common_INCLUDE_DIRS})

add_library(texture STATIC ${texture_SOURCES})
target_include_directories(texture PUBLIC ${common_INCLUDE_DIRS})

add_library(wm STATIC ${wm_SOURCES})
target_include_directories(wm PUBLIC ${common_INCLUDE_DIRS})

add_executable(pekwm pekwm.cc)
target_include_directories(pekwm PUBLIC ${PROJECT_BINARY_DIR}/src)
target_link_libraries(pekwm util ${ICONV_LIBRARIES})
install(TARGETS pekwm DESTINATION bin)

add_executable(pekwm_bg
  pekwm_bg.cc)
target_include_directories(pekwm_bg PUBLIC ${common_INCLUDE_DIRS})
target_link_libraries(pekwm_bg texture x11 util ${common_LIBRARIES})
install(TARGETS pekwm_bg DESTINATION bin)

if (CMAKE_BUILD_TYPE MATCHES Debug)
  add_executable(pekwm_cfg pekwm_cfg.cc)
  target_include_directories(pekwm_cfg PUBLIC ${PROJECT_BINARY_DIR}/src)
  target_link_libraries(pekwm_cfg util ${common_LIBRARIES})
  install(TARGETS pekwm_cfg DESTINATION bin)
endif (CMAKE_BUILD_TYPE MATCHES Debug)

add_executable(pekwm_dialog
  pekwm_dialog.cc)
target_include_directories(pekwm_dialog PUBLIC ${common_INCLUDE_DIRS})
target_link_libraries(pekwm_dialog texture x11 util ${common_LIBRARIES})
install(TARGETS pekwm_dialog DESTINATION bin)

add_executable(pekwm_ctrl
  pekwm_ctrl.cc)
target_include_directories(pekwm_ctrl PUBLIC ${common_INCLUDE_DIRS})
target_link_libraries(pekwm_ctrl x11 util ${common_LIBRARIES})
install(TARGETS pekwm_ctrl DESTINATION bin)

add_executable(pekwm_panel
  pekwm_panel.cc)
target_include_directories(pekwm_panel PUBLIC ${common_INCLUDE_DIRS})
target_link_libraries(pekwm_panel texture x11 util ${common_LIBRARIES})
install(TARGETS pekwm_panel DESTINATION bin)

if (ENABLE_IMAGE_PNG AND PNG_FOUND)
  add_executable(pekwm_screenshot
    pekwm_screenshot.cc)
  target_include_directories(pekwm_screenshot PUBLIC ${common_INCLUDE_DIRS})
  target_link_libraries(pekwm_screenshot texture x11 util ${common_LIBRARIES})
  install(TARGETS pekwm_screenshot DESTINATION bin)
endif (ENABLE_IMAGE_PNG AND PNG_FOUND)

add_executable(pekwm_wm pekwm_wm.cc)
target_include_directories(pekwm_wm PUBLIC ${common_INCLUDE_DIRS})
target_link_libraries(pekwm_wm wm texture x11 util ${common_LIBRARIES})
install(TARGETS pekwm_wm DESTINATION bin)
