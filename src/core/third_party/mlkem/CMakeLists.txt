cmake_minimum_required(VERSION 3.10)

# mlkem-native v1.2.0, portable-C monolithic build. The randomized upstream
# entry points are deliberately disabled: SwQuicMlKem768 supplies entropy via
# SwQuicRandom and calls the deterministic FIPS 203 entry points.
add_library(SwMlKem STATIC mlkem/mlkem_native.c)
target_include_directories(SwMlKem PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/mlkem")
target_compile_definitions(SwMlKem PUBLIC
    MLK_CONFIG_PARAMETER_SET=768
    MLK_CONFIG_NAMESPACE_PREFIX=sw_mlkem
    MLK_CONFIG_NO_RANDOMIZED_API=1)
set_target_properties(SwMlKem PROPERTIES
    C_STANDARD 90
    C_STANDARD_REQUIRED ON
    POSITION_INDEPENDENT_CODE ON)

# Third-party sources retain upstream's warning policy and are not made part of
# SwStack's /W4/-Wpedantic contract. Warnings in the wrapper remain enabled.
if(MSVC)
    target_compile_options(SwMlKem PRIVATE /w)
else()
    target_compile_options(SwMlKem PRIVATE -w)
endif()
