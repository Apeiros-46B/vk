cmake_minimum_required(VERSION 3.21)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

project(vk C CXX)

# deps
find_package(PkgConfig REQUIRED)
find_package(glm REQUIRED)
find_package(SDL2 REQUIRED)
find_package(VulkanMemoryAllocator REQUIRED)
find_package(Boost REQUIRED CONFIG)
find_package(vk-bootstrap CONFIG QUIET)
if(NOT vk-bootstrap_FOUND)
    message(STATUS "vk-bootstrap Config not found. Attempting manual search...")
    find_path(vk-bootstrap_INCLUDE_DIR
        NAMES VkBootstrap.h
        PATH_SUFFIXES vk-bootstrap
    )
    find_library(vk-bootstrap_LIBRARY
        NAMES vk-bootstrap
    )
    if(vk-bootstrap_INCLUDE_DIR AND vk-bootstrap_LIBRARY)
        message(STATUS "Found vk-bootstrap manually at: ${vk-bootstrap_LIBRARY}")
        # create imported target manually so we can link it consistently
        add_library(vk-bootstrap::vk-bootstrap UNKNOWN IMPORTED)
        set_target_properties(vk-bootstrap::vk-bootstrap PROPERTIES
            IMPORTED_LOCATION "${vk-bootstrap_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${vk-bootstrap_INCLUDE_DIR}"
        )
    else()
        message(FATAL_ERROR "Could not find vk-bootstrap via Config or Manual search.")
    endif()
endif()

# compile slang shaders
find_program(SLANGC_EXECUTABLE slangc REQUIRED DOC "Path to the Slang compiler")
function(target_slang_shaders TARGET_NAME)
    set(SHADER_SOURCES ${ARGN})
    foreach(SHADER_SOURCE ${SHADER_SOURCES})
        get_filename_component(FILE_NAME ${SHADER_SOURCE} NAME)
        get_filename_component(FILE_BASE ${SHADER_SOURCE} NAME_WE)
        set(SHADER_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/${FILE_BASE}.spv")
        add_custom_command(
            OUTPUT ${SHADER_OUTPUT}
            COMMAND ${SLANGC_EXECUTABLE}
                    ${SHADER_SOURCE}
                    -target spirv
                    -o ${SHADER_OUTPUT}
            DEPENDS ${SHADER_SOURCE}
            COMMENT "Compiling Slang shader: ${FILE_NAME} -> ${FILE_BASE}.spv"
            VERBATIM
        )
        target_sources(${TARGET_NAME} PRIVATE ${SHADER_OUTPUT})
        add_custom_command(
            TARGET ${TARGET_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${SHADER_OUTPUT}
            $<TARGET_FILE_DIR:${TARGET_NAME}>
        )
    endforeach()
endfunction()

# specify source code
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS
	${PROJECT_SOURCE_DIR}/src/*.c
	${PROJECT_SOURCE_DIR}/src/*.cc
	${PROJECT_SOURCE_DIR}/src/*.cpp
)
file(GLOB_RECURSE SLANG_SOURCES CONFIGURE_DEPENDS
	${PROJECT_SOURCE_DIR}/src/shaders/*.slang
)

add_executable(${PROJECT_NAME} ${SOURCES})
target_slang_shaders(${PROJECT_NAME} ${SLANG_SOURCES})
target_compile_definitions(${PROJECT_NAME} PRIVATE VK_NO_PROTOTYPES VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1)
target_include_directories(${PROJECT_NAME} PRIVATE ${PROJECT_SOURCE_DIR}/include)
set_property(TARGET ${PROJECT_NAME} PROPERTY C_STANDARD 20)
set_property(TARGET ${PROJECT_NAME} PROPERTY CXX_STANDARD 20)

include(GNUInstallDirs)
install(TARGETS ${PROJECT_NAME} DESTINATION ${CMAKE_INSTALL_FULL_BINDIR})

target_link_libraries(${PROJECT_NAME} PRIVATE
	glm::glm
	SDL2::SDL2
	GPUOpen::VulkanMemoryAllocator
	vk-bootstrap::vk-bootstrap
	Boost::boost
)
