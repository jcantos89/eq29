cmake_minimum_required(VERSION 3.7)
project(DynEQ LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
add_subdirectory(dpf)

dpf_add_plugin(DynEQ
  TARGETS lv2
  UI_TYPE cairo
  FILES_DSP src/DynEQPlugin.cpp
  FILES_UI  src/DynEGUI.cpp)

target_include_directories(DynEQ PUBLIC src)
