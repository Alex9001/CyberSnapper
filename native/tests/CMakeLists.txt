qt_add_executable(cybersnapper-core-tests TestCore.cpp)
target_link_libraries(cybersnapper-core-tests PRIVATE cybersnapper_core Qt6::Test)
add_test(NAME cybersnapper-core-tests COMMAND cybersnapper-core-tests)
