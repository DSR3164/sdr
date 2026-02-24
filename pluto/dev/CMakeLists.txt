cmake_minimum_required(VERSION 3.16)
project(PlutoSDR LANGUAGES CXX)

set(THIRD_PARTY_DIR ${PROJECT_SOURCE_DIR}/third_party)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Dependencies
find_package(SoapySDR REQUIRED)
find_package(OpenGL REQUIRED)
find_package(GLEW REQUIRED)
find_package(SDL2 REQUIRED)
find_package(PkgConfig REQUIRED)

pkg_check_modules(FFTW3F REQUIRED fftw3f)

# Third-party: imgui / implot
add_library(imgui STATIC
  ${THIRD_PARTY_DIR}/imgui/imgui.cpp
  ${THIRD_PARTY_DIR}/imgui/imgui_draw.cpp
  ${THIRD_PARTY_DIR}/imgui/imgui_demo.cpp
  ${THIRD_PARTY_DIR}/imgui/imgui_tables.cpp
  ${THIRD_PARTY_DIR}/imgui/imgui_widgets.cpp
  ${THIRD_PARTY_DIR}/imgui/backends/imgui_impl_sdl2.cpp
  ${THIRD_PARTY_DIR}/imgui/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui PUBLIC
  ${THIRD_PARTY_DIR}/imgui
  ${THIRD_PARTY_DIR}/imgui/backends
)
target_link_libraries(imgui PUBLIC ${SDL2_LIBRARIES} ${OPENGL_LIBRARIES})
target_compile_options(imgui PRIVATE -fPIC)

add_library(implot STATIC
  ${THIRD_PARTY_DIR}/implot/implot.cpp
  ${THIRD_PARTY_DIR}/implot/implot_demo.cpp
  ${THIRD_PARTY_DIR}/implot/implot_items.cpp
)
target_include_directories(implot PUBLIC ${THIRD_PARTY_DIR}/implot)
target_link_libraries(implot PUBLIC imgui)
target_compile_options(implot PRIVATE -fPIC)

add_library(implot3d STATIC
  ${THIRD_PARTY_DIR}/implot3d/implot3d.cpp
  ${THIRD_PARTY_DIR}/implot3d/implot3d_demo.cpp
  ${THIRD_PARTY_DIR}/implot3d/implot3d_items.cpp
  ${THIRD_PARTY_DIR}/implot3d/implot3d_meshes.cpp
)
target_include_directories(implot3d PUBLIC ${THIRD_PARTY_DIR}/implot3d)
target_link_libraries(implot3d PUBLIC imgui)
target_compile_options(implot3d PRIVATE -fPIC)

# Core library (your code once)
add_library(pluto_core STATIC
  src/pluto_lib.cpp
  src/dsp_module.cpp
  src/gui_module.cpp
)
target_include_directories(pluto_core PUBLIC
  ${PROJECT_SOURCE_DIR}/include
  ${THIRD_PARTY_DIR}/imgui
  ${THIRD_PARTY_DIR}/imgui/backends
)
target_link_libraries(pluto_core PUBLIC ${SoapySDR_LIBRARIES})

# FFTW3F include/lib flags
target_include_directories(pluto_core PRIVATE ${FFTW3F_INCLUDE_DIRS})
target_link_libraries(pluto_core PRIVATE ${FFTW3F_LIBRARIES})
target_compile_options(pluto_core PRIVATE ${FFTW3F_CFLAGS_OTHER})

# Executables
add_executable(main src/main.cpp)
target_link_libraries(main PRIVATE pluto_core)

add_executable(gui src/gui.cpp)
target_link_libraries(gui PRIVATE
  pluto_core
  imgui
  implot
  implot3d
  fftw3f_threads
  pthread
  ${SDL2_LIBRARIES}
  ${OPENGL_LIBRARIES}
  ${GLEW_LIBRARIES}
)

set_property(TARGET gui PROPERTY CXX_INCLUDE_WHAT_YOU_USE iwyu)

# Warnings (optional but полезно)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(pluto_core PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion)
  target_compile_options(gui PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(main PRIVATE -Wall -Wextra -Wpedantic)
endif()
