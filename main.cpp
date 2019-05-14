
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <algorithm>
#include <array>
#include <cassert>
#include <future>
#include <iostream>
#include <iomanip>
#include <list>
#include <string>
#include <vector>

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define WIN32_LEAN_AND_MEAN
#include <dxgi1_6.h>
#include <Windows.h>
#include <wrl.h>

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <GL/glew.h>
#include <GL/wglew.h>
#include <nvapi.h>
#include <cuda.h>
#include <cudaGL.h>

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "OpenGLUtilities.h"

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

using namespace Microsoft::WRL;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

namespace {

    //------------------------------------------------------------------------------
    // WGL_NV_gpu_affinity
    //------------------------------------------------------------------------------

#ifndef WGL_NV_gpu_affinity

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

    typedef HDC(WINAPI* wglCreateAffinityDCNV_f)(const HGPUNV *phGpuList);
    typedef BOOL(WINAPI* wglEnumGpusFromAffinityDCNV_f)(HDC hAffinityDC, UINT iGpuIndex, HGPUNV *hGpu);
    typedef BOOL(WINAPI* wglDeleteDCNV_f)(HDC hdc);

    wglEnumGpusNV_f wglEnumGpusNV = nullptr;
    wglEnumGpuDevicesNV_f wglEnumGpuDevicesNV = nullptr;
    wglCreateAffinityDCNV_f wglCreateAffinityDCNV = nullptr;
    wglEnumGpusFromAffinityDCNV_f wglEnumGpusFromAffinityDCNV = nullptr;
    wglDeleteDCNV_f wglDeleteDCNV = nullptr;

#endif // WGL_NV_gpu_affinity

    //------------------------------------------------------------------------------
    // Utilities
    //------------------------------------------------------------------------------

    void log_last_error_message()
    {
        const DWORD last_error = GetLastError();
        LPSTR last_error_message = nullptr;

        FormatMessageA((FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS),
            NULL, last_error, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
            (LPTSTR)&last_error_message, 0, NULL);

        fprintf(stderr, "%ju: %s", uintmax_t(last_error), last_error_message);

        LocalFree(last_error_message);
    }

    LRESULT CALLBACK window_callback(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
    {
        return DefWindowProc(hWnd, uMsg, wParam, lParam);
    }

    void print_to_stream(std::ostream& stream, const NV_MOSAIC_GRID_TOPO& display_grid, const std::string& indent)
    {
        stream << indent << display_grid.rows << "x" << display_grid.columns << " (" << display_grid.displayCount << (display_grid.displayCount == 1 ? " display)" : " displays)") << std::endl;
        stream << indent << display_grid.displaySettings.width << "x" << display_grid.displaySettings.height << " @ " << display_grid.displaySettings.freq << " Hz" << std::endl;

        for (size_t r = 0; r < display_grid.rows; ++r) {
            for (size_t c = 0; c < display_grid.columns; ++c) {
                std::cout << indent << "[" << r << "," << c << "] 0x" << std::hex << std::setfill('0') << std::setw(8) << display_grid.displays[c + (r * display_grid.columns)].displayId << std::dec << std::endl;
            }

            std::cout << std::endl;
        }
    }

    void print_display_flags_to_stream(std::ostream& stream, DWORD flags)
    {
        stream << "0x" << std::hex << std::setfill('0') << std::setw(8) << flags << std::dec;

        if (flags != 0) {
            bool first = true;

            if ((flags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP) == DISPLAY_DEVICE_ATTACHED_TO_DESKTOP) {
                if (first) { stream << " ("; }
                else { stream << ", "; }
                stream << "display attached";
                first = false;
            }

            if ((flags & DISPLAY_DEVICE_PRIMARY_DEVICE) == DISPLAY_DEVICE_PRIMARY_DEVICE) {
                if (first) { stream << " ("; }
                else { stream << ", "; }
                stream << "primary display";
                first = false;
            }

            if ((flags & DISPLAY_DEVICE_UNSAFE_MODES_ON) == DISPLAY_DEVICE_UNSAFE_MODES_ON) {
                if (first) { stream << " ("; }
                else { stream << ", "; }
                stream << "unsafe modes on";
                first = false;
            }

            if (!first) { stream << ")"; }
        }
    }

    void create_texture_backed_render_targets(GLuint* const framebuffers,
        GLuint* const color_attachments,
        size_t n,
        size_t width,
        size_t height)
    {
        glGenFramebuffers(GLsizei(n), framebuffers);
        glGenTextures(GLsizei(n), color_attachments);

        for (size_t i = 0; i < n; ++i) {
            glBindFramebuffer(GL_FRAMEBUFFER, framebuffers[i]);
            glBindTexture(GL_TEXTURE_2D, color_attachments[i]);

            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);

            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, GLsizei(width), GLsizei(height), 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color_attachments[i], 0);

            const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);

