cmake_minimum_required(VERSION 3.21)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

project(vk C CXX)

find_package(glm REQUIRED)
find_package(SDL2 REQUIRED)
find_package(VulkanMemoryAllocator REQUIRED)
find_package(Boost REQUIRED CONFIG)

find_package(PkgConfig REQUIRED)

find_package(vk-bootstrap CONFIG QUIET)
if(NOT vk-bootstrap_FOUND)
    message(STATUS "vk-bootstrap Config not found. Attempting manual search...")

    # Find the header
    find_path(vk-bootstrap_INCLUDE_DIR
        NAMES VkBootstrap.h
        PATH_SUFFIXES vk-bootstrap # Nix often installs to include/vk-bootstrap
    )

    # Find the library
    find_library(vk-bootstrap_LIBRARY
        NAMES vk-bootstrap
    )

    if(vk-bootstrap_INCLUDE_DIR AND vk-bootstrap_LIBRARY)
        message(STATUS "Found vk-bootstrap manually at: ${vk-bootstrap_LIBRARY}")

        # Create the Imported Target manually so we can link it consistently
        add_library(vk-bootstrap::vk-bootstrap UNKNOWN IMPORTED)
        set_target_properties(vk-bootstrap::vk-bootstrap PROPERTIES
            IMPORTED_LOCATION "${vk-bootstrap_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${vk-bootstrap_INCLUDE_DIR}"
        )
    else()
        message(FATAL_ERROR "Could not find vk-bootstrap via Config or Manual search.")
    endif()
endif()
# pkg_check_modules(vk-bootstrap REQUIRED IMPORTED_TARGET vk-bootstrap)
# find_package(vk-bootstrap CONFIG REQUIRED)

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
target_link_libraries(${PROJECT_NAME} PRIVATE
	glm::glm
	SDL2::SDL2
	GPUOpen::VulkanMemoryAllocator
	vk-bootstrap::vk-bootstrap
	Boost::boost
)
