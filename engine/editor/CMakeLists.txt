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

target_link_libraries(${TARGET_NAME} Toy3dRuntime Toy3dResource imguizmo)
target_compile_definitions(${TARGET_NAME} PRIVATE WITH_EDITOR=1 WITH_EDITORONLY_DATA=1)

if(BUILD_TESTING)
    add_executable(Toy3dEditorWorkspaceTests
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/workspace_tests.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/source/workspace/editor_workspace.cpp")
    target_compile_features(Toy3dEditorWorkspaceTests PRIVATE cxx_std_17)
    set_target_properties(Toy3dEditorWorkspaceTests PROPERTIES CXX_EXTENSIONS OFF FOLDER "Tests/Editor")
    target_include_directories(Toy3dEditorWorkspaceTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/source")
    target_link_libraries(Toy3dEditorWorkspaceTests PRIVATE Toy3dResource)
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
