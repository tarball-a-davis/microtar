add_executable(
    mtar_test
    mtar_test.cpp
)

target_link_libraries(
    mtar_test
    microtar
    GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(mtar_test)
