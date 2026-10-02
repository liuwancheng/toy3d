set(TARGET_NAME Toy3dEditor)

file(GLOB EDITOR_HEADERS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/source/*.h)
file(GLOB EDITOR_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/source/*.cpp)
list(APPEND EDITOR_HEADERS
    "${CMAKE_CURRENT_SOURCE_DIR}/source/scene/placement/placement_catalog.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/source/scene/placement/actor_placement.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/source/scene/placement/actor_factory.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/source/panels/place_actors_panel.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/source/scene/editor_command_history.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/source/panels/scene_panels.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/source/scene/editor_selection.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/source/viewport/scene_viewport.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/source/viewport/actor_icons.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/source/workspace/editor_workspace.h")
list(APPEND EDITOR_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/source/panels/place_actors_panel.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/source/workspace/editor_workspace.cpp")
# Scene authoring is shared by the application and its integration tests.
# Asset sessions/workspace remain independently usable without the runtime UI.
add_library(Toy3dEditorCore STATIC)
target_sources(Toy3dEditorCore PRIVATE
    "source/scene/editor_play_session.h" "source/scene/editor_play_session.cpp"
    "source/workspace/editor_project.cpp"
    "source/scene/placement/placement_catalog.cpp"
    "source/scene/placement/actor_placement.cpp"
    "source/scene/placement/actor_factory.cpp"
    "source/scene/editor_command_history.cpp"
    "source/panels/scene_panels.cpp"
    "source/scene/editor_selection.cpp"
    "source/viewport/scene_viewport.cpp"
    "source/viewport/actor_icons.cpp"
    "source/viewport/editor_viewport_gizmo.cpp"
    "source/scene/material_assignments.cpp"
    "source/scene/placement/asset_placement.cpp"
    "source/scene/editor_actor_state.cpp"
    "source/scene/actor_details.cpp"
    "source/scene/editor_scene_session.cpp"
    "source/scene/components/component_editor_registry.cpp"
    "source/scene/components/component_details.cpp"
    "source/scene/components/light_component_editor.cpp"
    "source/scene/components/camera_component_editor.cpp"
    "source/scene/components/mesh_component_editor.cpp"
    "source/panels/editor_panel_registry.cpp"
    "source/panels/console_panel.h"
    "source/panels/console_panel.cpp"
    "source/panels/editor_notifications.h" "source/panels/editor_notifications.cpp"
    "source/assets/asset_editor_registry.cpp"
)
target_compile_features(Toy3dEditorCore PUBLIC cxx_std_17)
set_target_properties(Toy3dEditorCore PROPERTIES CXX_EXTENSIONS OFF FOLDER "Engine/Editor")
target_include_directories(Toy3dEditorCore PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/source")
target_link_libraries(Toy3dEditorCore PUBLIC Toy3dRuntime Toy3dAssets imguizmo)
target_link_libraries(Toy3dEditorCore PRIVATE Toy3dShaderCompilerCore)
target_compile_definitions(Toy3dEditorCore PRIVATE WITH_EDITOR=1 WITH_EDITORONLY_DATA=1
    "TOY3D_SHADER_MAP_ENTRY_ROOT=\"${BINARY_ROOT_DIR}shader/phong\"")
target_compile_definitions(Toy3dEditorCore PUBLIC "TOY3D_BUILTIN_SHADER_ROOT=\"${BINARY_ROOT_DIR}shader\"")
target_compile_options(Toy3dEditorCore PRIVATE "$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/utf-8>")
list(REMOVE_ITEM EDITOR_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/source/main.cpp")
target_sources(Toy3dEditorCore PRIVATE ${EDITOR_SOURCES})

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${EDITOR_HEADERS} ${EDITOR_SOURCES})

set(TOY3D_ENGINE_ASSET_DIR "${ENGINE_ROOT_DIR}/asset")
set(TOY3D_PROJECT_ASSET_DIR "${TOY3D_ROOT_DIR}/project/asset")
set(TOY3D_EDITOR_RESOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/resources")
set(TOY3D_PLATFORM_RESOURCE_DIR "${ENGINE_ROOT_DIR}/build/windows/resources")

if(WIN32)
    set(EDITOR_ICON_FILE "${TOY3D_PLATFORM_RESOURCE_DIR}/Toy3dEditor.rc")
    add_executable(${TARGET_NAME} "source/main.cpp" "${ENGINE_ROOT_DIR}/runtime/application/host_entry.cpp" ${EDITOR_ICON_FILE})

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
    add_executable(${TARGET_NAME} MACOSX_BUNDLE "source/main.cpp" "${ENGINE_ROOT_DIR}/runtime/application/host_entry.cpp")
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
    add_executable(${TARGET_NAME} "source/main.cpp" "${ENGINE_ROOT_DIR}/runtime/application/host_entry.cpp")
endif()

target_include_directories(
  Toy3dEditorCore
  PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/source" $<BUILD_INTERFACE:${THIRD_PARTY_DIR}/stb>
)

target_compile_features(${TARGET_NAME} PRIVATE cxx_std_17)
set_target_properties(${TARGET_NAME} PROPERTIES CXX_EXTENSIONS OFF OUTPUT_NAME "Toy3dEditor")
set_target_properties(${TARGET_NAME} PROPERTIES FOLDER "Engine")

target_compile_options(${TARGET_NAME} PUBLIC "$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/WX->")
target_compile_options(${TARGET_NAME} PRIVATE "$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/utf-8>")

target_compile_definitions(Toy3dEditorCore
  PRIVATE
    "TOY3D_SHADER_MAP_ENTRY_ROOT=\"${BINARY_ROOT_DIR}shader/phong\""
    "TOY3D_EDITOR_ENGINE_ASSET_ROOT=\"${TOY3D_ENGINE_ASSET_DIR}\""
    "TOY3D_EDITOR_RESOURCE_ROOT=\"${TOY3D_EDITOR_RESOURCE_DIR}\""
    "TOY3D_EDITOR_DEPLOY_ROOT=\"${BINARY_ROOT_DIR}\""
    "TOY3D_EDITOR_ENGINE_CONFIG_ROOT=\"${ENGINE_ROOT_DIR}/config\""
    "TOY3D_EDITOR_EXECUTABLE=\"${BINARY_ROOT_DIR}$<TARGET_FILE_NAME:Toy3dEditor>\"")

target_compile_definitions(Toy3dEditorCore PRIVATE
    "TOY3D_EDITOR_ENGINE_SHADER_ROOT=\"${ENGINE_ROOT_DIR}/shader/builtin\""
    "TOY3D_EDITOR_ENGINE_INCLUDE_ROOT=\"${ENGINE_ROOT_DIR}/shader/include\""
    "TOY3D_EDITOR_SHADER_COMPILER=\"${BINARY_ROOT_DIR}$<TARGET_FILE_NAME:Toy3dShaderCompiler>\""
    "TOY3D_EDITOR_SHADER_TOOLCHAIN=\"${TOY3D_SHADER_TOOLCHAIN_ROOT}\"")

target_sources(Toy3dEditorCore PRIVATE
    "source/shader/shader_workflow.h" "source/shader/shader_workflow.cpp"
    "source/shader/shader_workflow_queue.cpp"
    "source/shader/shader_source_creation.cpp"
    "source/shader/shader_create_dialog.h" "source/shader/shader_create_dialog.cpp")
target_link_libraries(Toy3dEditorCore PRIVATE Toy3dRuntime)

target_sources(Toy3dEditorCore PRIVATE
    "source/scene/material_assignments.h"
    "source/assets/material/material_edit_session.h" "source/assets/material/material_edit_session.cpp"
    "source/assets/material/material_editor_panel.h" "source/assets/material/material_editor_panel.cpp"
    "source/assets/thumbnails/thumbnail_preview_scene.h" "source/assets/thumbnails/thumbnail_preview_scene.cpp"
    "source/assets/thumbnails/asset_thumbnail_pool.h" "source/assets/thumbnails/asset_thumbnail_pool.cpp"
    "source/panels/content_browser_panel.h" "source/panels/content_browser_panel.cpp"
    "source/assets/mesh/static_mesh_import_dialog.h" "source/assets/mesh/static_mesh_import_dialog.cpp"
    "source/assets/texture/texture_import_dialog.h" "source/assets/texture/texture_import_dialog.cpp"
    "source/assets/texture/texture_preview_panel.h" "source/assets/texture/texture_preview_panel.cpp"
    "source/assets/texture/texture_preview_image.h" "source/assets/texture/texture_preview_image.cpp"
    "source/assets/texture/texture_asset_tools.h" "source/assets/texture/texture_asset_tools.cpp"
    "source/assets/material/material_create_dialog.h" "source/assets/material/material_create_dialog.cpp"
    "source/assets/material/material_asset_tools.h" "source/assets/material/material_asset_tools.cpp"
    "source/scene/placement/asset_placement.h")

if(WIN32)
    set(TOY3D_MODEL_PICKER_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/source/platform/win/model_file_picker.cpp")
    set(TOY3D_MODEL_PICKER_LIBRARIES comdlg32 shell32 ole32)
elseif(APPLE)
    set(TOY3D_MODEL_PICKER_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/source/platform/mac/model_file_picker.mm")
    find_library(TOY3D_EDITOR_COCOA_FRAMEWORK Cocoa REQUIRED)
    set(TOY3D_MODEL_PICKER_LIBRARIES "${TOY3D_EDITOR_COCOA_FRAMEWORK}")
    set_target_properties(Toy3dEditorCore PROPERTIES OBJCXX_STANDARD 17 OBJCXX_STANDARD_REQUIRED ON OBJCXX_EXTENSIONS OFF)
else()
    set(TOY3D_MODEL_PICKER_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/source/platform/model_file_picker_unsupported.cpp")
    set(TOY3D_MODEL_PICKER_LIBRARIES "")
endif()
target_sources(Toy3dEditorCore PRIVATE "source/platform/model_file_picker.h" "${TOY3D_MODEL_PICKER_SOURCE}")
target_link_libraries(Toy3dEditorCore PRIVATE ${TOY3D_MODEL_PICKER_LIBRARIES})
target_link_libraries(Toy3dEditorCore PRIVATE Toy3dRuntime Toy3dAssets Toy3dCore Toy3dAssetPipeline imguizmo)
if(TOY3D_ENABLE_ASSIMP_MODEL_IMPORT)
    target_sources(Toy3dEditorCore PRIVATE
        "source/assets/mesh/static_mesh_asset_tools.h" "source/assets/mesh/static_mesh_asset_tools.cpp")
    target_link_libraries(Toy3dEditorCore PRIVATE Toy3dAssetPipeline)
    target_compile_definitions(Toy3dEditorCore PRIVATE WITH_MODEL_IMPORT=1)
else()
    target_compile_definitions(Toy3dEditorCore PRIVATE WITH_MODEL_IMPORT=0)
endif()
target_compile_definitions(Toy3dEditorCore PRIVATE WITH_EDITOR=1 WITH_EDITORONLY_DATA=1)

target_link_libraries(${TARGET_NAME} PRIVATE Toy3dEditorCore)

if(BUILD_TESTING)
    add_executable(Toy3dEditorProjectTests "tests/project_tests.cpp" "source/workspace/editor_workspace.cpp")
    target_compile_features(Toy3dEditorProjectTests PRIVATE cxx_std_17)
    set_target_properties(Toy3dEditorProjectTests PROPERTIES CXX_EXTENSIONS OFF FOLDER "Tests/Editor")
    target_link_libraries(Toy3dEditorProjectTests PRIVATE Toy3dEditorCore Toy3dShaderCompilerCore)
    target_compile_definitions(Toy3dEditorProjectTests PRIVATE
        "TOY3D_PROJECT_TEST_ROOT=\"${CMAKE_CURRENT_BINARY_DIR}/project-test-output\""
        "TOY3D_PROJECT_TEST_ENGINE_ASSETS=\"${TOY3D_ENGINE_ASSET_DIR}\""
        "TOY3D_PROJECT_TEST_EDITOR_RESOURCES=\"${TOY3D_EDITOR_RESOURCE_DIR}\""
        "TOY3D_PROJECT_TEST_DEPLOYMENT=\"${BINARY_ROOT_DIR}\""
        "TOY3D_PROJECT_TEST_SHADER=\"${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/shader/painted.shader\"")
    target_compile_options(Toy3dEditorProjectTests PRIVATE "$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/utf-8>")
    add_test(NAME Toy3dEditor.Project COMMAND Toy3dEditorProjectTests)
    set_tests_properties(Toy3dEditor.Project PROPERTIES TIMEOUT 30)
endif()

if(BUILD_TESTING)
    add_executable(Toy3dEditorConsoleTests "tests/console_tests.cpp")
    target_compile_features(Toy3dEditorConsoleTests PRIVATE cxx_std_17)
    set_target_properties(Toy3dEditorConsoleTests PROPERTIES CXX_EXTENSIONS OFF FOLDER "Tests/Editor")
    target_link_libraries(Toy3dEditorConsoleTests PRIVATE Toy3dEditorCore)
    target_compile_definitions(Toy3dEditorConsoleTests PRIVATE
        "TOY3D_CONSOLE_TEST_ROOT=\"${CMAKE_CURRENT_BINARY_DIR}/console-test-output\"")
    target_compile_options(Toy3dEditorConsoleTests PRIVATE "$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/utf-8>")
    add_test(NAME Toy3dEditor.Console COMMAND Toy3dEditorConsoleTests)
    set_tests_properties(Toy3dEditor.Console PROPERTIES TIMEOUT 30)

    add_executable(Toy3dEditorFrameworkTests
        "tests/editor_framework_tests.cpp" "source/workspace/editor_workspace.cpp"
        "source/assets/texture/texture_preview_panel.cpp" "source/assets/texture/texture_preview_image.cpp")
    target_compile_features(Toy3dEditorFrameworkTests PRIVATE cxx_std_17)
    set_target_properties(Toy3dEditorFrameworkTests PROPERTIES CXX_EXTENSIONS OFF FOLDER "Tests/Editor")
    target_link_libraries(Toy3dEditorFrameworkTests PRIVATE Toy3dEditorCore)
    target_compile_definitions(Toy3dEditorFrameworkTests PRIVATE
        "TOY3D_EDITOR_PROJECT_ASSET_ROOT=\"${TOY3D_PROJECT_ASSET_DIR}\"")
    target_compile_options(Toy3dEditorFrameworkTests PRIVATE "$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/utf-8>")
    add_test(NAME Toy3dEditor.Framework COMMAND Toy3dEditorFrameworkTests)

    add_executable(Toy3dEditorTexturePreviewTests
        "tests/texture_preview_image_tests.cpp"
        "source/assets/texture/texture_preview_image.cpp")
    target_compile_features(Toy3dEditorTexturePreviewTests PRIVATE cxx_std_17)
    set_target_properties(Toy3dEditorTexturePreviewTests PROPERTIES CXX_EXTENSIONS OFF FOLDER "Tests/Editor")
    target_include_directories(Toy3dEditorTexturePreviewTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/source")
    target_link_libraries(Toy3dEditorTexturePreviewTests PRIVATE Toy3dAssets)
    target_compile_options(Toy3dEditorTexturePreviewTests PRIVATE "$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/utf-8>")
    add_test(NAME Toy3dEditor.TexturePreview COMMAND Toy3dEditorTexturePreviewTests)

    if(TOY3D_ENABLE_VULKAN_RHI AND (WIN32 OR APPLE))
        add_executable(Toy3dEditorThumbnailTests
            "tests/thumbnail_integration_tests.cpp"
            "tests/editor_play_integration_tests.cpp"
            "source/workspace/editor_workspace.cpp"
            "source/assets/material/material_edit_session.cpp"
            "source/assets/thumbnails/asset_thumbnail_pool.cpp" "source/assets/thumbnails/thumbnail_preview_scene.cpp"
            "source/panels/content_browser_panel.cpp"
            "source/assets/mesh/static_mesh_import_dialog.cpp" "${TOY3D_MODEL_PICKER_SOURCE}"
)
        target_compile_features(Toy3dEditorThumbnailTests PRIVATE cxx_std_17)
        set_target_properties(Toy3dEditorThumbnailTests PROPERTIES CXX_EXTENSIONS OFF FOLDER "Tests/Editor")
        target_include_directories(Toy3dEditorThumbnailTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/source")
        target_link_libraries(Toy3dEditorThumbnailTests PRIVATE Toy3dEditorCore Toy3dRuntime Toy3dAssets Toy3dCore imgui)
        target_link_libraries(Toy3dEditorThumbnailTests PRIVATE ${TOY3D_MODEL_PICKER_LIBRARIES})
        if(TOY3D_ENABLE_ASSIMP_MODEL_IMPORT)
            target_sources(Toy3dEditorThumbnailTests PRIVATE "source/assets/mesh/static_mesh_asset_tools.cpp")
            target_link_libraries(Toy3dEditorThumbnailTests PRIVATE Toy3dAssetPipeline)
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
        add_test(NAME Toy3dEditor.Play COMMAND Toy3dEditorThumbnailTests --pie-integration)
        set_tests_properties(Toy3dEditor.Play PROPERTIES TIMEOUT 45 RESOURCE_LOCK Toy3dGpuWindow)
    endif()
    add_executable(Toy3dEditorWorkspaceTests
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/workspace_tests.cpp"
        "source/assets/material/material_edit_session.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/source/workspace/editor_workspace.cpp")
    target_compile_features(Toy3dEditorWorkspaceTests PRIVATE cxx_std_17)
    set_target_properties(Toy3dEditorWorkspaceTests PROPERTIES CXX_EXTENSIONS OFF FOLDER "Tests/Editor")
    target_include_directories(Toy3dEditorWorkspaceTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/source")
    target_sources(Toy3dEditorWorkspaceTests PRIVATE "source/assets/material/material_asset_tools.cpp")
    target_link_libraries(Toy3dEditorWorkspaceTests PRIVATE Toy3dCore Toy3dAssets)
    target_compile_options(Toy3dEditorWorkspaceTests PRIVATE "$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/utf-8>")
    add_test(NAME Toy3dEditor.Workspace COMMAND Toy3dEditorWorkspaceTests)

    add_executable(Toy3dEditorMaterialTests
        "tests/material_edit_tests.cpp"
        "source/assets/material/material_edit_session.cpp"
        "source/workspace/editor_workspace.cpp")
    target_compile_features(Toy3dEditorMaterialTests PRIVATE cxx_std_17)
    set_target_properties(Toy3dEditorMaterialTests PROPERTIES CXX_EXTENSIONS OFF FOLDER "Tests/Editor")
    target_include_directories(Toy3dEditorMaterialTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/source")
    target_link_libraries(Toy3dEditorMaterialTests PRIVATE Toy3dCore Toy3dAssets)
    target_compile_definitions(Toy3dEditorMaterialTests PRIVATE
        "TOY3D_MATERIAL_TEST_ROOT=\"${CMAKE_BINARY_DIR}/material-editor-test-output\"")
    target_compile_options(Toy3dEditorMaterialTests PRIVATE "$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/utf-8>")
    add_test(NAME Toy3dEditor.Material COMMAND Toy3dEditorMaterialTests)

    add_executable(Toy3dEditorMaterialUiTests
        "tests/material_ui_tests.cpp"
        "source/assets/material/material_edit_session.cpp"
        "source/assets/material/material_editor_panel.cpp"
        "source/workspace/editor_workspace.cpp")
    target_compile_features(Toy3dEditorMaterialUiTests PRIVATE cxx_std_17)
    set_target_properties(Toy3dEditorMaterialUiTests PROPERTIES CXX_EXTENSIONS OFF FOLDER "Tests/Editor")
    target_include_directories(Toy3dEditorMaterialUiTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/source")
    target_link_libraries(Toy3dEditorMaterialUiTests PRIVATE Toy3dEditorCore Toy3dRuntime Toy3dAssets imgui)
    target_compile_definitions(Toy3dEditorMaterialUiTests PRIVATE WITH_EDITOR=1 WITH_EDITORONLY_DATA=1
        "TOY3D_MATERIAL_TEST_ROOT=\"${CMAKE_BINARY_DIR}/material-ui-test-output\""
        "TOY3D_SHADER_MAP_ENTRY_ROOT=\"${BINARY_ROOT_DIR}shader/phong\""
        "TOY3D_EDITOR_ENGINE_ASSET_ROOT=\"${TOY3D_ENGINE_ASSET_DIR}\""
        "TOY3D_EDITOR_RESOURCE_ROOT=\"${TOY3D_EDITOR_RESOURCE_DIR}\""
        "TOY3D_EDITOR_DEPLOY_ROOT=\"${BINARY_ROOT_DIR}\"")
    target_compile_options(Toy3dEditorMaterialUiTests PRIVATE "$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/utf-8>")
    add_test(NAME Toy3dEditor.MaterialUi COMMAND Toy3dEditorMaterialUiTests)

    add_executable(Toy3dEditorPlacementTests
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/placement_tests.cpp"
)
    target_sources(Toy3dEditorPlacementTests PRIVATE
        "source/assets/material/material_edit_session.cpp" "source/workspace/editor_workspace.cpp")
    target_compile_features(Toy3dEditorPlacementTests PRIVATE cxx_std_17)
    set_target_properties(Toy3dEditorPlacementTests PROPERTIES CXX_EXTENSIONS OFF FOLDER "Tests/Editor")
    target_include_directories(Toy3dEditorPlacementTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/source")
    target_link_libraries(Toy3dEditorPlacementTests PRIVATE Toy3dEditorCore Toy3dRuntime Toy3dAssets imguizmo)
    target_compile_definitions(Toy3dEditorPlacementTests PRIVATE
        WITH_EDITOR=1 WITH_EDITORONLY_DATA=1
        "TOY3D_SHADER_MAP_ENTRY_ROOT=\"${BINARY_ROOT_DIR}shader/phong\"")
    target_compile_options(Toy3dEditorPlacementTests PRIVATE "$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/utf-8>")
    add_test(NAME Toy3dEditor.Placement COMMAND Toy3dEditorPlacementTests)

    add_executable(Toy3dEditorMaterialAssignmentTests
        "tests/material_assignment_tests.cpp"
         "source/assets/material/material_edit_session.cpp"
        "source/workspace/editor_workspace.cpp"
)
    target_compile_features(Toy3dEditorMaterialAssignmentTests PRIVATE cxx_std_17)
    set_target_properties(Toy3dEditorMaterialAssignmentTests PROPERTIES CXX_EXTENSIONS OFF FOLDER "Tests/Editor")
    target_include_directories(Toy3dEditorMaterialAssignmentTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/source")
    target_link_libraries(Toy3dEditorMaterialAssignmentTests PRIVATE Toy3dEditorCore Toy3dRuntime Toy3dAssets imguizmo)
    target_compile_definitions(Toy3dEditorMaterialAssignmentTests PRIVATE WITH_EDITOR=1 WITH_EDITORONLY_DATA=1
        "TOY3D_MATERIAL_TEST_ROOT=\"${CMAKE_BINARY_DIR}/material-assignment-test-output\""
        "TOY3D_SHADER_MAP_ENTRY_ROOT=\"${BINARY_ROOT_DIR}shader/phong\""
        "TOY3D_EDITOR_ENGINE_ASSET_ROOT=\"${TOY3D_ENGINE_ASSET_DIR}\""
        "TOY3D_EDITOR_RESOURCE_ROOT=\"${TOY3D_EDITOR_RESOURCE_DIR}\""
        "TOY3D_EDITOR_DEPLOY_ROOT=\"${BINARY_ROOT_DIR}\"")
    target_compile_options(Toy3dEditorMaterialAssignmentTests PRIVATE "$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/utf-8>")
    add_test(NAME Toy3dEditor.MaterialAssignment COMMAND Toy3dEditorMaterialAssignmentTests)
    add_test(NAME Toy3dEditor.MaterialAssignmentMultiThread COMMAND Toy3dEditorMaterialAssignmentTests --multithread)
    set_tests_properties(Toy3dEditor.MaterialAssignment Toy3dEditor.MaterialAssignmentMultiThread PROPERTIES TIMEOUT 45)
    if(TOY3D_ENABLE_VULKAN_RHI)
        add_executable(Toy3dEditorMaterialShaderTests
            "tests/material_shader_tests.cpp"
             "source/assets/material/material_edit_session.cpp"
            "source/assets/material/material_editor_panel.cpp" "source/workspace/editor_workspace.cpp"
)
        target_compile_features(Toy3dEditorMaterialShaderTests PRIVATE cxx_std_17)
        set_target_properties(Toy3dEditorMaterialShaderTests PROPERTIES CXX_EXTENSIONS OFF FOLDER "Tests/Editor")
        target_link_libraries(Toy3dEditorMaterialShaderTests PRIVATE Toy3dEditorCore Toy3dAssets imgui)
        target_compile_definitions(Toy3dEditorMaterialShaderTests PRIVATE WITH_EDITOR=1 WITH_EDITORONLY_DATA=1
            "TOY3D_SHADER_TEST_ROOT=\"${CMAKE_BINARY_DIR}/material-shader-test-output\""
            "TOY3D_SHADER_TEST_SOURCE=\"${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/shader/painted.shader\""
            "TOY3D_SHADER_TEST_INCLUDE=\"${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/shader/include/project_common.hlsli\""
            "TOY3D_SHADER_MAP_ENTRY_ROOT=\"${BINARY_ROOT_DIR}shader/phong\""
            "TOY3D_EDITOR_ENGINE_SHADER_ROOT=\"${ENGINE_ROOT_DIR}/shader/builtin\""
            "TOY3D_EDITOR_ENGINE_INCLUDE_ROOT=\"${ENGINE_ROOT_DIR}/shader/include\""
            "TOY3D_EDITOR_SHADER_COMPILER=\"${BINARY_ROOT_DIR}$<TARGET_FILE_NAME:Toy3dShaderCompiler>\""
            "TOY3D_EDITOR_SHADER_TOOLCHAIN=\"${TOY3D_SHADER_TOOLCHAIN_ROOT}\""
            "TOY3D_EDITOR_ENGINE_ASSET_ROOT=\"${TOY3D_ENGINE_ASSET_DIR}\""
            "TOY3D_EDITOR_RESOURCE_ROOT=\"${TOY3D_EDITOR_RESOURCE_DIR}\""
            "TOY3D_EDITOR_DEPLOY_ROOT=\"${BINARY_ROOT_DIR}\"")
        if(MSVC)
            target_compile_options(Toy3dEditorMaterialShaderTests PRIVATE /utf-8)
        endif()
        if(WIN32)
            target_sources(Toy3dEditorMaterialShaderTests PRIVATE "${EDITOR_ICON_FILE}")
        endif()
        add_dependencies(Toy3dEditorMaterialShaderTests Toy3dShaders Toy3dShaderCompiler)
        add_test(NAME Toy3dEditor.MaterialShader COMMAND Toy3dEditorMaterialShaderTests)
        set_tests_properties(Toy3dEditor.MaterialShader PROPERTIES TIMEOUT 150 RESOURCE_LOCK Toy3dGpuWindow)
    endif()
    foreach(TOY3D_SHADER_UI_TEST_TARGET Toy3dEditorMaterialUiTests Toy3dEditorThumbnailTests)
        if(TARGET ${TOY3D_SHADER_UI_TEST_TARGET})
            target_link_libraries(${TOY3D_SHADER_UI_TEST_TARGET} PRIVATE Toy3dEditorCore)
        endif()
    endforeach()
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

target_link_libraries(Toy3dEditor PRIVATE Toy3dEditorCore)
