cmake_minimum_required(VERSION 3.16)
project(qoiview VERSION 0.1.0)

include(cmake/prelude.cmake)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(fmt REQUIRED)
find_package(glfw3 REQUIRED)
find_package(glbinding REQUIRED)
find_package(khrplatform REQUIRED)
find_package(CLI11 REQUIRED)

include(cmake/fetched-libs.cmake)

add_executable(
    qoiview
    source/main.cpp
    source/qoiview.cpp
    source/async_decoder.cpp
)
target_include_directories(qoiview PUBLIC include)
target_link_libraries(
    qoiview
    PRIVATE
        fmt::fmt
        glfw
        glbinding::glbinding
        khrplatform::khrplatform
        CLI11::CLI11
        fetch::qoipp
)
target_compile_definitions(
    qoiview
    PUBLIC QOIVIEW_VERSION_STRING="${CMAKE_PROJECT_VERSION}"
)

if(MSVC)
    target_compile_options(qoiview PRIVATE /W4 /WX)
else()
    target_compile_options(qoiview PRIVATE -Wall -Wextra -Wconversion)

    # # sanitizer
    # target_compile_options(qoiview PRIVATE -fsanitize=address,leak,undefined)
    # target_link_options(qoiview PRIVATE -fsanitize=address,leak,undefined)
endif()
