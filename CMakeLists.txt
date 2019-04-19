
cmake_minimum_required(VERSION 3.5)
project(TestMultiGpuMultiMonitor)

add_executable(TestMultiGpuMultiMonitor main.cpp)
target_link_libraries(TestMultiGpuMultiMonitor OpenGL32 DXGI)
