cmake_minimum_required(VERSION 3.16)
project(pet-monitor LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(PM_PLATFORM "stub" CACHE STRING "plt implementation: stub or rk3588")
set_property(CACHE PM_PLATFORM PROPERTY STRINGS stub rk3588)
if(NOT PM_PLATFORM STREQUAL "stub" AND NOT PM_PLATFORM STREQUAL "rk3588")
    message(FATAL_ERROR "PM_PLATFORM must be stub or rk3588")
endif()

add_library(base INTERFACE)
target_include_directories(base INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/src/base/include)

if(PM_PLATFORM STREQUAL "rk3588")
    enable_language(CXX)
    set(CMAKE_CXX_STANDARD 11)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)

    set(PM_MPP_INCLUDE "" CACHE PATH "MPP headers")
    set(PM_RGA_INCLUDE "" CACHE PATH "librga headers")
    set(PM_RKNN_INCLUDE "" CACHE PATH "RKNN headers")
    if(PM_MPP_INCLUDE STREQUAL "" OR PM_RGA_INCLUDE STREQUAL "" OR PM_RKNN_INCLUDE STREQUAL "")
        message(FATAL_ERROR "rk3588 needs PM_MPP_INCLUDE, PM_RGA_INCLUDE and PM_RKNN_INCLUDE")
    endif()

    add_library(plt STATIC
        src/plt/rk3588/plt_sys.cpp
        src/plt/rk3588/plt_mem.cpp
        src/plt/rk3588/plt_vdec.cpp
        src/plt/rk3588/plt_rga.cpp
        src/plt/rk3588/plt_jpeg.cpp
        src/plt/rk3588/plt_venc.cpp
        src/plt/rk3588/plt_npu.cpp
    )
    target_compile_definitions(plt PRIVATE RK_3588)
    target_include_directories(plt
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/src/plt/include
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src/plt/rk3588
            ${PM_MPP_INCLUDE}
            ${PM_RGA_INCLUDE}
            ${PM_RKNN_INCLUDE}
    )
else()
    add_library(plt STATIC src/plt/stub/plt_stub.c)
    target_include_directories(plt PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src/plt/include)
endif()

target_link_libraries(plt PUBLIC base)

add_library(media STATIC
    src/media/media_dec.c
    src/media/media_enc.c
    src/media/media_pic.c
    src/media/media_jpeg.c
)
target_include_directories(media PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src/media/include)
target_link_libraries(media PUBLIC plt)

if(PM_PLATFORM STREQUAL "stub")
    enable_testing()
    add_executable(plt_stub_test tests/plt_stub_test.c)
    target_link_libraries(plt_stub_test PRIVATE plt)
    add_test(NAME plt_stub_test COMMAND plt_stub_test)

    add_executable(media_stub_test tests/media_stub_test.c)
    target_link_libraries(media_stub_test PRIVATE media)
    add_test(NAME media_stub_test COMMAND media_stub_test)
endif()
