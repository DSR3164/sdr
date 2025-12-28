cmake_minimum_required(VERSION 3.16)
project(PlutoSDR CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

find_package(SoapySDR REQUIRED)
find_package(pybind11 REQUIRED)
include_directories(include)

set(MAIN_SOURCE_FILES
    main.cpp
    src/pluto_lib.cpp
)

add_executable(main ${MAIN_SOURCE_FILES})
target_link_libraries(main ${SoapySDR_LIBRARIES})

pybind11_add_module(cpp
    src/py_module.cpp
    src/pluto_lib.cpp
)

target_include_directories(cpp PRIVATE include)
target_link_libraries(cpp PRIVATE ${SoapySDR_LIBRARIES})
target_compile_options(cpp PRIVATE -O3 -march=native)
