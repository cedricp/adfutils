cmake_minimum_required(VERSION 3.5.0)


project(adf_utils C CXX)

set(CMAKE_MODULE_PATH ${tapetools_project_SOURCE_DIR}/cmake ${CMAKE_MODULE_PATH})
set(CMAKE_INSTALL_PREFIX ${tapetools_project_SOURCE_DIR}/INSTALL_${CMAKE_BUILD_TYPE})

set(OpenGL_GL_PREFERENCE "GLVND")
find_package(OpenGL REQUIRED)
find_package(GLEW REQUIRED)
find_package(SDL2 REQUIRED)

add_subdirectory(hw_lib)
add_subdirectory(adf_lib)
add_subdirectory(libimgui)

add_executable(adfutils main.cpp)
target_include_directories(adfutils PRIVATE hw_lib libimgui)
target_link_libraries(adfutils imgui_static hw_lib adf_lib)