            if (status != GL_FRAMEBUFFER_COMPLETE) {
                throw std::runtime_error("Failed to validate framebuffer status!");
            }
        }
    }

    void delete_texture_backed_render_targets(const GLuint* const framebuffers, const GLuint* const color_attachments, size_t n)
    {
        glDeleteTextures(GLsizei(n), color_attachments);
        glDeleteFramebuffers(GLsizei(n), framebuffers);
    }

    std::mutex render_threads_mutex;
    std::list<std::future<void>> render_threads; 
    std::condition_variable start_render_threads_event;
    bool start_render_threads_flag = false;

    void start_render_threads(std::vector<HDC> display_contexts, std::vector<HGLRC> gl_contexts, const std::function<void(size_t)>& initialize, const std::function<void(size_t)>& render)
    {
        //------------------------------------------------------------------------------
        // Check arguments.
        assert(display_contexts.size() == gl_contexts.size());

        //------------------------------------------------------------------------------
        // Synchronize this function.
        std::unique_lock<std::mutex> lock(render_threads_mutex);
        start_render_threads_flag = false;

        //------------------------------------------------------------------------------
        // Start all threads and let them do their setup.
        for (size_t thread_index = 0; thread_index < display_contexts.size(); ++thread_index) {
            std::cout << "Starting render thread " << thread_index << std::endl;

            const HDC display_context = display_contexts[thread_index];
            const HGLRC gl_context = gl_contexts[thread_index];
            std::promise<void> render_thread_ready;

            std::future<void> render_thread = std::async(std::launch::async, [&render_thread_ready](
                HDC display_context,
                HGLRC gl_context,
                const std::function<void(size_t)>& initialize,
                const std::function<void(size_t)>& render,
                size_t thread_index)
            {
                try {
                    //------------------------------------------------------------------------------
                    // Prepare for rendering.
                    if (wglMakeCurrent(display_context, gl_context) != TRUE) {
                        std::cerr << "Error: Failed to make OpenGL context current: ";
                        log_last_error_message();
                        throw std::runtime_error("Failed to make OpenGL context current!");
                    }

                    //------------------------------------------------------------------------------
                    // Check associated CUDA device.
                    unsigned int cuda_device_count = 0;
                    std::array<CUdevice, 4> cuda_devices;

                    if (cuGLGetDevices(&cuda_device_count, cuda_devices.data(), unsigned int(cuda_devices.size()), CU_GL_DEVICE_LIST_ALL) == CUDA_SUCCESS) {
                        for (size_t i = 0; i < cuda_device_count; ++i) {
                            std::cout << "  CUDA device: " << cuda_devices[i] << std::endl;
                        }
                    }

                    //------------------------------------------------------------------------------
                    // Initialize.
                    initialize(thread_index);

                    //------------------------------------------------------------------------------
                    // Signal we are ready for rendering then wait for signal to start rendering.
                    render_thread_ready.set_value();

                    {
                        std::unique_lock<std::mutex> lock(render_threads_mutex);
                        start_render_threads_event.wait(lock, []() { return start_render_threads_flag; });
                    }

                    //------------------------------------------------------------------------------
                    // Render.
                    render(thread_index);
                }
                catch (std::exception& e) {
                    std::cerr << "Exception: " << e.what() << std::endl;
                }
                catch (...) {
                    std::cerr << "Exception: <unknown>!" << std::endl;
                }
            }, display_context, gl_context, initialize, render, thread_index);

            //------------------------------------------------------------------------------
            // Wait for the thread to be ready for rendering.
            render_thread_ready.get_future().wait();
            render_threads.emplace_back(std::move(render_thread));
        }

        //------------------------------------------------------------------------------
        // Signal all render threads to start.
        start_render_threads_flag = true;
        start_render_threads_event.notify_all();
    }

    void join_render_thread(std::future<void>& render_thread)
    {
        try {
            render_thread.get();
        }
        catch (std::exception& e) {
            std::cerr << "Exception: " << e.what() << std::endl;
        }
        catch (...) {
            std::cerr << "Exception: <unknown>!" << std::endl;
        }
    }

    void join_render_threads()
    {
        std::unique_lock<std::mutex> lock(render_threads_mutex);

        for (auto it = begin(render_threads); it != end(render_threads); ++it) {
            if (it->valid() == false) {
                continue;
            }

            join_render_thread(*it);
        }

        render_threads.clear();
    }

    bool try_join_render_threads(size_t timeout)
    {
        std::unique_lock<std::mutex> lock(render_threads_mutex);

        for (auto it = begin(render_threads); it != end(render_threads);) {
            if (it->valid() == false) {
                it = render_threads.erase(it);
                continue;
            }

            if (it->wait_for(std::chrono::milliseconds(timeout)) == std::future_status::ready) {
                join_render_thread(*it);
                it = render_threads.erase(it);
                continue;
            }

            ++it;
        }

        return render_threads.empty();
    }

    class RenderPoints
    {
    public:

        static GLuint create_program()
        {
            static const char* const vs_string =
                "#version 410\n"
                "uniform vec4 u_rect;\n"
                "uniform mat4 u_mvp;\n"
                "out vec2 v_uv;\n"
                "void main() {\n"
                "    int x = (gl_VertexID % 1024);\n"
                "    int y = (gl_VertexID / 1024);\n"
                "    vec2 uv = (vec2(x, y) * (1.0 / 1023.0));\n"
                "    gl_Position = (u_mvp * vec4((u_rect.xy + (uv * u_rect.zw)), 0.0, 1.0));\n"
                "    v_uv = vec2(uv.x, uv.y);\n"
                "}\n";

            static const char* const fs_string =
                "#version 410\n"
                "in vec2 v_uv;\n"
                "out vec4 f_color;\n"
                "void main() {\n"
                "    float vignette = pow(clamp(((v_uv.x * (1.0f - v_uv.x)) * (v_uv.y * (1.0f - v_uv.y)) * 36.0f), 0.0, 1.0), 4.0);\n"
                "    f_color = vec4((v_uv.rg * vignette), 0.0, 1.0);\n"
                "}\n";

            try {
                const GLuint vertex_shader = toolbox::OpenGLShader::create_from_source(GL_VERTEX_SHADER, vs_string);
                const GLuint fragment_shader = toolbox::OpenGLShader::create_from_source(GL_FRAGMENT_SHADER, fs_string);

                toolbox::OpenGLProgram::attribute_location_list_t attribute_locations;
                toolbox::OpenGLProgram::frag_data_location_list_t frag_data_locations;
                const GLuint program = toolbox::OpenGLProgram::create_from_shaders(vertex_shader, fragment_shader, attribute_locations, frag_data_locations);

                s_uniform_location_rect = glGetUniformLocation(program, "u_rect");
                s_uniform_location_mvp = glGetUniformLocation(program, "u_mvp");

                return program;
            }
            catch (std::exception& e) {
                std::cerr << "Exception: " << e.what() << std::endl;
            }
            catch (...) {
                std::cerr << "Exception: <unknown>!" << std::endl;
            }

            return 0;
        }

        static void set_rect(const float* const ndc_rect)
        {
            if (s_uniform_location_rect != -1) {
                glUniform4fv(s_uniform_location_rect, 1, ndc_rect);
            }
        }

        static void set_mvp(const float* const mvp)
        {
            if (s_uniform_location_mvp != -1) {
                glUniformMatrix4fv(s_uniform_location_mvp, 1, GL_FALSE, mvp);
            }
        }

        static void draw(GLuint& vao)
        {
            if (!vao) {
                glGenVertexArrays(1, &vao);
            }

            glBindVertexArray(vao);
            glDrawArrays(GL_POINTS, 0, (1024 * 1024));
        }

    private:

        static GLint        s_uniform_location_rect;
        static GLint        s_uniform_location_mvp;
    };

    GLint RenderPoints::s_uniform_location_rect = -1;
    GLint RenderPoints::s_uniform_location_mvp = -1;

    //------------------------------------------------------------------------------
    // Global data.
    //------------------------------------------------------------------------------

    float rect[4] = { -1.0, -1.0, 2.0, 2.0 };

    float mvp[16] = {
        1.0, 0.0, 0.0, 0.0,
        0.0, 1.0, 0.0, 0.0,
        0.0, 0.0, 1.0, 0.0,
        0.0, 0.0, 0.0, 1.0,
    };

    uint8_t pixels[4][64 * 64 * 4];

    typedef struct rect_s {
        long        m_x = 0;
        long        m_y = 0;
        long        m_width = 0;
        long        m_height = 0;
    } rect_t;

    rect_t virtual_screen;
    long num_virtual_screen_monitors = 0;
    std::vector<rect_t> virtual_screen_monitors;

    //------------------------------------------------------------------------------
    // Windows API
    //------------------------------------------------------------------------------

    int windows()
    {
        std::cout << "[Windows API]" << std::endl;

        //------------------------------------------------------------------------------
        // Get the Virtual Screen geometry.
        virtual_screen.m_x = GetSystemMetrics(SM_XVIRTUALSCREEN);
        virtual_screen.m_y = GetSystemMetrics(SM_YVIRTUALSCREEN);
        virtual_screen.m_width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
        virtual_screen.m_height = GetSystemMetrics(SM_CYVIRTUALSCREEN);
        num_virtual_screen_monitors = GetSystemMetrics(SM_CMONITORS);

        std::cout << std::endl;
        std::cout << "Virtual Screen origin: " << virtual_screen.m_x << " / " << virtual_screen.m_y << std::endl;
        std::cout << "Virtual Screen size: " << virtual_screen.m_width << " x " << virtual_screen.m_height << std::endl;
        std::cout << "Virtual Screen spans " << num_virtual_screen_monitors << " monitor(s)" << std::endl;

        //------------------------------------------------------------------------------
        // Enumerate display devices.
        std::cout << std::endl;

        DISPLAY_DEVICE display_device = {};
        display_device.cb = sizeof(display_device);

        DWORD display_device_index = 0;

        while (EnumDisplayDevices(nullptr, display_device_index, &display_device, 0)) {
            std::cout << "Display " << display_device.DeviceName << ", " << display_device.DeviceString << std::endl;
            std::cout << "  " << display_device.DeviceID << std::endl;
            std::cout << "  " << display_device.DeviceKey << std::endl;
            std::cout << "  "; print_display_flags_to_stream(std::cout, display_device.StateFlags); std::cout << std::endl;

            ++display_device_index;

            DEVMODE device_mode = {};
            device_mode.dmSize = sizeof(device_mode);

            //------------------------------------------------------------------------------
            // Enumerate/Get current display settings.
            if ((0)) {
                DWORD mode_index = 0;

                while (EnumDisplaySettingsEx(display_device.DeviceName, mode_index, &device_mode, 0)) {
                    std::cout << "  " << device_mode.dmPelsWidth << " x " << device_mode.dmPelsHeight << " @ " << device_mode.dmDisplayFrequency << " Hz" << std::endl;
                    ++mode_index;
                }
            }
            else {
                if (EnumDisplaySettingsEx(display_device.DeviceName, ENUM_CURRENT_SETTINGS, &device_mode, 0)) {
                    std::cout << "  " << device_mode.dmPelsWidth << " x " << device_mode.dmPelsHeight << " @ " << device_mode.dmDisplayFrequency << " Hz" << std::endl;
                }
            }
        }

        //------------------------------------------------------------------------------
        // Enumerate display monitors.
        //
        // Each physical display is represented by a monitor handle of type HMONITOR. 
        std::cout << std::endl;

        if (EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR monitor, HDC display_context, LPRECT virtual_screen_rect, LPARAM user_data) {
            //------------------------------------------------------------------------------
            // Add to list of monitors making up the virtual screen.
            rect_t virtual_screen_monitor = {};
            {
                virtual_screen_monitor.m_x = virtual_screen_rect->left;
                virtual_screen_monitor.m_y = virtual_screen_rect->top;
                virtual_screen_monitor.m_width = (virtual_screen_rect->right - virtual_screen_rect->left);
                virtual_screen_monitor.m_height = (virtual_screen_rect->bottom - virtual_screen_rect->top);
            }

            virtual_screen_monitors.push_back(virtual_screen_monitor);

            //------------------------------------------------------------------------------
            // Get additional monitor info.
            MONITORINFOEX monitor_info = {};
            monitor_info.cbSize = sizeof(monitor_info);

            bool is_primary = false;

            if (GetMonitorInfo(monitor, &monitor_info) != 0) {
                std::cout << "Monitor " << monitor_info.szDevice << ": ";
                is_primary = ((monitor_info.dwFlags & MONITORINFOF_PRIMARY) == MONITORINFOF_PRIMARY);
            }
            else {
                std::cout << "Monitor 0x" << monitor << ": ";
            }

            std::cout << "(" << virtual_screen_monitor.m_x << " / " << virtual_screen_monitor.m_y << ") [" << virtual_screen_monitor.m_width << " x " << virtual_screen_monitor.m_height << "]";

            if (is_primary) {
                std::cout << " (primary)";
            }

            std::cout << std::endl;

            return TRUE;
        }, 0) == 0)
        {
            std::cerr << "Error: Failed to enumerate monitors!" << std::endl;
            return EXIT_FAILURE;
        }

        if (virtual_screen_monitors.size() != num_virtual_screen_monitors) {
            std::cerr << "Warning: EnumDisplayMonitors() returned more monitors than GetSystemMetrics() reported to be part of the virtual screen!" << std::endl;
        }

        //------------------------------------------------------------------------------
        // ...
        return EXIT_SUCCESS;
    }

    //------------------------------------------------------------------------------
    // NVAPI based GPU query
    //------------------------------------------------------------------------------

    int nvapi()
    {
        std::cout << "[NVAPI]" << std::endl;

        //------------------------------------------------------------------------------
        // Initialize NVAPI.
        if (NvAPI_Initialize() != NVAPI_OK) {
            std::cerr << "Error: Failed to initialize NVAPI!" << std::endl;
            return EXIT_FAILURE;
        }

        //------------------------------------------------------------------------------
        // Print interface version string.
        NvAPI_ShortString interface_version = {};

        if (NvAPI_GetInterfaceVersionString(interface_version) == NVAPI_OK) {
            std::cout << std::endl;
            std::cout << "NVAPI interface version: " << interface_version << std::endl;
        }

        //------------------------------------------------------------------------------
        // Get brief of current mosaic topology.
        NV_MOSAIC_TOPO_BRIEF mosaic_topology = {};
        mosaic_topology.version = NVAPI_MOSAIC_TOPO_BRIEF_VER;

        NV_MOSAIC_DISPLAY_SETTING mosaic_display_settings = {};
        mosaic_display_settings.version = NVAPI_MOSAIC_DISPLAY_SETTING_VER;

        NvS32 mosaic_overlap_x = 0;
        NvS32 mosaic_overlap_y = 0;

        if (NvAPI_Mosaic_GetCurrentTopo(&mosaic_topology, &mosaic_display_settings, &mosaic_overlap_x, &mosaic_overlap_y) != NVAPI_OK) {
            std::cerr << "Error: Failed to get mosaic topology!" << std::endl;
            return EXIT_FAILURE;
        }

        //------------------------------------------------------------------------------
        // If a topology is enabled show which one.
        std::cout << std::endl;

        if (mosaic_topology.enabled) {
            std::cout << "Mosaic is ENABLED: ";

            switch (mosaic_topology.topo) {
            case NV_MOSAIC_TOPO_1x2_BASIC: std::cout << "1x2"; break;
            case NV_MOSAIC_TOPO_2x1_BASIC: std::cout << "2x1"; break;
            case NV_MOSAIC_TOPO_1x3_BASIC: std::cout << "1x3"; break;
            case NV_MOSAIC_TOPO_3x1_BASIC: std::cout << "3x1"; break;
            case NV_MOSAIC_TOPO_1x4_BASIC: std::cout << "1x4"; break;
            case NV_MOSAIC_TOPO_4x1_BASIC: std::cout << "4x1"; break;
            case NV_MOSAIC_TOPO_2x2_BASIC: std::cout << "2x2"; break;
            case NV_MOSAIC_TOPO_2x3_BASIC: std::cout << "2x3"; break;
            case NV_MOSAIC_TOPO_2x4_BASIC: std::cout << "2x4"; break;
            case NV_MOSAIC_TOPO_3x2_BASIC: std::cout << "3x2"; break;
            case NV_MOSAIC_TOPO_4x2_BASIC: std::cout << "4x2"; break;
            case NV_MOSAIC_TOPO_1x5_BASIC: std::cout << "1x5"; break;
            case NV_MOSAIC_TOPO_1x6_BASIC: std::cout << "1x6"; break;
            case NV_MOSAIC_TOPO_7x1_BASIC: std::cout << "1x7"; break;
            case NV_MOSAIC_TOPO_1x2_PASSIVE_STEREO: std::cout << "1x2 passive stereo"; break;
            case NV_MOSAIC_TOPO_2x1_PASSIVE_STEREO: std::cout << "2x1 passive stereo"; break;
            case NV_MOSAIC_TOPO_1x3_PASSIVE_STEREO: std::cout << "1x3 passive stereo"; break;
            case NV_MOSAIC_TOPO_3x1_PASSIVE_STEREO: std::cout << "3x1 passive stereo"; break;
            case NV_MOSAIC_TOPO_1x4_PASSIVE_STEREO: std::cout << "1x4 passive stereo"; break;
            case NV_MOSAIC_TOPO_4x1_PASSIVE_STEREO: std::cout << "4x1 passive stereo"; break;
            case NV_MOSAIC_TOPO_2x2_PASSIVE_STEREO: std::cout << "2x2 passive stereo"; break;
            default: std::cout << "unknown topology"; break;
            }

            std::cout << std::endl;
        }
        else if (mosaic_topology.isPossible) {
            std::cout << "Mosaic is DISABLED but supported" << std::endl;
        }

        //------------------------------------------------------------------------------
        // Show current display grid (mosaic) configuration, including where mosaic is
        // disabled and each display is a 1x1 grid.
        if (mosaic_topology.isPossible) {
            NvU32 num_grids = 0;

            if (NvAPI_Mosaic_EnumDisplayGrids(nullptr, &num_grids) != NVAPI_OK) {
                std::cerr << "Error: Failed to enumerate display grids!" << std::endl;
                return EXIT_FAILURE;
            }

            std::vector<NV_MOSAIC_GRID_TOPO> display_grids(num_grids);

            std::for_each(begin(display_grids), end(display_grids), [](NV_MOSAIC_GRID_TOPO& display_grid) {
                display_grid.version = NV_MOSAIC_GRID_TOPO_VER;
            });

            if (NvAPI_Mosaic_EnumDisplayGrids(display_grids.data(), &num_grids) != NVAPI_OK) {
                std::cerr << "Error: Failed to enumerate display grids!" << std::endl;
                return EXIT_FAILURE;
            }

            assert(display_grids.size() >= num_grids);  // In some cases the initially reported number appears to be conservative!
            display_grids.resize(num_grids);

            std::for_each(begin(display_grids), end(display_grids), [](NV_MOSAIC_GRID_TOPO& display_grid) {
                print_to_stream(std::cout, display_grid, "  ");
            });
        }

        //------------------------------------------------------------------------------
        // Enumerate logical GPUs and the physical GPUs underneath it.
        NvLogicalGpuHandle logical_gpus[NVAPI_MAX_LOGICAL_GPUS] = {};
        NvU32 num_logical_gpus = 0;

        if (NvAPI_EnumLogicalGPUs(logical_gpus, &num_logical_gpus) != NVAPI_OK) {
            std::cerr << "Error: Failed to enumerate logical GPUs!" << std::endl;
            return EXIT_FAILURE;
        }

        NvPhysicalGpuHandle physical_gpus[NVAPI_MAX_PHYSICAL_GPUS] = { 0 };
        NvU32 num_physical_gpus = 0;
        NvU32 total_num_physical_gpus = 0;

        for (NvU32 logical_gpu_index = 0; logical_gpu_index < num_logical_gpus; ++logical_gpu_index) {
            std::cout << "Logical GPU " << logical_gpu_index << std::endl;

            if (NvAPI_GetPhysicalGPUsFromLogicalGPU(logical_gpus[logical_gpu_index], physical_gpus, &num_physical_gpus) != NVAPI_OK) {
                std::cerr << "Error: Failed to enumerate physical GPUs!" << std::endl;
                continue;
            }

            total_num_physical_gpus += num_physical_gpus;

            for (size_t physical_gpu_index = 0; physical_gpu_index < num_physical_gpus; ++physical_gpu_index) {
                NvAPI_ShortString name = {};

                if (NvAPI_GPU_GetFullName(physical_gpus[physical_gpu_index], name) != NVAPI_OK) {
                    std::cerr << "Error: Failed to get GPU name!" << std::endl;
                    continue;
                }

                std::cout << "  Physical GPU " << physical_gpu_index << ": " << name << std::endl;

                NvU32 num_displays = 0;

                if (NvAPI_GPU_GetAllDisplayIds(physical_gpus[physical_gpu_index], nullptr, &num_displays) != NVAPI_OK) {
                    std::cerr << "Error: Failed to get conencted displays!" << std::endl;
                    continue;
                }

                std::vector<NV_GPU_DISPLAYIDS> displays(num_displays);

                std::for_each(begin(displays), end(displays), [](NV_GPU_DISPLAYIDS& display) {
                    display.version = NV_GPU_DISPLAYIDS_VER;
                });

                if (NvAPI_GPU_GetAllDisplayIds(physical_gpus[physical_gpu_index], displays.data(), &num_displays) != NVAPI_OK) {
                    std::cerr << "Error: Failed to get conencted displays!" << std::endl;
                    continue;
                }

                assert(displays.size() >= num_displays);    // In some cases the initially reported number appears to be conservative!
                displays.resize(num_displays);

                for (size_t display_index = 0; display_index < displays.size(); ++display_index) {
                    std::cout << "    Display " << display_index << ": ";

                    switch (displays[display_index].connectorType) {
                    case NV_MONITOR_CONN_TYPE_VGA: std::cout << "VGA"; break;
                    case NV_MONITOR_CONN_TYPE_COMPONENT: std::cout << "Component"; break;
                    case NV_MONITOR_CONN_TYPE_SVIDEO: std::cout << "S-Video"; break;
                    case NV_MONITOR_CONN_TYPE_HDMI: std::cout << "HDMI"; break;
                    case NV_MONITOR_CONN_TYPE_DVI: std::cout << "DVI"; break;
                    case NV_MONITOR_CONN_TYPE_LVDS: std::cout << "LVDS"; break;
                    case NV_MONITOR_CONN_TYPE_DP: std::cout << "DP"; break;
                    case NV_MONITOR_CONN_TYPE_COMPOSITE: std::cout << "Composite"; break;
                    default: std::cout << "Unknown"; break;
                    }

                    std::cout << ", 0x" << std::hex << std::setfill('0') << std::setw(8) << displays[display_index].displayId << std::dec;

                    if (displays[display_index].isDynamic) {
                        std::cout << ", dynamic";
                    }

                    if (displays[display_index].isActive) {
                        std::cout << ", active";
                    }

                    if (displays[display_index].isCluster) {
                        std::cout << ", cluster";
                    }

                    if (displays[display_index].isOSVisible) {
                        std::cout << ", OS visible";
                    }

                    if (displays[display_index].isWFD) {
                        std::cout << ", wireless";
                    }

                    if (displays[display_index].isConnected) {
                        if (displays[display_index].isPhysicallyConnected) {
                            std::cout << ", physically connected";
                        }
                        else {
                            std::cout << ", connected";
                        }
                    }

                    std::cout << std::endl;
                }
            }
        }

        if (NvAPI_EnumPhysicalGPUs(physical_gpus, &num_physical_gpus) != NVAPI_OK) {
            std::cerr << "Error: Failed to enumerate physical GPUs!" << std::endl;
            return EXIT_FAILURE;
        }

        assert(num_physical_gpus == total_num_physical_gpus);

        //------------------------------------------------------------------------------
        // ...
        return EXIT_SUCCESS;
    }

    //------------------------------------------------------------------------------
    // DirectX based GPU query
    //------------------------------------------------------------------------------

    int directx()
    {
        std::cout << "[DirectX]" << std::endl;

        //------------------------------------------------------------------------------
        // Grab DirectX factory.
        ComPtr<IDXGIFactory4> factory;

        if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
            std::cerr << "Error: Failed to create DXGI factory!" << std::endl;
            return EXIT_FAILURE;
        }

        //------------------------------------------------------------------------------
        // Enumerate DirectX adapters (GPUs).
        std::cout << std::endl;

        UINT adapter_index = 0;
        IDXGIAdapter* adapter = nullptr;

        while (factory->EnumAdapters(adapter_index, &adapter) != DXGI_ERROR_NOT_FOUND) {
            DXGI_ADAPTER_DESC adapter_desc = {};

            if (FAILED(adapter->GetDesc(&adapter_desc))) {
                std::cerr << "Error: Failed to get adapter description!" << std::endl;
                continue;
            }

            std::cout << "Adapter " << adapter_index << ": ";
            std::wcout << adapter_desc.Description;
            std::cout << ", 0x" << std::hex << adapter_desc.AdapterLuid.HighPart << std::setfill('0') << std::setw(8) << adapter_desc.AdapterLuid.LowPart << std::dec;
            std::cout << std::endl;

            UINT output_index = 0;
            IDXGIOutput* output = nullptr;

            //------------------------------------------------------------------------------
            // Enumerate outputs (displays).
            while (adapter->EnumOutputs(output_index, &output) != DXGI_ERROR_NOT_FOUND) {
                DXGI_OUTPUT_DESC output_desc = {};

                if (FAILED(output->GetDesc(&output_desc))) {
                    std::cerr << "Error: Failed to get output description!" << std::endl;
                    continue;
                }

                std::cout << "  Output " << output_index << ": ";
                std::wcout << output_desc.DeviceName;

                bool first = true;

                if (output_desc.AttachedToDesktop) {
                    if (first) { std::cout << " ("; }
                    else { std::cout << ", "; }
                    std::cout << "display attached";
                    first = false;
                }

                MONITORINFO monitor_info = {};
                monitor_info.cbSize = sizeof(monitor_info);

                if (GetMonitorInfo(output_desc.Monitor, &monitor_info)) {
                    if (monitor_info.dwFlags & MONITORINFOF_PRIMARY) {
                        if (first) { std::cout << " ("; }
                        else { std::cout << ", "; }
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

        return EXIT_SUCCESS;
    }

    //------------------------------------------------------------------------------
    // OpenGL based GPU query
    //------------------------------------------------------------------------------

    int opengl()
    {
        std::cout << "[OpenGL]" << std::endl;

        //------------------------------------------------------------------------------
        // Check prerequisites.
        if (virtual_screen_monitors.empty()) {
            std::cerr << "Error: No monitors are listed for the virtual screen!" << std::endl;
            return EXIT_FAILURE;
        }

        //------------------------------------------------------------------------------
        // Initialize CUDA if available;
        if (cuInit(0) == CUDA_SUCCESS) {
            std::cout << std::endl << "CUDA available" << std::endl;
        }

        //------------------------------------------------------------------------------
        // Register a window class.
        WNDCLASSA wc = {};
        {
            wc.style = CS_OWNDC;
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

        //------------------------------------------------------------------------------
        // Create one 'full screen' window per each monitor in the virtual screen.
        //------------------------------------------------------------------------------

        //------------------------------------------------------------------------------
        // "An OpenGL window should be created with the WS_CLIPCHILDREN and
        // WS_CLIPSIBLINGS styles. Additionally, the window class attribute should NOT
        // include the CS_PARENTDC style." [SetPixelFormat documentation]
        const DWORD style = (WS_OVERLAPPED | WS_CLIPCHILDREN | WS_CLIPSIBLINGS);

        PIXELFORMATDESCRIPTOR pixel_format_desc = {};
        {
            pixel_format_desc.nSize = sizeof(pixel_format_desc);
            pixel_format_desc.nVersion = 1;

            //------------------------------------------------------------------------------
            // "PFD_DEPTH_DONTCARE: To select a pixel format without a depth buffer, you
            // must specify this flag. The requested pixel format can be with or without a
            // depth buffer. Otherwise, only pixel formats with a depth buffer are
            // considered." [PIXELFORMATDESCRIPTOR documentation]
            pixel_format_desc.dwFlags = (PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER | PFD_DEPTH_DONTCARE);

            //------------------------------------------------------------------------------
            // "For RGBA pixel types, it is the size of the color buffer, excluding the
            // alpha bitplanes." [PIXELFORMATDESCRIPTOR documentation]
            pixel_format_desc.iPixelType = PFD_TYPE_RGBA;
            pixel_format_desc.cColorBits = 24;
        };

        std::vector<HWND> windows;
        std::vector<HDC> display_contexts;
        std::vector<HGLRC> gl_contexts;

        for (size_t virtual_screen_monitor_index = 0; virtual_screen_monitor_index < virtual_screen_monitors.size(); ++virtual_screen_monitor_index) {
            //------------------------------------------------------------------------------
            // Create a 'full screen' window.
            RECT window_rect = {};
            {
                SetRect(&window_rect,
                    virtual_screen_monitors[virtual_screen_monitor_index].m_x,
                    virtual_screen_monitors[virtual_screen_monitor_index].m_y,
                    (virtual_screen_monitors[virtual_screen_monitor_index].m_x + virtual_screen_monitors[virtual_screen_monitor_index].m_width),
                    (virtual_screen_monitors[virtual_screen_monitor_index].m_y + virtual_screen_monitors[virtual_screen_monitor_index].m_height));

                AdjustWindowRect(&window_rect, style, FALSE);
            }

            const HWND window = CreateWindowA(wc.lpszClassName, "TestMultiGpuMultiMonitor",
                style, window_rect.left, window_rect.top, (window_rect.right - window_rect.left), (window_rect.bottom - window_rect.top),
                nullptr, nullptr, nullptr, nullptr);

            ShowWindow(window, SW_SHOWDEFAULT);
            UpdateWindow(window);

            //------------------------------------------------------------------------------
            // Setup the display context.
            const HDC display_context = GetDC(window);
            size_t num_monitors = 0;

            if (EnumDisplayMonitors(display_context, nullptr, [](HMONITOR monitor, HDC display_context, LPRECT virtual_screen_rect, LPARAM user_data) {
                size_t* const num_monitors_ptr = (size_t*)user_data;
                (*num_monitors_ptr) += 1;
                return TRUE;
            }, (LPARAM)&num_monitors) == 0)
            {
                std::cerr << "Error: Failed to enumerate monitors for display context!" << std::endl;
                return EXIT_FAILURE;
            }

            if (num_monitors != 1) {
                std::cerr << "Error: Display context intersects more than one monitor!" << std::endl;
                return EXIT_FAILURE;
            }

            const int pixel_format = ChoosePixelFormat(display_context, &pixel_format_desc);

            if (pixel_format == 0) {
                std::cerr << "Error: Failed to choose pixel format!" << std::endl;
                return EXIT_FAILURE;
            }

            if (SetPixelFormat(display_context, pixel_format, &pixel_format_desc) != TRUE) {
                std::cerr << "Error: Failed to set pixel format!" << std::endl;
                return EXIT_FAILURE;
            }

            //------------------------------------------------------------------------------
            // Ccreate OpenGL context and share lists between all the contexts.
            const HGLRC gl_context = wglCreateContext(display_context);

            if (gl_context == NULL) {
                std::cerr << "Error: Failed to create OpenGL context!" << std::endl;
                return EXIT_FAILURE;
            }

            if (gl_contexts.size() > 0) {
                wglShareLists(gl_contexts[0], gl_context);
            }

            //------------------------------------------------------------------------------
            // Add to list.
            windows.push_back(window);
            display_contexts.push_back(display_context);
            gl_contexts.push_back(gl_context);
        }

        assert(windows.size() == virtual_screen_monitors.size());
        assert(display_contexts.size() == virtual_screen_monitors.size());
        assert(gl_contexts.size() == virtual_screen_monitors.size());

        //------------------------------------------------------------------------------
        // Make one of the contexts current so we can initialize OpenGL (via GLEW).
        std::cout << std::endl;

        if (wglMakeCurrent(display_contexts[0], gl_contexts[0]) != TRUE) {
            std::cerr << "Error: Failed to make OpenGL context current: ";
            log_last_error_message();
            return EXIT_FAILURE;
        }

        std::cout << "OpenGL vendor: " << glGetString(GL_VENDOR) << std::endl;
        std::cout << "OpenGL renderer: " << glGetString(GL_RENDERER) << std::endl;
        std::cout << "OpenGL version: " << glGetString(GL_VERSION) << std::endl;

        const GLenum glew_result = glewInit();

        if (glew_result != GLEW_OK) {
            std::cerr << "Error: Failed to initialize GLEW: " << glewGetErrorString(glew_result) << std::endl;
            return EXIT_FAILURE;
        }

#ifndef WGL_NV_gpu_affinity
        wglEnumGpusNV = (wglEnumGpusNV_f)wglGetProcAddress("wglEnumGpusNV");
        wglEnumGpuDevicesNV = (wglEnumGpuDevicesNV_f)wglGetProcAddress("wglEnumGpuDevicesNV");
        wglCreateAffinityDCNV = (wglCreateAffinityDCNV_f)wglGetProcAddress("wglCreateAffinityDCNV");
        wglEnumGpusFromAffinityDCNV = (wglEnumGpusFromAffinityDCNV_f)wglGetProcAddress("wglEnumGpusFromAffinityDCNV");
        wglDeleteDCNV = (wglDeleteDCNV_f)wglGetProcAddress("wglDeleteDCNV");
#endif // WGL_NV_gpu_affinity

        //------------------------------------------------------------------------------
        // Check associated CUDA device.
        unsigned int cuda_device_count = 0;
        std::array<CUdevice, 4> cuda_devices;

        if (cuGLGetDevices(&cuda_device_count, cuda_devices.data(), unsigned int(cuda_devices.size()), CU_GL_DEVICE_LIST_ALL) == CUDA_SUCCESS) {
            for (size_t i = 0; i < cuda_device_count; ++i) {
                std::cout << "CUDA device: " << cuda_devices[i] << std::endl;
            }
        }

        //------------------------------------------------------------------------------
        // Enumerate GPUs.
        std::cout << std::endl;

        UINT gpu_index = 0;
        HGPUNV gpu = nullptr;

        std::vector<HGPUNV> gpus;

        while (wglEnumGpusNV(gpu_index, &gpu)) {
            std::cout << "GPU " << gpu_index << ":" << std::endl;
            gpus.push_back(gpu);

            //------------------------------------------------------------------------------
            // Get associated CUDA device.
            CUdevice cuda_device = -1;
            
            if (cuWGLGetDevice(&cuda_device, gpu) == CUDA_SUCCESS) {
                std::cout << "  CUDA Device: " << cuda_device << std::endl;
            }

            //------------------------------------------------------------------------------
            // Enumerate devices (displays).
            UINT device_index = 0;

            GPU_DEVICE gpu_device;
            gpu_device.cb = sizeof(gpu_device);

            while (wglEnumGpuDevicesNV(gpu, device_index, &gpu_device)) {
                std::cout << "  Device " << device_index << ": ";
                std::cout << gpu_device.DeviceString << ", " << gpu_device.DeviceName << ", ";
                print_display_flags_to_stream(std::cout, gpu_device.Flags);
                std::cout << std::endl;

                ++device_index;
            }

            ++gpu_index;
        }

        if (wglMakeCurrent(nullptr, nullptr) != TRUE) {
            std::cerr << "Error: Failed to release current OpenGL context: ";
            log_last_error_message();
            return EXIT_FAILURE;
        }

        //------------------------------------------------------------------------------
        // Create one (affinity) display and OpenGL context per GPU.
        std::vector<HDC> affinity_display_contexts;
        std::vector<HGLRC> affinity_gl_contexts;

        for (size_t gpu_index = 0; gpu_index < gpus.size(); ++gpu_index) {
            HGPUNV gpu_list[2] = {};
            gpu_list[0] = gpus[gpu_index];

            //------------------------------------------------------------------------------
            // Create and setup affinity display context.
            const HDC display_context = wglCreateAffinityDCNV(gpu_list);

            if (display_context == NULL) {
                std::cerr << "Error: Failed to create affinity display context!" << std::endl;
                return EXIT_FAILURE;
            }

            const int pixel_format = ChoosePixelFormat(display_context, &pixel_format_desc);

            if (pixel_format == 0) {
                std::cerr << "Error: Failed to choose pixel format!" << std::endl;
                return EXIT_FAILURE;
            }

            if (SetPixelFormat(display_context, pixel_format, &pixel_format_desc) != TRUE) {
                std::cerr << "Error: Failed to set pixel format!" << std::endl;
                wglDeleteDCNV(display_context);
                continue;
            }

            //------------------------------------------------------------------------------
            // Ccreate OpenGL context and share lists between all the contexts.
            const HGLRC gl_context = wglCreateContext(display_context);

            if (gl_context == NULL) {
                std::cerr << "Error: Failed to create OpenGL context!" << std::endl;
                wglDeleteDCNV(display_context);
                continue;
            }

            if (affinity_gl_contexts.size() > 0) {
                wglShareLists(affinity_gl_contexts[0], gl_context);
            }

            //------------------------------------------------------------------------------
            // Add to list.
            affinity_display_contexts.push_back(display_context);
            affinity_gl_contexts.push_back(gl_context);
        }

        //------------------------------------------------------------------------------
        // Start rendering threads.
        std::vector<GLuint> affinity_programs(affinity_display_contexts.size());
        std::vector<GLuint> framebuffers(affinity_display_contexts.size());
        std::vector<GLuint> color_attachments(affinity_display_contexts.size());

        if ((0)) {
            start_render_threads(affinity_display_contexts, affinity_gl_contexts,
                [&framebuffers, &color_attachments, &affinity_programs](size_t thread_index)
            {
                if (wglSwapIntervalEXT(1) != TRUE) {
                    std::cerr << "Error: Failed to set swap interval: ";
                    log_last_error_message();
                }

                affinity_programs[thread_index] = RenderPoints::create_program();
                create_texture_backed_render_targets(&framebuffers[thread_index], &color_attachments[thread_index], 1, 4096, 4096);
            },
                [framebuffers, color_attachments, &affinity_programs](size_t thread_index)
            {
                const auto start_time = std::chrono::steady_clock::now();
                GLuint vao = 0;

                //------------------------------------------------------------------------------
                // Render frames.
                for (size_t frame_index = 0; frame_index < (1024 * 16); ++frame_index) {
                    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffers[thread_index]);
                    glClearColor(0.0, 0.0, 0.0, 1.0);
                    glClear(GL_COLOR_BUFFER_BIT);

                    //toolbox::OpenGLProgram::validate(programs[thread_index]);
                    glUseProgram(affinity_programs[thread_index]);

                    RenderPoints::set_rect(rect);
                    RenderPoints::set_mvp(mvp);
                    RenderPoints::draw(vao);

                    glFlush();
                }

                glFinish();

                const auto end_time = std::chrono::steady_clock::now();
                const auto duration = (end_time - start_time);

                std::cout << "Render thread completed in: " << std::chrono::duration_cast<std::chrono::milliseconds>(duration).count() << " ms" << std::endl;

                delete_texture_backed_render_targets(&framebuffers[thread_index], &color_attachments[thread_index], 1);
            });
        }

        std::vector<GLuint> programs(display_contexts.size());
        size_t initial_start_time_offset = (1000000 * 2);
        const auto start_time = std::chrono::steady_clock::now();

        start_render_threads(display_contexts, gl_contexts,
            [&programs](size_t thread_index)
        {
            if (wglSwapIntervalEXT(1) != TRUE) {
                std::cerr << "Error: Failed to set swap interval: ";
                log_last_error_message();
            }

            programs[thread_index] = RenderPoints::create_program();
        },
            [&display_contexts, &programs, &start_time, initial_start_time_offset](size_t thread_index)
        {
            constexpr bool LOG_TIMINGS_TO_CONSOLE = false;
            constexpr bool LOG_TIMINGS_TO_FILE = true;

            size_t start_time_offset = initial_start_time_offset;
            GLuint vao = 0;

            FILE *f = nullptr;

            if (LOG_TIMINGS_TO_FILE) {
                char path[] = "D:\\timings_?.tsv";
                path[11] = ('0' + thread_index);
                f = fopen(path, "w");
            }

            //------------------------------------------------------------------------------
            // Wait till half a frame before the intended start time, let the wait in the
            // loop handle the remainder to the first frame (if enabled).
            std::this_thread::sleep_until(start_time + std::chrono::microseconds(start_time_offset - (1000000 / 120)));
            auto prev_frame_start_time = std::chrono::steady_clock::now();

            for (size_t frame_index = 0; frame_index < (5 * 60  * 60); ++frame_index) {
                const auto frame_start_time = std::chrono::steady_clock::now();

                if (LOG_TIMINGS_TO_CONSOLE || LOG_TIMINGS_TO_FILE) {
                    const auto duration = (frame_start_time - prev_frame_start_time);
                    prev_frame_start_time = frame_start_time;

                    if (LOG_TIMINGS_TO_CONSOLE && (thread_index == 0)) {
                        std::cout << "Frame: " << std::chrono::duration_cast<std::chrono::microseconds>(duration).count() << std::endl;
                    }

                    if (LOG_TIMINGS_TO_FILE && f && (frame_index > 60)) {
                        fprintf(f, "%ji\t", intmax_t(std::chrono::duration_cast<std::chrono::microseconds>(duration).count()));
                    }
                }

                if ((0)) {
                    //------------------------------------------------------------------------------
                    // Start encoding in 1/60 second intervals.
                    std::this_thread::sleep_until(start_time + std::chrono::microseconds(start_time_offset));
                    start_time_offset += (1000000 / 60);
                }
                else if ((0)) {
                    //------------------------------------------------------------------------------
                    // Wait till we are clearly within the frame interval.
                    if (wglDelayBeforeSwapNV(display_contexts[thread_index], GLfloat(1.0 / 80.0)) != TRUE) {
                        //std::cout << GetLastError() << std::endl;
                    }
                }

                const auto encode_start_time = std::chrono::steady_clock::now();

                if (LOG_TIMINGS_TO_CONSOLE || LOG_TIMINGS_TO_FILE) {
                    const auto duration = (encode_start_time - frame_start_time);

                    if (LOG_TIMINGS_TO_CONSOLE && (thread_index == 0)) {
                        std::cout << "Sync: " << std::chrono::duration_cast<std::chrono::microseconds>(duration).count() << std::endl;
                    }

                    if (LOG_TIMINGS_TO_FILE && f && (frame_index > 60)) {
                        fprintf(f, "%ji\t", intmax_t(std::chrono::duration_cast<std::chrono::microseconds>(duration).count()));
                    }
                }

                //------------------------------------------------------------------------------
                // Encode the frame.
                if ((1)) {
                    switch (frame_index % 8) {
                    case 0:
                        glEnable(GL_SCISSOR_TEST);
                        glScissor(0, 256, 2048, 256);
                        glClearColor(1.0f, 0.0f, 0.0f, 1.0f);
                        break;

                    case 1:
                        glEnable(GL_SCISSOR_TEST);
                        glScissor(0, 512, 2048, 256);
                        glClearColor(0.0f, 1.0f, 0.0f, 1.0f);
                        break;

                    case 2:
                        glEnable(GL_SCISSOR_TEST);
                        glScissor(0, 768, 2048, 256);
                        glClearColor(0.0f, 0.0f, 1.0f, 1.0f);
                        break;

                    case 3:
                        glEnable(GL_SCISSOR_TEST);
                        glScissor(0, 64, 2048, 64);
                        glClearColor(0.0f, 0.0f, 1.0f, 1.0f);
                        break;

                    case 4:
                        glEnable(GL_SCISSOR_TEST);
                        glScissor(0, 128, 2048, 64);
                        glClearColor(0.0f, 1.0f, 1.0f, 1.0f);
                        break;

                    case 5:
                        glEnable(GL_SCISSOR_TEST);
                        glScissor(0, 192, 2048, 64);
                        glClearColor(1.0f, 0.0f, 1.0f, 1.0f);
                        break;

                    default:
                        glDisable(GL_SCISSOR_TEST);
                        glClearColor(0.2f, 0.2f, 0.2f, 1.0f);
                        break;
                    }

                    glClear(GL_COLOR_BUFFER_BIT);
                }

                if ((0)) {
                    toolbox::OpenGLProgram::validate(programs[thread_index]);
                }

                if ((0)) {
                    glUseProgram(programs[thread_index]);

                    RenderPoints::set_rect(rect);
                    RenderPoints::set_mvp(mvp);
                    RenderPoints::draw(vao);
                }

                //------------------------------------------------------------------------------
                // Swap buffers.
                const auto swap_buffers_start_time = std::chrono::steady_clock::now();

                if (LOG_TIMINGS_TO_CONSOLE || LOG_TIMINGS_TO_FILE) {
                    const auto duration = (swap_buffers_start_time - encode_start_time);

                    if (LOG_TIMINGS_TO_CONSOLE && (thread_index == 0)) {
                        std::cout << "Encode: " << std::chrono::duration_cast<std::chrono::microseconds>(duration).count() << std::endl;
                    }

                    if (LOG_TIMINGS_TO_FILE && f && (frame_index > 60)) {
                        fprintf(f, "%ji\t", intmax_t(std::chrono::duration_cast<std::chrono::microseconds>(duration).count()));
                    }
                }

                SwapBuffers(display_contexts[thread_index]);

                if (LOG_TIMINGS_TO_CONSOLE || LOG_TIMINGS_TO_FILE) {
                    const auto now = std::chrono::steady_clock::now();
                    const auto duration = (now - swap_buffers_start_time);

                    if (LOG_TIMINGS_TO_CONSOLE) {
                        std::cout << "Swap: " << std::chrono::duration_cast<std::chrono::microseconds>(duration).count() << std::endl;
                    }

                    if (LOG_TIMINGS_TO_FILE && f && (frame_index > 60)) {
                        fprintf(f, "%ji\t", intmax_t(std::chrono::duration_cast<std::chrono::microseconds>(duration).count()));
                        fprintf(f, "%ji\n", intmax_t(std::chrono::duration_cast<std::chrono::microseconds>(now - start_time).count()));
                    }
                }
            }

            if (f) {
                fclose(f);
            }
        });

        //------------------------------------------------------------------------------
        // Main loop driving application window.
        MSG message = {};

        while (try_join_render_threads(10) == false) {
            //------------------------------------------------------------------------------
            // Handle application window messages.
            GetMessage(&message, nullptr, 0, 0);
            TranslateMessage(&message);
            DispatchMessage(&message);
        }

        //------------------------------------------------------------------------------
        // Wait for all render threads to terminate.
        join_render_threads();

        //------------------------------------------------------------------------------
        // Tidy.
        std::for_each(begin(gl_contexts), end(gl_contexts), [](HGLRC gl_context) {
            wglDeleteContext(gl_context);
        });

        std::for_each(begin(display_contexts), end(display_contexts), [](HDC display_context) {
            wglDeleteDCNV(display_context);
        });

        std::for_each(begin(gl_contexts), end(gl_contexts), [](HGLRC gl_context) {
            wglDeleteContext(gl_context);
        });

        for (size_t i = 0; i < virtual_screen_monitors.size(); ++i) {
            if ((wc.style & CS_OWNDC) != CS_OWNDC) {
                ReleaseDC(windows[i], display_contexts[i]);
            }

            DestroyWindow(windows[i]);
        }

        //------------------------------------------------------------------------------
        // ...
        return EXIT_SUCCESS;
    }

} // unnamed namespace

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int
main(int argc, char* argv[])
{
    windows();
    std::cout << std::endl;
    nvapi();
    std::cout << std::endl;
    directx();
    std::cout << std::endl;
    opengl();

    return EXIT_SUCCESS;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
