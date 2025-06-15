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
add_compile_options("$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/utf-8>")

set(SKRIBIDI_EXAMPLE_FILES
	debug_draw.c
	debug_draw.h
	example_cached.c
	example_fallback.c
	example_icons.c
	example_richtext.c
	example_testbed.c
	ime.c
	ime.h
	main.c
	utils.c
	utils.h
)

add_executable(skribidi_example ${SKRIBIDI_EXAMPLE_FILES})

set_target_properties(skribidi_example PROPERTIES
	C_STANDARD 17
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
)

target_include_directories(skribidi_example PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${GLAD_DIR}/include)
target_link_libraries(skribidi_example PUBLIC glfw skribidi harfbuzz)

if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_link_libraries(skribidi_example PUBLIC imm32.lib comctl32.lib)
elseif (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(STATUS "Detecting GTK for IME support on Linux")
    find_package(PkgConfig REQUIRED)
    # Try GTK3, fallback to GTK4
    pkg_check_modules(GTK_REQ gtk+-3.0)
    if (GTK_REQ_FOUND)
        set(GTK_PKG gtk+-3.0)
    else()
        pkg_check_modules(GTK_REQ gtk4)
        if (GTK_REQ_FOUND)
            set(GTK_PKG gtk4)
        else()
            message(FATAL_ERROR "Neither gtk+-3.0 nor gtk4 found. Please install GTK.")
        endif()
    endif()

    message(STATUS "Using ${GTK_PKG} for IME support")
    pkg_check_modules(GTK ${GTK_PKG})

    target_include_directories(skribidi_example
        PRIVATE ${GTK_INCLUDE_DIRS}
    )
    target_link_libraries(skribidi_example
        PUBLIC ${GTK_LIBRARIES}
    )
    target_compile_options(skribidi_example
        PRIVATE ${GTK_CFLAGS_OTHER}
    )
elseif (APPLE)
    message(WARNING "IME support not implemented for Apple platforms")
endif()

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SKRIBIDI_EXAMPLE_FILES})

function(collect_data_files output_var dir)
    file(GLOB files "${dir}/*")
    set(all_files)
    foreach(file ${files})
        if(IS_DIRECTORY ${file})
            collect_data_files(sub_files ${file})
            list(APPEND all_files ${sub_files})
        else()
            list(APPEND all_files ${file})
        endif()
    endforeach()
    set(${output_var} ${all_files} PARENT_SCOPE)
endfunction()

# Create a symbolic link to the data folder at build time, with copy fallback for Windows
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    if(MSVC)
        # Collect all files in the data directory recursively
        collect_data_files(DATA_FILES "${CMAKE_CURRENT_SOURCE_DIR}/data")

        # Create a stamp file to mark the copy operation, placed in the config-specific output directory
        set(DATA_STAMP "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/$<CONFIG>/data_copied.stamp")

        add_custom_command(
            OUTPUT ${DATA_STAMP}
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                    ${CMAKE_CURRENT_SOURCE_DIR}/data
                    ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/$<CONFIG>/data
            COMMAND ${CMAKE_COMMAND} -E touch ${DATA_STAMP}
            DEPENDS ${DATA_FILES}
            COMMENT "Copying data folder to ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/$<CONFIG>/data"
        )

        # Add a custom target to depend on the stamp file
        add_custom_target(data_target ALL DEPENDS ${DATA_STAMP})
    else()
        # For MinGW on Windows, use copy_directory without stamp
        add_custom_command(
            OUTPUT ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/data
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                    ${CMAKE_CURRENT_SOURCE_DIR}/data
                    ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/data
            DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/data
            COMMENT "Copying data folder to ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/data"
        )

        add_custom_target(data_target ALL DEPENDS ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/data)
    endif()
else()
    # Non-Windows (symlink)
    add_custom_command(
        OUTPUT ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/data
        COMMAND ${CMAKE_COMMAND} -E create_symlink
                ${CMAKE_CURRENT_SOURCE_DIR}/data
                ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/data
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/data
        COMMENT "Creating symlink to data folder in ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/data"
    )

    add_custom_target(data_target ALL DEPENDS ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/data)
endif()

add_dependencies(skribidi_example data_target)
