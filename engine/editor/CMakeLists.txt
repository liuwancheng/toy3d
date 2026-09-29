set(TARGET_NAME Toy3dEditor)

file(GLOB EDITOR_HEADERS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/source/*.h)
file(GLOB EDITOR_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/source/*.cpp)
list(APPEND EDITOR_HEADERS
    "${CMAKE_CURRENT_SOURCE_DIR}/source/placement/placement_catalog.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/source/placement/actor_placement.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/source/placement/actor_factory.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/source/panels/place_actors_panel.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/source/commands/editor_command_history.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/source/panels/scene_panels.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/source/selection/editor_selection.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/source/viewport/scene_viewport.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/source/viewport/actor_icons.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/source/workspace/editor_workspace.h")
list(APPEND EDITOR_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/source/placement/placement_catalog.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/source/placement/actor_placement.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/source/placement/actor_factory.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/source/panels/place_actors_panel.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/source/commands/editor_command_history.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/source/panels/scene_panels.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/source/selection/editor_selection.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/source/viewport/scene_viewport.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/source/viewport/actor_icons.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/source/workspace/editor_workspace.cpp")
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${EDITOR_HEADERS} ${EDITOR_SOURCES} )

set(TOY3D_ENGINE_ASSET_DIR "${ENGINE_ROOT_DIR}/asset")
set(TOY3D_PROJECT_ASSET_DIR "${TOY3D_ROOT_DIR}/project/asset")
set(TOY3D_EDITOR_RESOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/resources")
set(TOY3D_PLATFORM_RESOURCE_DIR "${ENGINE_ROOT_DIR}/build/windows/resources")

if(WIN32)
    set(EDITOR_ICON_FILE "${TOY3D_PLATFORM_RESOURCE_DIR}/Toy3dEditor.rc")
    add_executable(${TARGET_NAME} ${EDITOR_HEADERS} ${EDITOR_SOURCES} ${EDITOR_ICON_FILE})

    set(RESOURCE_H "${TOY3D_PLATFORM_RESOURCE_DIR}/resource.h")
    target_include_directories(${TARGET_NAME} PRIVATE "${TOY3D_PLATFORM_RESOURCE_DIR}")
    set_source_files_properties("${EDITOR_ICON_FILE}" PROPERTIES OBJECT_DEPENDS
        "${RESOURCE_H};${TOY3D_PLATFORM_RESOURCE_DIR}/Toy3dEditor.ico")

    source_group("resource" FILES ${EDITOR_ICON_FILE} ${RESOURCE_H})
elseif(APPLE)
    # 使用 macOS 的 .icns 图标文件和 Info.plist
    set(EDITOR_ICON_FILE "${ENGINE_ROOT_DIR}/build/mac/resources/Toy3d.icns")
    set(EDITOR_PLIST_TEMPLATE "${ENGINE_ROOT_DIR}/build/mac/resources/Info.plist.in")
    
    # 创建 macOS 应用程序包，并指定 Info.plist 模板
    add_executable(${TARGET_NAME} MACOSX_BUNDLE ${EDITOR_HEADERS} ${EDITOR_SOURCES})
    set_target_properties(${TARGET_NAME} PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST ${EDITOR_PLIST_TEMPLATE}
        MACOSX_BUNDLE_BUNDLE_NAME "Toy3d Editor"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.toy3d.Editor"
        MACOSX_BUNDLE_ICON_FILE "Toy3d.icns"
        MACOSX_BUNDLE_BUNDLE_VERSION "${CMAKE_PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${CMAKE_PROJECT_VERSION}"
        MACOSX_BUNDLE_COPYRIGHT "Toy3d contributors"
        XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED "NO"
        XCODE_ATTRIBUTE_CODE_SIGNING_REQUIRED "NO"
    )
    
    # 将图标文件添加到应用程序资源中
    set_source_files_properties(${EDITOR_ICON_FILE} PROPERTIES
        MACOSX_PACKAGE_LOCATION "Resources"
    )
    
    # 添加资源文件到目标
    target_sources(${TARGET_NAME} PRIVATE ${EDITOR_ICON_FILE})
else()
    add_executable(${TARGET_NAME} ${EDITOR_HEADERS} ${EDITOR_SOURCES})
endif()

add_compile_definitions("TOY3D_ROOT_DIR=${BINARY_ROOT_DIR}")

target_include_directories(
  ${TARGET_NAME} 
  PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/source" $<BUILD_INTERFACE:${THIRD_PARTY_DIR}/stb>
)

target_compile_features(${TARGET_NAME} PRIVATE cxx_std_17)
set_target_properties(${TARGET_NAME} PROPERTIES CXX_EXTENSIONS OFF OUTPUT_NAME "Toy3dEditor")
set_target_properties(${TARGET_NAME} PROPERTIES FOLDER "Engine")

target_compile_options(${TARGET_NAME} PUBLIC "$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/WX->")
target_compile_options(${TARGET_NAME} PRIVATE "$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/utf-8>")

target_compile_definitions(${TARGET_NAME}
  PRIVATE
    "TOY3D_SHADER_MAP_ENTRY_ROOT=\"${BINARY_ROOT_DIR}shader/phong\""
    "TOY3D_EDITOR_ASSET_ROOT=\"${TOY3D_PROJECT_ASSET_DIR}\""
    "TOY3D_EDITOR_ENGINE_ASSET_ROOT=\"${TOY3D_ENGINE_ASSET_DIR}\""
    "TOY3D_EDITOR_RESOURCE_ROOT=\"${TOY3D_EDITOR_RESOURCE_DIR}\""
    "TOY3D_EDITOR_DEPLOY_ROOT=\"${BINARY_ROOT_DIR}\""
    "TOY3D_EDITOR_LAYOUT_PATH=\"${BINARY_ROOT_DIR}saved/editor_layout.ini\"")

target_sources(${TARGET_NAME} PRIVATE
    "source/thumbnails/thumbnail_preview_scene.h" "source/thumbnails/thumbnail_preview_scene.cpp"
    "source/thumbnails/asset_thumbnail_pool.h" "source/thumbnails/asset_thumbnail_pool.cpp"
    "source/panels/content_browser_panel.h" "source/panels/content_browser_panel.cpp"
    "source/panels/static_mesh_import_dialog.h" "source/panels/static_mesh_import_dialog.cpp"
    "source/panels/material_create_dialog.h" "source/panels/material_create_dialog.cpp"
    "source/asset_tools/material_asset_tools.h" "source/asset_tools/material_asset_tools.cpp"
    "source/placement/asset_placement.h" "source/placement/asset_placement.cpp")

if(WIN32)
    set(TOY3D_MODEL_PICKER_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/source/platform/win/model_file_picker.cpp")
    set(TOY3D_MODEL_PICKER_LIBRARIES comdlg32)
elseif(APPLE)
    set(TOY3D_MODEL_PICKER_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/source/platform/mac/model_file_picker.mm")
    find_library(TOY3D_EDITOR_COCOA_FRAMEWORK Cocoa REQUIRED)
    set(TOY3D_MODEL_PICKER_LIBRARIES "${TOY3D_EDITOR_COCOA_FRAMEWORK}")
    set_target_properties(${TARGET_NAME} PROPERTIES OBJCXX_STANDARD 17 OBJCXX_STANDARD_REQUIRED ON OBJCXX_EXTENSIONS OFF)
else()
    set(TOY3D_MODEL_PICKER_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/source/platform/model_file_picker_unsupported.cpp")
    set(TOY3D_MODEL_PICKER_LIBRARIES "")
endif()
target_sources(${TARGET_NAME} PRIVATE "source/platform/model_file_picker.h" "${TOY3D_MODEL_PICKER_SOURCE}")
target_link_libraries(${TARGET_NAME} PRIVATE ${TOY3D_MODEL_PICKER_LIBRARIES})
target_link_libraries(${TARGET_NAME} PRIVATE Toy3dRuntime Toy3dResource Toy3dAssetThumbnail Toy3dImageCodec imguizmo)
if(TOY3D_ENABLE_ASSIMP_MODEL_IMPORT)
    target_sources(${TARGET_NAME} PRIVATE
        "source/asset_tools/static_mesh_asset_tools.h" "source/asset_tools/static_mesh_asset_tools.cpp")
    target_link_libraries(${TARGET_NAME} PRIVATE Toy3dAssetImport)
    target_compile_definitions(${TARGET_NAME} PRIVATE WITH_MODEL_IMPORT=1)
else()
    target_compile_definitions(${TARGET_NAME} PRIVATE WITH_MODEL_IMPORT=0)
endif()
target_compile_definitions(${TARGET_NAME} PRIVATE WITH_EDITOR=1 WITH_EDITORONLY_DATA=1)

if(BUILD_TESTING)
    if(TOY3D_ENABLE_VULKAN_RHI AND (WIN32 OR APPLE))
        add_executable(Toy3dEditorThumbnailTests
            "tests/thumbnail_integration_tests.cpp"
            "source/workspace/editor_workspace.cpp"
            "source/thumbnails/asset_thumbnail_pool.cpp" "source/thumbnails/thumbnail_preview_scene.cpp"
            "source/panels/content_browser_panel.cpp" "source/selection/editor_selection.cpp"
            "source/panels/static_mesh_import_dialog.cpp" "${TOY3D_MODEL_PICKER_SOURCE}"
            "source/placement/asset_placement.cpp" "source/placement/actor_placement.cpp"
            "source/commands/editor_command_history.cpp"
            "source/placement/actor_factory.cpp" "source/placement/placement_catalog.cpp")
        target_compile_features(Toy3dEditorThumbnailTests PRIVATE cxx_std_17)
        set_target_properties(Toy3dEditorThumbnailTests PROPERTIES CXX_EXTENSIONS OFF FOLDER "Tests/Editor")
        target_include_directories(Toy3dEditorThumbnailTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/source")
        target_link_libraries(Toy3dEditorThumbnailTests PRIVATE Toy3dRuntime Toy3dResource Toy3dAssetThumbnail Toy3dImageCodec imgui)
        target_link_libraries(Toy3dEditorThumbnailTests PRIVATE ${TOY3D_MODEL_PICKER_LIBRARIES})
        if(TOY3D_ENABLE_ASSIMP_MODEL_IMPORT)
            target_sources(Toy3dEditorThumbnailTests PRIVATE "source/asset_tools/static_mesh_asset_tools.cpp")
            target_link_libraries(Toy3dEditorThumbnailTests PRIVATE Toy3dAssetImport)
            target_compile_definitions(Toy3dEditorThumbnailTests PRIVATE WITH_MODEL_IMPORT=1)
        else()
            target_compile_definitions(Toy3dEditorThumbnailTests PRIVATE WITH_MODEL_IMPORT=0)
        endif()
        if(APPLE)
            set_target_properties(Toy3dEditorThumbnailTests PROPERTIES OBJCXX_STANDARD 17 OBJCXX_STANDARD_REQUIRED ON OBJCXX_EXTENSIONS OFF)
        endif()
        if(WIN32)
            target_sources(Toy3dEditorThumbnailTests PRIVATE "${EDITOR_ICON_FILE}")
        endif()
        target_compile_definitions(Toy3dEditorThumbnailTests PRIVATE WITH_EDITOR=1 WITH_EDITORONLY_DATA=1
            "TOY3D_SHADER_MAP_ENTRY_ROOT=\"${BINARY_ROOT_DIR}shader/phong\""
            "TOY3D_THUMBNAIL_TEST_ROOT=\"${CMAKE_BINARY_DIR}/thumbnail-test-output\""
            "TOY3D_EDITOR_ENGINE_ASSET_ROOT=\"${TOY3D_ENGINE_ASSET_DIR}\""
            "TOY3D_EDITOR_RESOURCE_ROOT=\"${TOY3D_EDITOR_RESOURCE_DIR}\""
            "TOY3D_EDITOR_DEPLOY_ROOT=\"${BINARY_ROOT_DIR}\"")
        target_compile_options(Toy3dEditorThumbnailTests PRIVATE "$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/utf-8>")
        add_test(NAME Toy3dEditor.Thumbnail COMMAND Toy3dEditorThumbnailTests)
        set_tests_properties(Toy3dEditor.Thumbnail PROPERTIES TIMEOUT 45 RESOURCE_LOCK Toy3dGpuWindow)
    endif()
    add_executable(Toy3dEditorWorkspaceTests
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/workspace_tests.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/source/workspace/editor_workspace.cpp")
    target_compile_features(Toy3dEditorWorkspaceTests PRIVATE cxx_std_17)
    set_target_properties(Toy3dEditorWorkspaceTests PROPERTIES CXX_EXTENSIONS OFF FOLDER "Tests/Editor")
    target_include_directories(Toy3dEditorWorkspaceTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/source")
    target_sources(Toy3dEditorWorkspaceTests PRIVATE "source/asset_tools/material_asset_tools.cpp")
    target_link_libraries(Toy3dEditorWorkspaceTests PRIVATE Toy3dResource Toy3dMaterialAsset Toy3dStaticMeshAsset)
    target_compile_options(Toy3dEditorWorkspaceTests PRIVATE "$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/utf-8>")
    add_test(NAME Toy3dEditor.Workspace COMMAND Toy3dEditorWorkspaceTests)

    add_executable(Toy3dEditorPlacementTests
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/placement_tests.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/source/placement/placement_catalog.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/source/placement/actor_placement.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/source/placement/actor_factory.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/source/viewport/actor_icons.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/source/viewport/scene_viewport.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/source/editor_viewport_gizmo.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/source/selection/editor_selection.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/source/commands/editor_command_history.cpp")
    target_compile_features(Toy3dEditorPlacementTests PRIVATE cxx_std_17)
    set_target_properties(Toy3dEditorPlacementTests PROPERTIES CXX_EXTENSIONS OFF FOLDER "Tests/Editor")
    target_include_directories(Toy3dEditorPlacementTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/source")
    target_link_libraries(Toy3dEditorPlacementTests PRIVATE Toy3dRuntime Toy3dResource imguizmo)
    target_compile_definitions(Toy3dEditorPlacementTests PRIVATE
        WITH_EDITOR=1 WITH_EDITORONLY_DATA=1
        "TOY3D_SHADER_MAP_ENTRY_ROOT=\"${BINARY_ROOT_DIR}shader/phong\"")
    target_compile_options(Toy3dEditorPlacementTests PRIVATE "$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/utf-8>")
    add_test(NAME Toy3dEditor.Placement COMMAND Toy3dEditorPlacementTests)
endif()

if(WIN32)
    set_target_properties(${TARGET_NAME} PROPERTIES
        WIN32_EXECUTABLE TRUE    # 设置为 Windows GUI 应用程序
    )
endif()


set(POST_BUILD_COMMANDS
  COMMAND ${CMAKE_COMMAND} -E make_directory "${BINARY_ROOT_DIR}"
  COMMAND ${CMAKE_COMMAND} -E make_directory "${BINARY_ROOT_DIR}saved"
)

if(APPLE)
  list(APPEND POST_BUILD_COMMANDS
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "$<TARGET_BUNDLE_DIR:${TARGET_NAME}>"
            "${BINARY_ROOT_DIR}${TARGET_NAME}.app"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:${TARGET_NAME}>"
            "${BINARY_ROOT_DIR}${TARGET_NAME}"
  )
else()
  list(APPEND POST_BUILD_COMMANDS
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "$<TARGET_FILE_DIR:${TARGET_NAME}>/"
            "${BINARY_ROOT_DIR}"
  )
endif()

list(APPEND POST_BUILD_COMMANDS
  COMMAND "${CMAKE_COMMAND}" -E copy_directory
          "${TOY3D_EDITOR_RESOURCE_DIR}" "${BINARY_ROOT_DIR}editor/resources")


add_custom_command(TARGET ${TARGET_NAME} POST_BUILD ${POST_BUILD_COMMANDS} VERBATIM)
toy3d_deploy_common_resources(${TARGET_NAME})

#precompile
#set global vari used by precompile
set(TOY3D_EDITOR_HEADS "${EDITOR_HEADERS}" PARENT_SCOPE)
