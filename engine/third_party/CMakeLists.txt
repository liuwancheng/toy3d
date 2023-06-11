set(3rdparty_folder "ThirdParty")

# imgui,暂时先屏蔽
# include(imgui.cmake)
# set_target_properties(imgui PROPERTIES FOLDER ${3rdparty_folder}/imgui)

# stb 暂时先屏蔽
# include(stb.cmake)
# set_target_properties(stb PROPERTIES FOLDER ${3rdparty_folder}/stb)

# glm
option(BUILD_STATIC_LIBS "" ON)
option(BUILD_TESTING "" OFF)
option(GLM_TEST_ENABLE "" OFF)
add_subdirectory(glm)
set_target_properties(glm_static PROPERTIES FOLDER ${3rdparty_folder}/glm)

# glfw
option(GLFW_BUILD_EXAMPLES "" OFF)
option(GLFW_BUILD_TESTS "" OFF)
option(GLFW_BUILD_DOCS "" OFF)
option(GLFW_INSTALL "" OFF)
add_subdirectory(glfw)
set_target_properties(glfw PROPERTIES FOLDER ${3rdparty_folder}/glfw)
set_target_properties(update_mappings PROPERTIES FOLDER ${3rdparty_folder}/glfw)

# tinyobjloader
add_library(tinyobjloader STATIC)
target_sources(tinyobjloader PRIVATE
    tinyobjloader/tiny_obj_loader.h
    tinyobjloader/tiny_obj_loader.cc
)
target_include_directories(tinyobjloader PUBLIC tinyobjloader)

# vma
add_library(vma INTERFACE)
target_include_directories(vma INTERFACE vma)
target_sources(vma PRIVATE vma/vk_mem_alloc.h)