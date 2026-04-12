project(GridLayoutSelfTest)

add_executable(GridLayoutSelfTest GridLayoutSelfTest.cpp)

if(WIN32)
    target_link_libraries(GridLayoutSelfTest PRIVATE ws2_32 user32 gdi32)
endif()
