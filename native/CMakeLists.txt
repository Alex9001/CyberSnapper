set(CYBERSNAPPER_CORE_SOURCES
  src/core/Models.cpp
  src/core/Paths.cpp
  src/core/ProjectStore.cpp
  src/core/Rpc.cpp
  src/core/JobManager.cpp
  src/core/Scheduler.cpp
  src/core/RestServer.cpp
  src/core/AgentService.cpp
)

add_library(cybersnapper_core STATIC ${CYBERSNAPPER_CORE_SOURCES})
target_include_directories(cybersnapper_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(cybersnapper_core
  PUBLIC Qt6::Core Qt6::Network Qt6::Sql
)
target_compile_definitions(cybersnapper_core PUBLIC
  CYBERSNAPPER_VERSION="${PROJECT_VERSION}"
  CYBERSNAPPER_SOURCE_ROOT="${CMAKE_SOURCE_DIR}"
)

find_path(CPP_HTTPLIB_INCLUDE_DIR httplib.h)
if(NOT CPP_HTTPLIB_INCLUDE_DIR)
  include(FetchContent)
  FetchContent_Declare(
    cpp_httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    GIT_TAG v0.52.0
    GIT_SHALLOW TRUE
  )
  FetchContent_MakeAvailable(cpp_httplib)
  target_link_libraries(cybersnapper_core PRIVATE httplib::httplib)
else()
  target_include_directories(cybersnapper_core PRIVATE ${CPP_HTTPLIB_INCLUDE_DIR})
endif()

qt_add_executable(CyberSnapper
  src/gui/main.cpp
  src/gui/MainWindow.cpp
  resources/native.qrc
)
if(WIN32)
  target_sources(CyberSnapper PRIVATE resources/cybersnapper.rc)
endif()
target_link_libraries(CyberSnapper PRIVATE
  cybersnapper_core Qt6::Widgets Qt6::Svg
)

set(CYBERSNAPPER_MACOS_ICON "" CACHE FILEPATH "Generated CyberSnapper .icns file")
if(APPLE AND CYBERSNAPPER_MACOS_ICON)
  get_filename_component(CYBERSNAPPER_MACOS_ICON_NAME "${CYBERSNAPPER_MACOS_ICON}" NAME)
  set_source_files_properties("${CYBERSNAPPER_MACOS_ICON}" PROPERTIES
    MACOSX_PACKAGE_LOCATION "Resources")
  target_sources(CyberSnapper PRIVATE "${CYBERSNAPPER_MACOS_ICON}")
  set_target_properties(CyberSnapper PROPERTIES
    MACOSX_BUNDLE_ICON_FILE "${CYBERSNAPPER_MACOS_ICON_NAME}")
endif()
set_target_properties(CyberSnapper PROPERTIES
  WIN32_EXECUTABLE TRUE
  MACOSX_BUNDLE TRUE
  MACOSX_BUNDLE_GUI_IDENTIFIER net.cyberbrand.CyberSnapper
  MACOSX_BUNDLE_BUNDLE_NAME CyberSnapper
  MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
  MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
)

qt_add_executable(cybersnapper-agent src/agent/main.cpp resources/native.qrc)
target_link_libraries(cybersnapper-agent PRIVATE
  cybersnapper_core Qt6::Widgets Qt6::Svg
)

qt_add_executable(cybersnapper_cli src/cli/main.cpp)
target_link_libraries(cybersnapper_cli PRIVATE cybersnapper_core Qt6::Core Qt6::Network)
set_target_properties(cybersnapper_cli PROPERTIES OUTPUT_NAME cybersnapper-cli)

if(APPLE)
  install(TARGETS CyberSnapper BUNDLE DESTINATION .)
  install(TARGETS cybersnapper-agent cybersnapper_cli
    RUNTIME DESTINATION CyberSnapper.app/Contents/MacOS)
  set(CYBERSNAPPER_RUNTIME_DESTINATION CyberSnapper.app/Contents/Resources)
  set(CYBERSNAPPER_NODE_DESTINATION CyberSnapper.app/Contents/Resources/runtime)
else()
  install(TARGETS CyberSnapper cybersnapper-agent cybersnapper_cli RUNTIME DESTINATION bin)
  if(WIN32)
    set(CYBERSNAPPER_RUNTIME_DESTINATION bin)
    set(CYBERSNAPPER_NODE_DESTINATION bin/runtime)
  else()
    set(CYBERSNAPPER_RUNTIME_DESTINATION share/cybersnapper)
    set(CYBERSNAPPER_NODE_DESTINATION lib/cybersnapper/runtime)
    install(FILES packaging/net.cyberbrand.CyberSnapper.desktop
      DESTINATION share/applications)
    install(FILES packaging/net.cyberbrand.CyberSnapper.metainfo.xml
      DESTINATION share/metainfo)
    install(FILES ${CMAKE_SOURCE_DIR}/assets/cybersnapper-512.png
      DESTINATION share/pixmaps
      RENAME net.cyberbrand.CyberSnapper.png)
  endif()
endif()

install(DIRECTORY ${CMAKE_SOURCE_DIR}/worker/dist/
  DESTINATION ${CYBERSNAPPER_RUNTIME_DESTINATION}/worker OPTIONAL)
install(DIRECTORY ${CMAKE_SOURCE_DIR}/node_modules/
  DESTINATION ${CYBERSNAPPER_RUNTIME_DESTINATION}/worker/node_modules OPTIONAL
  PATTERN ".cache" EXCLUDE)
install(DIRECTORY ${CMAKE_SOURCE_DIR}/.playwright-browsers/
  DESTINATION ${CYBERSNAPPER_RUNTIME_DESTINATION}/browsers OPTIONAL
  USE_SOURCE_PERMISSIONS)
install(DIRECTORY ${CMAKE_SOURCE_DIR}/.runtime/
  DESTINATION ${CYBERSNAPPER_NODE_DESTINATION} OPTIONAL USE_SOURCE_PERMISSIONS)
install(FILES ${CMAKE_SOURCE_DIR}/README.md ${CMAKE_SOURCE_DIR}/LICENSE DESTINATION .)
install(DIRECTORY ${CMAKE_SOURCE_DIR}/docs/ DESTINATION docs)
