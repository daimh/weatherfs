cmake_minimum_required(VERSION 3.21)
project(weatherfs C)
enable_testing()
add_executable(weatherfs weatherfs.c)
target_compile_options(weatherfs PRIVATE -Wall -D_FILE_OFFSET_BITS=64)
target_include_directories(weatherfs PRIVATE /usr/include/fuse3)
target_link_libraries(weatherfs PRIVATE fuse3 curl jansson)
add_test(
	NAME TestWeatherfs
	COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/test_weatherfs.sh
)
