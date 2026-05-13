cmake_minimum_required(VERSION 3.16)
project(rkvc
    VERSION 0.1.0
    DESCRIPTION "RK3588 H.265 Video Codec Library (RKMPP)"
    LANGUAGES C
)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── 选项 ──────────────────────────────────────────────────────────────
option(RKVC_BUILD_SHARED   "Build shared library"      ON)
option(RKVC_BUILD_STATIC   "Build static library"      ON)
option(RKVC_BUILD_EXAMPLES "Build example programs"    ON)
option(RKVC_BUILD_BENCH    "Build benchmark tool"      ON)
option(RKVC_BUILD_TESTS    "Build unit tests (CMocka)" OFF)
option(RKVC_ENABLE_TIDY    "Enable clang-tidy checks"  OFF)
option(RKVC_BUILD_DOCS     "Build Doxygen API docs"    OFF)
option(RKVC_INSTALL        "Generate install targets"   ON)

# ── FFmpeg (ffmpeg-rockchip) ──────────────────────────────────────────
# 优先使用 /usr/local/ffmpeg-rockchip
set(FFMPEG_ROOT "/usr/local/ffmpeg-rockchip" CACHE PATH "Path to ffmpeg-rockchip installation")

if(EXISTS "${FFMPEG_ROOT}/include/libavcodec/avcodec.h")
    message(STATUS "Using ffmpeg-rockchip from ${FFMPEG_ROOT}")
    set(AVCODEC_INCLUDE_DIRS "${FFMPEG_ROOT}/include")
    set(AVCODEC_LIB_DIRS     "${FFMPEG_ROOT}/lib")
else()
    # Fallback: 用 pkg-config
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(AVCODEC  REQUIRED libavcodec)
    pkg_check_modules(AVFORMAT REQUIRED libavformat)
    pkg_check_modules(AVUTIL   REQUIRED libavutil)
endif()

# ── 源文件 ─────────────────────────────────────────────────────────────
set(RKVC_SOURCES
    lib/init.c
    lib/frame.c
    lib/encoder.c
    lib/decoder.c
    lib/stream.c
    lib/utils.c
)

# ── 库目标 ─────────────────────────────────────────────────────────────
if(RKVC_BUILD_SHARED)
    add_library(rkvc_shared SHARED ${RKVC_SOURCES})
    target_include_directories(rkvc_shared
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include>
        PRIVATE
            ${AVCODEC_INCLUDE_DIRS}
            ${CMAKE_CURRENT_SOURCE_DIR}/lib
    )
    target_link_directories(rkvc_shared PRIVATE ${AVCODEC_LIB_DIRS})
    target_link_libraries(rkvc_shared PRIVATE avcodec avformat avutil swscale pthread m)
    set_target_properties(rkvc_shared PROPERTIES
        OUTPUT_NAME rkvc
        VERSION     ${PROJECT_VERSION}
        SOVERSION   0
        POSITION_INDEPENDENT_CODE ON
    )
    # soname alias
    add_library(rkvc::shared ALIAS rkvc_shared)
endif()

if(RKVC_BUILD_STATIC)
    add_library(rkvc_static STATIC ${RKVC_SOURCES})
    target_include_directories(rkvc_static
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include>
        PRIVATE
            ${AVCODEC_INCLUDE_DIRS}
            ${CMAKE_CURRENT_SOURCE_DIR}/lib
    )
    target_link_directories(rkvc_static PRIVATE ${AVCODEC_LIB_DIRS})
    target_link_libraries(rkvc_static PRIVATE avcodec avformat avutil swscale pthread m)
    set_target_properties(rkvc_static PROPERTIES OUTPUT_NAME rkvc)
    add_library(rkvc::static ALIAS rkvc_static)
endif()

# 默认别名
if(RKVC_BUILD_SHARED)
    add_library(rkvc ALIAS rkvc_shared)
elseif(RKVC_BUILD_STATIC)
    add_library(rkvc ALIAS rkvc_static)
endif()

# ── 示例程序 ──────────────────────────────────────────────────────────
if(RKVC_BUILD_EXAMPLES)
    set(EXAMPLE_TARGETS "")

    foreach(EXAMPLE encode_file decode_file stream_encode stream_decode transcode)
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/examples/${EXAMPLE}.c")
            add_executable(example_${EXAMPLE} examples/${EXAMPLE}.c)
            target_link_libraries(example_${EXAMPLE} PRIVATE rkvc)
            target_include_directories(example_${EXAMPLE}
                PRIVATE ${AVCODEC_INCLUDE_DIRS})
            target_link_directories(example_${EXAMPLE}
                PRIVATE ${AVCODEC_LIB_DIRS})
            list(APPEND EXAMPLE_TARGETS example_${EXAMPLE})
        endif()
    endforeach()
