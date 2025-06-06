# glad for OpenGL API
set(GLAD_DIR ${CMAKE_SOURCE_DIR}/extern/glad)

# glfw for windowing and input
set(GLFW_BUILD_DOCS OFF CACHE BOOL "GLFW Docs")
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "GLFW Examples")
set(GLFW_BUILD_TESTS OFF CACHE BOOL "GLFW Tests")
set(GLFW_INSTALL OFF CACHE BOOL "GLFW Install")

FetchContent_Declare(
	glfw
	GIT_REPOSITORY https://github.com/glfw/glfw.git
	GIT_TAG 3.4
	GIT_SHALLOW TRUE
	GIT_PROGRESS TRUE
)
FetchContent_MakeAvailable(glfw)


add_compile_options("$<$<COMPILE_LANG_AND_ID:C,MSVC>:/utf-8>")

set(SKRIBIDI_EXAMPLE_FILES
	example_testbed.c
	example_richtext.c
	example_icons.c
	example_cached.c
	utils.h
	utils.c
	main.c
	debug_draw.c
	debug_draw.h
)

add_executable(skribidi_example ${SKRIBIDI_EXAMPLE_FILES})

set_target_properties(skribidi_example PROPERTIES
	C_STANDARD 17
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
)

target_include_directories(skribidi_example PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${GLAD_DIR}/include)
target_link_libraries(skribidi_example PUBLIC glfw skribidi harfbuzz)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SKRIBIDI_EXAMPLE_FILES})

# Create a symbolic link to the data folder at build time, with copy fallback for Windows
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    add_custom_command(
        OUTPUT ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/data
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                ${CMAKE_CURRENT_SOURCE_DIR}/data
                ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/data
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/data
        COMMENT "Copying data folder to ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/data"
    )
else()
    add_custom_command(
        OUTPUT ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/data
        COMMAND ${CMAKE_COMMAND} -E create_symlink
                ${CMAKE_CURRENT_SOURCE_DIR}/data
                ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/data
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/data
        COMMENT "Creating symlink to data folder in ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/data"
    )
endif()

add_custom_target(data_target ALL
    DEPENDS ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/data
)

add_dependencies(skribidi_example data_target)
