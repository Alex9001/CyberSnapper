qt_add_executable(cybersnapper-core-tests TestCore.cpp)
target_link_libraries(cybersnapper-core-tests PRIVATE cybersnapper_core Qt6::Test)
add_test(NAME cybersnapper-core-tests COMMAND cybersnapper-core-tests)

qt_add_executable(cybersnapper-gui-tests
  TestGui.cpp
  ../src/gui/MainWindow.cpp
  ../resources/native.qrc
)
target_link_libraries(cybersnapper-gui-tests PRIVATE cybersnapper_core Qt6::Test Qt6::Widgets Qt6::Svg)
add_test(NAME cybersnapper-gui-tests COMMAND cybersnapper-gui-tests)
set_tests_properties(cybersnapper-gui-tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

qt_add_executable(cybersnapper-doc-fixture DocFixture.cpp)
target_link_libraries(cybersnapper-doc-fixture PRIVATE cybersnapper_core Qt6::Gui)
