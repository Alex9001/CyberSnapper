cmake_minimum_required(VERSION 3.24)

project(CyberSnapper VERSION 2.2.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

option(CYBERSNAPPER_BUILD_TESTS "Build native CyberSnapper tests" ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets Network Sql Svg)

add_subdirectory(native)

if(CYBERSNAPPER_BUILD_TESTS)
  enable_testing()
  find_package(Qt6 6.8 REQUIRED COMPONENTS Test)
  add_subdirectory(native/tests)
endif()

set(CPACK_PACKAGE_NAME "CyberSnapper")
set(CPACK_PACKAGE_VENDOR "CyberBrand")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Native cross-platform website screenshot studio for portfolios")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_INSTALL_DIRECTORY "CyberSnapper")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")
if(WIN32)
  set(CPACK_GENERATOR "ZIP;NSIS")
elseif(APPLE)
  set(CPACK_GENERATOR "DragNDrop;ZIP")
else()
  set(CPACK_GENERATOR "TGZ")
endif()
include(CPack)
