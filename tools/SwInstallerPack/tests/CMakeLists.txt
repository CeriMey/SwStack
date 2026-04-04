add_executable(SwInstallerFrameworkSelfTest
    SwInstallerFrameworkSelfTest.cpp
)

target_include_directories(SwInstallerFrameworkSelfTest PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_SOURCE_DIR}/src/core/installer
    ${CMAKE_SOURCE_DIR}/tools/SwInstallerPack
)

target_compile_features(SwInstallerFrameworkSelfTest PRIVATE cxx_std_17)
target_link_libraries(SwInstallerFrameworkSelfTest PRIVATE SwMiniz)

if(WIN32)
    target_link_libraries(SwInstallerFrameworkSelfTest PRIVATE user32 gdi32 shell32 ole32 advapi32)
endif()
