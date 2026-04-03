cmake_minimum_required(VERSION 4.1)

if(NOT CUSTOM_CLANG_PATH AND CMAKE_CXX_COMPILER)
    cmake_path(GET CMAKE_CXX_COMPILER FILENAME _custom_clang_name)
    if(_custom_clang_name MATCHES "^clang([+][+]|-cl)?(\\.exe)?$")
        cmake_path(GET CMAKE_CXX_COMPILER PARENT_PATH _custom_clang_bin_dir)
        cmake_path(GET _custom_clang_bin_dir PARENT_PATH CUSTOM_CLANG_PATH)
    endif()
endif() 

if(CUSTOM_CLANG_PATH AND NOT DEFINED CACHE{CMAKE_CXX_COMPILER})
    set(CMAKE_CXX_COMPILER "${CUSTOM_CLANG_PATH}/bin/clang++" CACHE FILEPATH "C++ compiler")
    set(CMAKE_C_COMPILER   "${CUSTOM_CLANG_PATH}/bin/clang"   CACHE FILEPATH "C compiler")
endif()


project(render_visualizer)

set(CMAKE_CXX_STANDARD 26)
set(MARS_REFLECTION_FLAGS
    -freflection
    -fexpansion-statements
    -fparameter-reflection
    -fattribute-reflection
    -fannotation-attributes
)

add_subdirectory(mars)

add_executable(render_visualizer)

add_subdirectory(src)

target_include_directories(render_visualizer PRIVATE
    "include"
)

target_compile_options(render_visualizer PRIVATE
    ${MARS_REFLECTION_FLAGS}
    -Wno-unused-command-line-argument -Wno-deprecated-declarations -Wno-unused-local-typedef -Wno-error=unused-local-typedef -Wno-error=c23-extensions 
    -Wall -Wextra -Werror
)

if(MSVC)
    target_compile_options(render_visualizer PRIVATE /arch:AVX2)
else()
    target_compile_options(render_visualizer PRIVATE -mavx2)
endif()

target_link_libraries(render_visualizer PRIVATE mars)

if(WIN32)
    target_link_options(render_visualizer PRIVATE -L"${CUSTOM_CLANG_PATH}/lib")
    mars_win32_configure(render_visualizer)
elseif(LINUX)
    if(CUSTOM_CLANG_PATH)
        target_link_options(render_visualizer PRIVATE -L"${CUSTOM_CLANG_PATH}/lib")
    endif()
    mars_linux_configure(render_visualizer)
endif()

add_custom_command(TARGET render_visualizer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_SOURCE_DIR}/assets"
    "$<TARGET_FILE_DIR:render_visualizer>/assets"
)
