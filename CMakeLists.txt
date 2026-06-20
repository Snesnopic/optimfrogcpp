cmake_minimum_required(VERSION 3.10)
project(optimfrogcpp CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_STANDARD 99)

include_directories(include)

add_library(optimfrogcpp_lib
    src/optimfrog.cpp
    src/optimfrog_decoder.cpp
    src/optimfrog_decoder_entropy.cpp
    src/optimfrog_decoder_predictor.cpp
)

add_executable(optimfrogcpp src/main.cpp)
target_link_libraries(optimfrogcpp optimfrogcpp_lib)
