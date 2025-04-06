
# misc模块CMake配置
set(TARGET_NAME Toy3dMisc)

add_library(${TARGET_NAME} STATIC)

# 设置C++标准
set_target_properties(${TARGET_NAME} PROPERTIES CXX_STANDARD 17)
set_target_properties(${TARGET_NAME} PROPERTIES FOLDER "Engine")

# 包含头文件
target_include_directories(${TARGET_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# 添加源文件
file(GLOB MISC_H CONFIGURE_DEPENDS ${CMAKE_CURRENT_LIST_DIR}/*.h)
file(GLOB MISC_CPP CONFIGURE_DEPENDS ${CMAKE_CURRENT_LIST_DIR}/*.cpp)
target_sources(${TARGET_NAME} PRIVATE ${MISC_H} ${MISC_CPP})