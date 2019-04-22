# TestMultiGpuMultiMonitor

Test project for multi-GPU multi-monitor rendering on Windows with Nvidia Mosaic/Quadro Sync.

# Build

Obtain a copy of the NVAPI SDK (R410) and place it in the nvapi directory.
Obtain a copy of the GLEW SDK (2.1.0) and place it in the glew directory.

mkdir build
cd build
cmake .. -G "Visual Studio 15 2017 Win64"
