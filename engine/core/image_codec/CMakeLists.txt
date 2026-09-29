add_library(Toy3dImageCodec STATIC)
target_sources(Toy3dImageCodec PRIVATE "png_codec.cpp" "png_codec.h")
target_compile_features(Toy3dImageCodec PUBLIC cxx_std_17)
set_target_properties(Toy3dImageCodec PROPERTIES CXX_EXTENSIONS OFF FOLDER "Engine/Core")
target_include_directories(Toy3dImageCodec PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>"
    PRIVATE "${THIRD_PARTY_DIR}/stb")
