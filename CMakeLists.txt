cmake_minimum_required(VERSION 3.16)
project(pet-monitor LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(PM_PLATFORM "stub" CACHE STRING "plt implementation: stub or rk3588")
set_property(CACHE PM_PLATFORM PROPERTY STRINGS stub rk3588)
if(NOT PM_PLATFORM STREQUAL "stub" AND NOT PM_PLATFORM STREQUAL "rk3588")
    message(FATAL_ERROR "PM_PLATFORM must be stub or rk3588")
endif()

add_library(base SHARED src/base/ini.c)
target_include_directories(base PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src/base/include)

if(PM_PLATFORM STREQUAL "rk3588")
    set(PM_MPP_INCLUDE "" CACHE PATH "MPP headers")
    set(PM_RGA_INCLUDE "" CACHE PATH "librga headers")
    set(PM_RKNN_INCLUDE "" CACHE PATH "RKNN headers")
    if(PM_MPP_INCLUDE STREQUAL "" OR PM_RGA_INCLUDE STREQUAL "" OR PM_RKNN_INCLUDE STREQUAL "")
        message(FATAL_ERROR "rk3588 needs PM_MPP_INCLUDE, PM_RGA_INCLUDE and PM_RKNN_INCLUDE")
    endif()

    add_library(plt SHARED
        src/plt/rk3588/plt_sys.cpp
        src/plt/rk3588/plt_mem.cpp
        src/plt/rk3588/plt_vdec.cpp
        src/plt/rk3588/plt_rga.cpp
        src/plt/rk3588/plt_jpeg.cpp
        src/plt/rk3588/plt_venc.cpp
        src/plt/rk3588/plt_npu.cpp
    )
    target_link_libraries(plt PUBLIC rknnrt rockchip_mpp rga)
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
    add_library(plt SHARED src/plt/stub/plt_stub.c)
    target_include_directories(plt PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src/plt/include)
endif()

target_link_libraries(plt PUBLIC base)

add_library(media SHARED
    src/media/media_sys.c
    src/media/media_mem.c
    src/media/media_dec.c
    src/media/media_enc.c
    src/media/media_pic.c
    src/media/media_jpeg.c
    src/media/media_npu.c
    src/media/media_cfg.c
)
target_include_directories(media PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src/media/include)
target_link_libraries(media PUBLIC plt)

find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET libavformat libavcodec libavutil)

add_library(net SHARED
    src/net/net_cfg.c
    src/net/net_pull.c
)
target_include_directories(net PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src/net/include)
target_link_libraries(net
    PUBLIC base
    PRIVATE PkgConfig::FFMPEG Threads::Threads
)

add_library(algo SHARED
    src/algo/algo.c
    src/algo/algo_detect.c
    src/algo/algo_track.c
    src/algo/algo_recog.c
    src/algo/algo_cat_det.c
    src/algo/algo_cfg.c
)
target_include_directories(algo PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src/algo/include)
target_link_libraries(algo PUBLIC media Threads::Threads ${CMAKE_DL_LIBS})

find_package(OpenSSL REQUIRED)

add_library(app STATIC
    src/app/app_email.cpp
    src/app/app_cfg.c
)
target_include_directories(app PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src/app/include)
target_link_libraries(app
    PUBLIC base
    PRIVATE OpenSSL::SSL OpenSSL::Crypto Threads::Threads
)

if(PM_PLATFORM STREQUAL "stub")
    enable_testing()
    add_executable(plt_stub_test tests/plt_stub_test.c)
    target_link_libraries(plt_stub_test PRIVATE plt)
    add_test(NAME plt_stub_test COMMAND plt_stub_test)

    add_library(algo_probe MODULE tests/algo_probe.c)
    target_include_directories(algo_probe PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/algo/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src/base/include
    )
    target_link_libraries(algo_probe PRIVATE algo)

    add_executable(media_stub_test tests/media_stub_test.c)
    target_link_libraries(media_stub_test PRIVATE media)
    add_test(NAME media_stub_test COMMAND media_stub_test)

    add_executable(algo_stub_test tests/algo_stub_test.c)
    target_link_libraries(algo_stub_test PRIVATE algo)
    target_compile_definitions(algo_stub_test PRIVATE "ALGO_PLUGIN=\"$<TARGET_FILE:algo_probe>\"")
    add_dependencies(algo_stub_test algo_probe)
    add_test(NAME algo_stub_test COMMAND algo_stub_test)

    add_executable(app_email_test tests/app_email_test.c)
    target_link_libraries(app_email_test PRIVATE app)
    add_test(NAME app_email_test COMMAND app_email_test)

    add_executable(net_test tests/net_test.c)
    target_link_libraries(net_test PRIVATE net)
    target_compile_definitions(net_test PRIVATE "PM_INI_SAMPLE=\"${CMAKE_SOURCE_DIR}/conf/pet-monitor.ini\"")
    add_test(NAME net_test COMMAND net_test)

    add_executable(ini_test tests/ini_test.c)
    target_link_libraries(ini_test PRIVATE base media algo app)
    target_compile_definitions(ini_test PRIVATE "PM_INI_SAMPLE=\"${CMAKE_SOURCE_DIR}/conf/pet-monitor.ini\"")
    add_test(NAME ini_test COMMAND ini_test)
endif()

if(PM_PLATFORM STREQUAL "rk3588")
    add_executable(algo_cat_demo tests/algo_cat_demo.c)
    target_link_libraries(algo_cat_demo PRIVATE algo)
endif()
