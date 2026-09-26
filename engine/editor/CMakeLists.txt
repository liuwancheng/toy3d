set(TARGET_NAME Toy3dEditor)

file(GLOB EDITOR_HEADERS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/source/*.h)
file(GLOB EDITOR_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/source/*.cpp)
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${EDITOR_HEADERS} ${EDITOR_SOURCES} )

set(ASSET_ABS_DIR ${ENGINE_ROOT_DIR}/${ASSET_NAME})

if(WIN32)
    set(EDITOR_ICON_FILE ${ASSET_ABS_DIR}/win32/Toy3d.rc)
    add_executable(${TARGET_NAME} ${EDITOR_HEADERS} ${EDITOR_SOURCES} ${EDITOR_ICON_FILE})

    set(RESOURCE_H ${ASSET_ABS_DIR}/win32/resource.h)
    target_include_directories(${TARGET_NAME} PRIVATE ${ASSET_ABS_DIR}/win32)
    set_source_files_properties(${EDITOR_ICON_FILE} PROPERTIES OBJECT_DEPENDS ${RESOURCE_H})

    source_group("resource" FILES ${EDITOR_ICON_FILE} ${RESOURCE_H})
elseif(APPLE)
    # 使用 macOS 的 .icns 图标文件和 Info.plist
    set(EDITOR_ICON_FILE ${ASSET_ABS_DIR}/mac/Toy3d.icns)
    set(EDITOR_PLIST_TEMPLATE ${ASSET_ABS_DIR}/mac/Info.plist)
    
    # 创建 macOS 应用程序包，并指定 Info.plist 模板
    add_executable(${TARGET_NAME} MACOSX_BUNDLE ${EDITOR_HEADERS} ${EDITOR_SOURCES})
    set_target_properties(${TARGET_NAME} PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST ${EDITOR_PLIST_TEMPLATE}
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
  PUBLIC $<BUILD_INTERFACE:${THIRD_PARTY_DIR}/stb>
)

set_target_properties(${TARGET_NAME} PROPERTIES CXX_STANDARD 17 OUTPUT_NAME "Toy3dEditor")
set_target_properties(${TARGET_NAME} PROPERTIES FOLDER "Engine")

target_compile_options(${TARGET_NAME} PUBLIC "$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/WX->")
target_compile_options(${TARGET_NAME} PRIVATE "$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/utf-8>")

target_compile_definitions(${TARGET_NAME}
  PRIVATE
    "TOY3D_SHADER_MAP_ENTRY_ROOT=\"${BINARY_ROOT_DIR}shader/phong\"")

target_link_libraries(${TARGET_NAME} Toy3dRuntime imguizmo)
target_compile_definitions(${TARGET_NAME} PRIVATE WITH_EDITOR=1 WITH_EDITORONLY_DATA=1)

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
  COMMAND ${CMAKE_COMMAND} -E remove_directory "${BINARY_ROOT_DIR}/${ASSET_NAME}"
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${ASSET_ABS_DIR}" "${BINARY_ROOT_DIR}/${ASSET_NAME}"
)


add_custom_command(TARGET ${TARGET_NAME} ${POST_BUILD_COMMANDS})

#precompile
#set global vari used by precompile
set(TOY3D_EDITOR_HEADS "${EDITOR_HEADERS}" PARENT_SCOPE)
