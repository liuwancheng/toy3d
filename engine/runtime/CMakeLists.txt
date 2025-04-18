set(TARGET_NAME Toy3dRuntime)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
 
file(GLOB_RECURSE HEADER_FILES "*.h")
file(GLOB_RECURSE SOURCE_FILES "*.cpp")

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${HEADER_FILES} ${SOURCE_FILES})
add_library(${TARGET_NAME} ${HEADER_FILES} ${SOURCE_FILES})

# 分类平台特定的文件
file(GLOB_RECURSE WINDOWS_FILES "platform/windows/*.cpp" "platform/windows/*.h")
file(GLOB_RECURSE MACOS_FILES "platform/macos/*.cpp" "platform/macos/*.h")
file(GLOB_RECURSE LINUX_FILES "platform/linux/*.cpp" "platform/linux/*.h")
# 根据平台设置文件属性，使非当前平台的代码不参与编译
if(WIN32)
    set_source_files_properties(${MACOS_FILES} ${LINUX_FILES} PROPERTIES HEADER_FILE_ONLY TRUE)
elseif(APPLE)
    set_source_files_properties(${WINDOWS_FILES} ${LINUX_FILES} PROPERTIES HEADER_FILE_ONLY TRUE)
elseif(UNIX AND NOT APPLE)
    set_source_files_properties(${WINDOWS_FILES} ${MACOS_FILES} PROPERTIES HEADER_FILE_ONLY TRUE)
endif()

set_target_properties(${TARGET_NAME} PROPERTIES CXX_STANDARD 17)
set_target_properties(${TARGET_NAME} PROPERTIES FOLDER "Engine")

# being a cross-platform target, we enforce standards conformance on MSVC
target_compile_options(${TARGET_NAME} PUBLIC "$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/permissive->")
target_compile_options(${TARGET_NAME} PUBLIC "$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/WX->")

# Link dependencies    
target_link_libraries(${TARGET_NAME} PRIVATE tinyobjloader stb)
target_link_libraries(${TARGET_NAME} PUBLIC glfw)
target_link_libraries(${TARGET_NAME} PUBLIC imgui)
target_link_libraries(${TARGET_NAME} PUBLIC ${vulkan_lib})
target_link_libraries(${TARGET_NAME} PUBLIC vma)
target_link_libraries(${TARGET_NAME} PUBLIC glm::glm)


target_include_directories(
  ${TARGET_NAME}
  PUBLIC $<BUILD_INTERFACE:${vulkan_include}>)

# 添加项目根目录为包含目录
# target_include_directories(
#     ${TARGET_NAME}
#     PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)

target_include_directories(
    ${TARGET_NAME} 
    PUBLIC $<BUILD_INTERFACE:${ENGINE_ROOT_DIR}/runtime>)

target_include_directories(
    ${TARGET_NAME} 
    PUBLIC $<BUILD_INTERFACE:${ENGINE_ROOT_DIR}/runtime/drivers>)

# add generated files
target_include_directories(
    ${TARGET_NAME} 
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/generated>)
  
# win32 resouce.h
if(WIN32)
    target_include_directories(${TARGET_NAME} PRIVATE ${ENGINE_ROOT_DIR}/${ASSET_NAME}/win32)
endif()
