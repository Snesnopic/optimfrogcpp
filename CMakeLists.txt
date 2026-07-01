cmake_minimum_required(VERSION 3.10)
project(optimfrogcpp CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -ffast-math")

include_directories(include)

add_library(optimfrogcpp_lib
    src/core/optimfrog.cpp
    src/core/optimfrog_decoder.cpp
    src/core/optimfrog_decoder_entropy.cpp
    src/core/optimfrog_decoder_predictor.cpp
    src/core/optimfrog_decoder_pred3.cpp
    src/core/optimfrog_decoder_ent3.cpp
    src/core/optimfrog_decoder_post2.cpp
    src/core/optimfrog_encoder.cpp
)

# float math that must NOT be reordered/contracted by -ffast-math (bit-exactness vs the binary,
# which was built -fno-fast-math). pred3 cascade + the mono predictor weight-update/LDLT.
set_source_files_properties(src/core/optimfrog_decoder_pred3.cpp src/core/optimfrog_decoder_predictor.cpp PROPERTIES
    COMPILE_OPTIONS "-fno-fast-math;-ffp-contract=off")

add_executable(optimfrogcpp src/cli/main.cpp)
target_link_libraries(optimfrogcpp optimfrogcpp_lib)

# encoder CLI (mono 16-bit lossless, pred=1/ent=1/post=1)
add_executable(ofrenc src/cli/ofrenc_main.cpp)
target_link_libraries(ofrenc optimfrogcpp_lib)
