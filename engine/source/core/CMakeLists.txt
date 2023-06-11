
# PackageProject.cmake will be used to make our target installable
set(TARGET_NAME Toy3DCore)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(${TARGET_NAME} STATIC)

set_target_properties(${TARGET_NAME} PROPERTIES CXX_STANDARD 17)
set_target_properties(${TARGET_NAME} PROPERTIES FOLDER "Engine")

# being a cross-platform target, we enforce standards conformance on MSVC
target_compile_options(${TARGET_NAME} PUBLIC "$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/permissive->")
target_compile_options(${TARGET_NAME} PUBLIC "$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/WX->")

#third party
target_link_libraries(${TARGET_NAME} PUBLIC ${vulkan_lib})
target_link_libraries(${TARGET_NAME} PUBLIC glm)
target_link_libraries(${TARGET_NAME} PUBLIC glfw)
target_link_libraries(${TARGET_NAME} PUBLIC vma)

target_include_directories(${TARGET_NAME} PUBLIC $<BUILD_INTERFACE:${vulkan_include}>)

# core
target_include_directories(${TARGET_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# core
file(GLOB CORE_H CONFIGURE_DEPENDS ${CMAKE_CURRENT_LIST_DIR}/*.h)
file(GLOB CORE_CPP CONFIGURE_DEPENDS ${CMAKE_CURRENT_LIST_DIR}/*.cpp)
source_group("core" FILES ${CORE_H} ${CORE_CPP})
target_sources(${TARGET_NAME} PRIVATE ${CORE_H} ${CORE_CPP})
message(STATUS ${CORE_H} ${CORE_CPP})

# core math
file(GLOB CORE_MATH_H CONFIGURE_DEPENDS ${CMAKE_CURRENT_LIST_DIR}/math/*.h)
file(GLOB CORE_MATH_CPP CONFIGURE_DEPENDS ${CMAKE_CURRENT_LIST_DIR}/math/*.cpp)
source_group("core/math" FILES ${CORE_MATH_H} ${CORE_MATH_CPP})
target_sources(${TARGET_NAME} PRIVATE ${CORE_MATH_H} ${CORE_MATH_CPP})

# core render
file(GLOB CORE_RENDER_H CONFIGURE_DEPENDS ${CMAKE_CURRENT_LIST_DIR}/render/*.h)
file(GLOB CORE_RENDERER_CPP CONFIGURE_DEPENDS ${CMAKE_CURRENT_LIST_DIR}/render/*.cpp)
source_group("core/render" FILES ${CORE_RENDER_H} ${CORE_RENDERER_CPP})
target_sources(${TARGET_NAME} PRIVATE ${CORE_RENDER_H} ${CORE_RENDERER_CPP})

# core render/pass
file(GLOB CORE_RENDER_PASS_H CONFIGURE_DEPENDS ${CMAKE_CURRENT_LIST_DIR}/render/pass/*.h)
file(GLOB CORE_RENDERER_PASS_CPP CONFIGURE_DEPENDS ${CMAKE_CURRENT_LIST_DIR}/render/pass/*.cpp)
source_group("core/render/pass" FILES ${CORE_RENDER_PASS_H} ${CORE_RENDERER_PASS_CPP})
target_sources(${TARGET_NAME} PRIVATE ${CORE_RENDER_PASS_H} ${CORE_RENDERER_PASS_CPP})

# core rhi
file(GLOB CORE_RHI_H CONFIGURE_DEPENDS ${CMAKE_CURRENT_LIST_DIR}/rhi/*.h)
file(GLOB CORE_RHI_CPP CONFIGURE_DEPENDS ${CMAKE_CURRENT_LIST_DIR}/rhi/*.cpp)
source_group("core/rhi" FILES ${CORE_RHI_H} ${CORE_RHI_CPP})
target_sources(${TARGET_NAME} PRIVATE ${CORE_RHI_H} ${CORE_RHI_CPP})
message(STATUS ${CORE_RHI_H} ${CORE_RHI_CPP})

# core rhi/vulkan
file(GLOB CORE_RHI_VULKAN_H CONFIGURE_DEPENDS ${CMAKE_CURRENT_LIST_DIR}/rhi/vulkan/*.h)
file(GLOB CORE_RHI_VULKAN_CPP CONFIGURE_DEPENDS ${CMAKE_CURRENT_LIST_DIR}/rhi/vulkan/*.cpp)
source_group("core/rhi/vulkan" FILES ${CORE_RHI_VULKAN_H} ${CORE_RHI_VULKAN_CPP})
target_sources(${TARGET_NAME} PRIVATE ${CORE_RHI_VULKAN_H} ${CORE_RHI_VULKAN_CPP})

# core rhi/vulkan/mac
file(GLOB CORE_RHI_VULKAN_MAC_H CONFIGURE_DEPENDS ${CMAKE_CURRENT_LIST_DIR}/rhi/vulkan/mac/*.h)
file(GLOB CORE_RHI_VULKAN_MAC_CPP CONFIGURE_DEPENDS ${CMAKE_CURRENT_LIST_DIR}/rhi/vulkan/mac/*.cpp)
source_group("core/rhi/vulkan/mac" FILES ${CORE_RHI_VULKAN_MAC_H} ${CORE_RHI_VULKAN_MAC_CPP})
target_sources(${TARGET_NAME} PRIVATE ${CORE_RHI_VULKAN_MAC_H} ${CORE_RHI_VULKAN_MAC_CPP})




