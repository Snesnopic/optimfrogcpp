cmake_minimum_required(VERSION 3.10)
project(optimfrogcpp CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_C_STANDARD 99)
if(MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /O2 /fp:fast")
else()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -ffast-math")
endif()

include_directories(include)

# we always statically link our own implementation of the OptimFROG.h API (never a
# prebuilt OptimFROG.dll), so the SDK header's dllimport decoration is wrong here.
add_compile_definitions(IMPORT_SYMBOL=)

add_library(optimfrogcpp_lib
    src/core/optimfrog.cpp
    src/core/optimfrog_decoder.cpp
    src/core/optimfrog_decoder_entropy.cpp
    src/core/optimfrog_decoder_predictor.cpp
    src/core/optimfrog_decoder_pred3.cpp
    src/core/optimfrog_decoder_ent3.cpp
    src/core/optimfrog_decoder_post2.cpp
    src/core/optimfrog_decoder_float.cpp
    src/core/optimfrog_encoder.cpp
    src/core/optimfrog_encoder_float.cpp
    src/core/optimfrogcpp.cpp
)

# float math that must NOT be reordered/contracted (bit-exactness vs the reference binary,
# which was built -fno-fast-math). pred3 cascade + the mono predictor weight-update/LDLT.
if(MSVC)
    set_source_files_properties(src/core/optimfrog_decoder_pred3.cpp src/core/optimfrog_decoder_predictor.cpp PROPERTIES
        COMPILE_OPTIONS "/fp:precise")
else()
    set_source_files_properties(src/core/optimfrog_decoder_pred3.cpp src/core/optimfrog_decoder_predictor.cpp PROPERTIES
        COMPILE_OPTIONS "-fno-fast-math;-ffp-contract=off")
endif()

add_executable(optimfrogcpp src/cli/main.cpp)
target_link_libraries(optimfrogcpp optimfrogcpp_lib)

# encoder CLI (positional args: in.raw out.ofr rate [channels] [bps])
add_executable(ofrenc src/cli/ofrenc_main.cpp)
target_link_libraries(ofrenc optimfrogcpp_lib)

# unified CLI aiming for usage/flag parity with the original `ofr` tool
add_executable(ofr src/cli/ofr_main.cpp)
target_link_libraries(ofr optimfrogcpp_lib)
