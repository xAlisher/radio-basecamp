# Tier-1 offline unit tests (Qt::Test). Built only when -DRADIO_BUILD_TESTS=ON.
# No network, no AppImage, no delivery_module — mocks stand in (see tests/mocks/).
find_package(Qt6 REQUIRED COMPONENTS Core Test Network)

add_executable(test_radio
    test_radio.cpp
    mocks/radio_events_stub.cpp
    ../src/radio_plugin.cpp
)
target_include_directories(test_radio PRIVATE ../src)
target_link_libraries(test_radio PRIVATE Qt6::Core Qt6::Test Qt6::Network)
set_target_properties(test_radio PROPERTIES AUTOMOC ON)

add_test(NAME test_radio COMMAND test_radio)
