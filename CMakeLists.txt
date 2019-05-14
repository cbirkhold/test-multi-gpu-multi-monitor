
cmake_minimum_required(VERSION 3.5)
project(TestMultiGpuMultiMonitor)

add_executable(TestMultiGpuMultiMonitor main.cpp OpenGLUtilities.cpp)

target_include_directories(TestMultiGpuMultiMonitor PRIVATE glew/include)
target_include_directories(TestMultiGpuMultiMonitor PRIVATE nvapi)
target_include_directories(TestMultiGpuMultiMonitor PRIVATE $ENV{CUDA_PATH}/include)

target_link_libraries(TestMultiGpuMultiMonitor OpenGL32 DXGI)
target_link_libraries(TestMultiGpuMultiMonitor ../nvapi/amd64/nvapi64)
target_link_libraries(TestMultiGpuMultiMonitor ../glew/lib/Release/x64/glew32)
target_link_libraries(TestMultiGpuMultiMonitor $ENV{CUDA_PATH}/lib/x64/cuda.lib)
