find_package(Threads REQUIRED)

add_library(Toy3dResource STATIC)
target_sources(Toy3dResource PRIVATE "asset_identity.cpp" "asset_identity.h" "asset_value_codec.cpp" "asset_file.cpp" "asset_file.h" "asset_meta.cpp" "asset_meta.h" "asset_yaml.cpp" "asset_yaml.h" "asset_pair.cpp" "asset_pair.h" "asset_pair_store.cpp" "asset_pair_store.h" "asset_descriptor_path.cpp" "asset_descriptor_path.h" "asset_format_migration.cpp" "asset_index.cpp" "asset_index.h" "asset_catalog.cpp" "asset_catalog.h" "property_path.cpp" "property_path.h" "edit_session.h")
target_compile_features(Toy3dResource PUBLIC cxx_std_17)
set_target_properties(Toy3dResource PROPERTIES CXX_EXTENSIONS OFF FOLDER "Engine/Core")
target_include_directories(Toy3dResource
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>")
target_link_libraries(Toy3dResource
    PUBLIC
        Toy3dReflection
        Toy3dSerialization
        Toy3dFileSystem
        Toy3dHash)
target_link_libraries(Toy3dResource PUBLIC Threads::Threads)
target_link_libraries(Toy3dResource PRIVATE yaml-cpp)

if(BUILD_TESTING)
    add_executable(Toy3dAssetFileTests "tests/asset_file_tests.cpp")
    target_compile_features(Toy3dAssetFileTests PRIVATE cxx_std_17)
    set_target_properties(Toy3dAssetFileTests PROPERTIES CXX_EXTENSIONS OFF FOLDER "Engine/Tests")
    target_link_libraries(Toy3dAssetFileTests PRIVATE Toy3dResource)
    add_test(NAME Toy3dResource.AssetFile COMMAND Toy3dAssetFileTests)
endif()
