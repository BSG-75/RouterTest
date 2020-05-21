cmake_minimum_required(VERSION 3.17)
project(MyRouterTest)
include(BoostTest.cmake)

find_package(Boost 1.73 COMPONENTS unit_test_framework REQUIRED)
add_executable(${PROJECT_NAME} Test.cpp Routers.hpp Routers.cpp)

if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE "/W4" "$<$<CONFIG:RELEASE>:/O2>")
    target_compile_options(${PROJECT_NAME} PUBLIC "/permissive-" "/await" "/Zc:__cplusplus")
endif()


target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_17)
target_link_libraries(${PROJECT_NAME} PRIVATE Boost::headers Boost::unit_test_framework)
boost_test_discover_tests(${PROJECT_NAME} EXTRA_ARGS --log_level=message)