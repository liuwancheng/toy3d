set(TOY3D_IMGUI_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/imgui")

# Toy3d consumes Dear ImGui only as its CPU-side UI/layout library. Platform
# event routing and all GPU rendering are implemented by Toy3d runtime code.
add_library(imgui STATIC
    "${TOY3D_IMGUI_SOURCE_DIR}/imgui.cpp"
    "${TOY3D_IMGUI_SOURCE_DIR}/imgui_draw.cpp"
    "${TOY3D_IMGUI_SOURCE_DIR}/imgui_tables.cpp"
    "${TOY3D_IMGUI_SOURCE_DIR}/imgui_widgets.cpp")
target_compile_features(imgui PUBLIC cxx_std_17)
set_target_properties(imgui PROPERTIES CXX_EXTENSIONS OFF)
target_include_directories(imgui
    PUBLIC
        "$<BUILD_INTERFACE:${TOY3D_IMGUI_SOURCE_DIR}>")
