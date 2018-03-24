find_package(LLVM REQUIRED CONFIG)
set (CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_FLAGS "-Wall -fno-rtti")
add_definitions(${LLVM_DEFINITIONS})
include_directories(${LLVM_INCLUDE_DIRS})

add_subdirectory(infeasible_paths)
