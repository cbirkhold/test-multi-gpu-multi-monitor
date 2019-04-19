
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <iostream>
#include <iomanip>

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <dxgi1_6.h>
#include <wrl.h>

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

using namespace Microsoft::WRL;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

namespace {

    DECLARE_HANDLE(HGPUNV);

    typedef struct _GPU_DEVICE {
        DWORD  cb;
        CHAR   DeviceName[32];
        CHAR   DeviceString[128];
        DWORD  Flags;
        RECT   rcVirtualScreen;
    } GPU_DEVICE, *PGPU_DEVICE;

    typedef BOOL(WINAPI* wglEnumGpusNV_f)(UINT iGpuIndex, HGPUNV* phGpu);
    typedef BOOL(WINAPI* wglEnumGpuDevicesNV_f)(HGPUNV hGpu, UINT iDeviceIndex, PGPU_DEVICE lpGpuDevice);

    LRESULT CALLBACK window_callback(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
    {
        return DefWindowProcA(hWnd, uMsg, wParam, lParam);
    }

}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int
main(int argc, char* argv[])
{
    //------------------------------------------------------------------------------
    // List DirectX adapters.
    if ((1)) {
        ComPtr<IDXGIFactory4> factory;

        if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
            std::cerr << "Failed to create DXGI factory!" << std::endl;
            return EXIT_FAILURE;
        }

        UINT adapter_index = 0;
        IDXGIAdapter* adapter = nullptr;

        while (factory->EnumAdapters(adapter_index, &adapter) != DXGI_ERROR_NOT_FOUND) {
            DXGI_ADAPTER_DESC adapter_desc = {};

            if (FAILED(adapter->GetDesc(&adapter_desc))) {
                std::cerr << "Failed to get adapter description!" << std::endl;
                continue;
            }

            std::cout << "Adapter " << adapter_index << ": ";
            std::wcout << adapter_desc.Description;
            std::cout << ", 0x" << std::hex << adapter_desc.AdapterLuid.HighPart << std::setfill('0') << std::setw(8) << adapter_desc.AdapterLuid.LowPart;
            std::cout << std::endl;

            UINT output_index = 0;
            IDXGIOutput* output = nullptr;

            while (adapter->EnumOutputs(output_index, &output) != DXGI_ERROR_NOT_FOUND) {
                DXGI_OUTPUT_DESC output_desc = {};

                if (FAILED(output->GetDesc(&output_desc))) {
                    std::cerr << "Failed to get output description!" << std::endl;
                    continue;
                }

                std::cout << "  Output " << output_index << ": ";
                std::wcout << output_desc.DeviceName;

                bool first = true;

                if (output_desc.AttachedToDesktop) {
                    if (first) { std::cout << " ("; } else { std::cout << ", "; }
                    std::cout << "display attached";
                    first = false;
                }

                MONITORINFO monitor_info = {};
                monitor_info.cbSize = sizeof(monitor_info);

                if (GetMonitorInfo(output_desc.Monitor, &monitor_info)) {
                    if (monitor_info.dwFlags & MONITORINFOF_PRIMARY) {
                        if (first) { std::cout << " ("; } else { std::cout << ", "; }
                        std::cout << "primary display";
                        first = false;
                    }
                }

                if (!first) { std::cout << ")"; }
                std::cout << std::endl;

                output->Release();
                ++output_index;
            }

            adapter->Release();
            ++adapter_index;
        }
    }

