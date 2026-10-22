cmake_minimum_required(VERSION 4.2.3)
project(Boilerplate)

set(CMAKE_EXPORT_COMPILE_COMMANDS True)

add_executable(Boilerplate
  src/main.c
  src/vulkan_helper.c
)

target_compile_options(Boilerplate PRIVATE -Wno-unused)

target_include_directories(Boilerplate PRIVATE src)

target_link_libraries(Boilerplate PRIVATE
    vulkan
)
