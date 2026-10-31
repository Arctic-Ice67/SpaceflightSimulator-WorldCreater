cmake_minimum_required(VERSION 3.20)
project(WorldCreater CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(WorldCreater main.cpp net.cpp glass.cpp
    third_party/imgui/imgui.cpp
    third_party/imgui/imgui_draw.cpp
    third_party/imgui/imgui_tables.cpp
    third_party/imgui/imgui_widgets.cpp
    third_party/imgui/backends/imgui_impl_win32.cpp
    third_party/imgui/backends/imgui_impl_dx11.cpp)

target_include_directories(WorldCreater PRIVATE
    third_party/imgui
    third_party/imgui/backends
    third_party)

target_link_libraries(WorldCreater PRIVATE
    d3d11 dxgi d3dcompiler imm32 ole32 shell32 comdlg32 dwmapi uuid ws2_32 windowscodecs)

target_compile_options(WorldCreater PRIVATE
    -mwindows
    -static-libgcc -static-libstdc++)