    //------------------------------------------------------------------------------
    // List OpenGL GPUs.
    if ((1)) {
        const DWORD style = (WS_OVERLAPPED | WS_CAPTION);

        RECT rect = {};
        {
            SetRect(&rect, 0, 0, 64, 64);
            AdjustWindowRect(&rect, style, FALSE);
        }

        WNDCLASSA wc = {};
        {
            wc.style = 0;
            wc.lpfnWndProc = window_callback;
            wc.cbClsExtra = 0;
            wc.cbWndExtra = 0;
            wc.hInstance = NULL;
            wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
            wc.hCursor = LoadCursor(NULL, IDC_ARROW);
            wc.hbrBackground = NULL;
            wc.lpszMenuName = NULL;
            wc.lpszClassName = "TestMultiGpuMultiMonitor";
        }

        RegisterClassA(&wc);

        const HWND window = CreateWindowA(wc.lpszClassName, "TestMultiGpuMultiMonitor",
            style, CW_USEDEFAULT, CW_USEDEFAULT,
            (rect.right - rect.left), (rect.bottom - rect.top),
            nullptr, nullptr, nullptr, nullptr);

        ShowWindow(window, SW_SHOWDEFAULT);
        UpdateWindow(window);

        const HDC display_context = GetDC(window);

        PIXELFORMATDESCRIPTOR pixel_format_desc = {};
        {
            pixel_format_desc.nSize = sizeof(pixel_format_desc);
            pixel_format_desc.nVersion = 1;
            pixel_format_desc.dwFlags = (PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER);
            pixel_format_desc.iPixelType = PFD_TYPE_RGBA;
        };

        const int pixel_format = ChoosePixelFormat(display_context, &pixel_format_desc);
        
        if (pixel_format == 0) {
            std::cerr << "Failed to choose pixel format!" << std::endl;
            return EXIT_FAILURE;
        }

        if (SetPixelFormat(display_context, pixel_format, &pixel_format_desc) != TRUE) {
            std::cerr << "Failed to set pixel format!" << std::endl;
            return EXIT_FAILURE;
        }

        const HGLRC gl_context = wglCreateContext(display_context);

        if (gl_context == NULL) {
            std::cerr << "Failed to create OpenGL context!" << std::endl;
            return EXIT_FAILURE;
        }

        wglMakeCurrent(display_context, gl_context);

        const wglEnumGpusNV_f wglEnumGpusNV = (wglEnumGpusNV_f)wglGetProcAddress("wglEnumGpusNV");
        const wglEnumGpuDevicesNV_f wglEnumGpuDevicesNV = (wglEnumGpuDevicesNV_f)wglGetProcAddress("wglEnumGpuDevicesNV");

        UINT gpu_index = 0;
        HGPUNV gpu;

        while (wglEnumGpusNV(gpu_index, &gpu)) {
            UINT device_index = 0;

            GPU_DEVICE gpu_device;
            gpu_device.cb = sizeof(gpu_device);

            std::cout << "GPU " << gpu_index << ":" << std::endl;

            while (wglEnumGpuDevicesNV(gpu, device_index, &gpu_device)) {
                std::cout << "  Device " << device_index << ": ";
                std::cout << gpu_device.DeviceString << ", " << gpu_device.DeviceName;
                std::cout << ", 0x" << std::hex << std::setfill('0') << std::setw(8) << gpu_device.Flags;

                if (gpu_device.Flags != 0) {
                    bool first = true;
                    
                    if (gpu_device.Flags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP) {
                        if (first) { std::cout << " ("; } else { std::cout << ", "; }
                        std::cout << "display attached";
                        first = false;
                    }

                    if (gpu_device.Flags & DISPLAY_DEVICE_PRIMARY_DEVICE) {
                        if (first) { std::cout << " ("; } else { std::cout << ", "; }
                        std::cout << "primary display";
                        first = false;
                    }

                    if (gpu_device.Flags & DISPLAY_DEVICE_UNSAFE_MODES_ON) {
                        if (first) { std::cout << " ("; } else { std::cout << ", "; }
                        std::cout << "unsafe modes on";
                        first = false;
                    }

                    if (!first) { std::cout << ")"; }
                }

                std::cout << std::endl;

                ++device_index;
            }

            ++gpu_index;
        }

        wglDeleteContext(gl_context);
        ReleaseDC(window, display_context);
        DestroyWindow(window);
    }

    return EXIT_SUCCESS;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
