cmake_minimum_required(VERSION 3.21)

project(vk C CXX)

find_package(glm REQUIRED)
find_package(SDL2 REQUIRED)
find_package(VulkanMemoryAllocator REQUIRED)
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS
	${PROJECT_SOURCE_DIR}/src/*.c
	${PROJECT_SOURCE_DIR}/src/*.cc
	${PROJECT_SOURCE_DIR}/src/*.cpp
)

# compilation
add_executable(${PROJECT_NAME} ${SOURCES})
target_compile_definitions(${PROJECT_NAME} PRIVATE VK_NO_PROTOTYPES VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1)
target_include_directories(${PROJECT_NAME} PRIVATE ${PROJECT_SOURCE_DIR}/include)
set_property(TARGET ${PROJECT_NAME} PROPERTY C_STANDARD 20)
set_property(TARGET ${PROJECT_NAME} PROPERTY CXX_STANDARD 20)

include(GNUInstallDirs)
install(TARGETS ${PROJECT_NAME} DESTINATION ${CMAKE_INSTALL_FULL_BINDIR})

# deps
set(DEPS
	glm::glm
	SDL2::SDL2
	GPUOpen::VulkanMemoryAllocator
)
add_dependencies(${PROJECT_NAME} ${DEPS})
target_link_libraries(${PROJECT_NAME} ${DEPS})
