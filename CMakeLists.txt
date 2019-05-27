
cmake_minimum_required(VERSION 3.5)
project(TestMultiGpuMultiMonitor)

add_executable(TestMultiGpuMultiMonitor main.cpp OpenGLUtilities.cpp)

target_include_directories(TestMultiGpuMultiMonitor PRIVATE $ENV{CUDA_PATH}/include)
target_include_directories(TestMultiGpuMultiMonitor PRIVATE sdks/glew/include)
target_include_directories(TestMultiGpuMultiMonitor PRIVATE sdks/glfw/include)
target_include_directories(TestMultiGpuMultiMonitor PRIVATE sdks/nvapi)
target_include_directories(TestMultiGpuMultiMonitor PRIVATE sdks/openvr/headers)

target_link_libraries(TestMultiGpuMultiMonitor OpenGL32 DXGI)
target_link_libraries(TestMultiGpuMultiMonitor $ENV{CUDA_PATH}/lib/x64/cuda.lib)
target_link_libraries(TestMultiGpuMultiMonitor ../sdks/glew/lib/Release/x64/glew32)
target_link_libraries(TestMultiGpuMultiMonitor ../sdks/glfw/lib-vc2017/glfw3)
target_link_libraries(TestMultiGpuMultiMonitor ../sdks/nvapi/amd64/nvapi64)
target_link_libraries(TestMultiGpuMultiMonitor ../sdks/openvr/lib/win64/openvr_api)