endif()

# ── 基准测试工具 ──────────────────────────────────────────────────────
if(RKVC_BUILD_BENCH)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/bench/rkvc_bench.c")
        add_executable(rkvc_bench bench/rkvc_bench.c)
        target_link_libraries(rkvc_bench PRIVATE rkvc)
        target_include_directories(rkvc_bench
            PRIVATE ${AVCODEC_INCLUDE_DIRS})
        target_link_directories(rkvc_bench
            PRIVATE ${AVCODEC_LIB_DIRS})
    endif()
endif()

# ── 安装 ──────────────────────────────────────────────────────────────
if(RKVC_INSTALL)
    include(GNUInstallDirs)

    install(DIRECTORY include/rkvc
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )

    if(RKVC_BUILD_SHARED)
        install(TARGETS rkvc_shared
            LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
            ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        )
    endif()

    if(RKVC_BUILD_STATIC)
        install(TARGETS rkvc_static
            ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        )
    endif()

    # pkg-config 文件
    configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/lib/rkvc.pc.in
        ${CMAKE_CURRENT_BINARY_DIR}/rkvc.pc
        @ONLY
    )
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/rkvc.pc
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig
    )

    # CMake config
    include(CMakePackageConfigHelpers)
    configure_package_config_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/lib/rkvc-config.cmake.in
        ${CMAKE_CURRENT_BINARY_DIR}/rkvc-config.cmake
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/rkvc
    )
    write_basic_package_version_file(
        ${CMAKE_CURRENT_BINARY_DIR}/rkvc-config-version.cmake
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion
    )
    install(FILES
        ${CMAKE_CURRENT_BINARY_DIR}/rkvc-config.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/rkvc-config-version.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/rkvc
    )
endif()

# ── clang-tidy ────────────────────────────────────────────────────────
if(RKVC_ENABLE_TIDY)
    find_program(CLANG_TIDY_EXE NAMES "clang-tidy")
    if(CLANG_TIDY_EXE)
        set(CMAKE_C_CLANG_TIDY "${CLANG_TIDY_EXE};--header-filter=${CMAKE_CURRENT_SOURCE_DIR}/include/.*")
        message(STATUS "clang-tidy enabled: ${CLANG_TIDY_EXE}")
    else()
        message(WARNING "clang-tidy not found, skipping")
    endif()
endif()

# 自定义 tidy 目标 (不影响正常编译)
find_program(CLANG_TIDY_CMD NAMES "clang-tidy")
if(CLANG_TIDY_CMD AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/build/compile_commands.json")
    file(GLOB TIDY_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/lib/*.c)
    add_custom_target(tidy
        COMMAND ${CLANG_TIDY_CMD}
            -p ${CMAKE_CURRENT_BINARY_DIR}
            --header-filter=${CMAKE_CURRENT_SOURCE_DIR}/include/.*
            ${TIDY_SOURCES}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Running clang-tidy on lib/*.c"
        VERBATIM
    )
endif()

# ── 单元测试 (CMocka) ────────────────────────────────────────────────
if(RKVC_BUILD_TESTS)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(CMOCKA REQUIRED cmocka)

    set(RKVC_TEST_SOURCES
        tests/test_types.c
        tests/test_frame.c
    )

    foreach(TEST_SRC ${RKVC_TEST_SOURCES})
        get_filename_component(TEST_NAME ${TEST_SRC} NAME_WE)
        add_executable(${TEST_NAME} ${TEST_SRC})
        target_link_libraries(${TEST_NAME} PRIVATE rkvc_static ${CMOCKA_LIBRARIES})
        target_include_directories(${TEST_NAME}
            PRIVATE
                ${CMAKE_CURRENT_SOURCE_DIR}/include
                ${CMOCKA_INCLUDE_DIRS}
                ${AVCODEC_INCLUDE_DIRS}
        )
        target_link_directories(${TEST_NAME}
            PRIVATE
                ${AVCODEC_LIB_DIRS}
                ${CMOCKA_LIBRARY_DIRS}
        )
        add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
    endforeach()

    enable_testing()
endif()

# ── Doxygen API 文档 ─────────────────────────────────────────────────
if(RKVC_BUILD_DOCS)
    find_package(Doxygen REQUIRED)

    set(DOXYGEN_IN  ${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile.in)
    set(DOXYGEN_OUT ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile)

    configure_file(${DOXYGEN_IN} ${DOXYGEN_OUT} @ONLY)

    add_custom_target(docs
        COMMAND ${DOXYGEN_EXECUTABLE} ${DOXYGEN_OUT}
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        COMMENT "Generating API documentation with Doxygen"
        VERBATIM
    )
endif()
