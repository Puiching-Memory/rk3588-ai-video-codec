cmake_minimum_required(VERSION 3.16)
project(rkvc
    VERSION 0.1.4
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
option(RKVC_BUILD_TOOLS    "Build CLI tools"           ON)
option(RKVC_BUILD_TESTS    "Build unit tests (CMocka)" OFF)
option(RKVC_ENABLE_ASAN    "Enable AddressSanitizer"   OFF)
option(RKVC_ENABLE_UBSAN   "Enable UndefinedBehaviorSanitizer" OFF)
option(RKVC_ENABLE_COVERAGE "Enable gcov/llvm-cov instrumentation" OFF)
option(RKVC_ENABLE_FAULT_INJECTION "Enable deterministic test fault injection hooks" OFF)
option(RKVC_ENABLE_TIDY    "Enable clang-tidy checks"  OFF)
option(RKVC_BUILD_DOCS     "Build Doxygen API docs"    OFF)
option(RKVC_INSTALL        "Generate install targets"   ON)

# ── FFmpeg (ffmpeg-rockchip) ──────────────────────────────────────────
# 始终使用 third_party/ 源码树构建的静态库
set(AVCODEC_INCLUDE_DIRS "${CMAKE_SOURCE_DIR}/third_party/ffmpeg-rockchip")
set(AVCODEC_LIB_DIRS
    "${CMAKE_SOURCE_DIR}/third_party/ffmpeg-rockchip/libavcodec"
    "${CMAKE_SOURCE_DIR}/third_party/ffmpeg-rockchip/libavformat"
    "${CMAKE_SOURCE_DIR}/third_party/ffmpeg-rockchip/libavutil"
)

# ── MPP (rockchip mpp) ───────────────────────────────────────────────
# 始终使用 third_party/ 源码树构建的静态库
set(MPP_BUILD_DIR "${CMAKE_BINARY_DIR}/mpp_build" CACHE PATH "MPP build output directory")
set(MPP_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/third_party/mpp/inc")
set(MPP_LIB_DIR "${MPP_BUILD_DIR}/mpp")

# ── 源文件 ─────────────────────────────────────────────────────────────
# FFmpeg 静态库存在循环依赖，需要用 --start-group/--end-group 包裹
set(FFMPEG_LIBS -Wl,--start-group avcodec avformat avutil -Wl,--end-group)
# 外部依赖：MPP、zlib、drm
set(EXTRA_LIBS rockchip_mpp z drm pthread m rga rt)
set(RKVC_SOURCES
    lib/init.c
    lib/frame.c
    lib/scale.c
    lib/encoder.c
    lib/decoder.c
    lib/stream.c
    lib/utils.c
)

add_library(rkvc_instrumentation INTERFACE)

if(RKVC_ENABLE_ASAN OR RKVC_ENABLE_UBSAN OR RKVC_ENABLE_COVERAGE)
    if(NOT CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        message(FATAL_ERROR "Sanitizers and coverage require GCC or Clang")
    endif()
endif()

if(RKVC_ENABLE_ASAN)
    target_compile_options(rkvc_instrumentation INTERFACE
        -fsanitize=address
        -fno-omit-frame-pointer
    )
    target_link_options(rkvc_instrumentation INTERFACE
        -fsanitize=address
        -fno-omit-frame-pointer
    )
endif()

if(RKVC_ENABLE_UBSAN)
    target_compile_options(rkvc_instrumentation INTERFACE
        -fsanitize=undefined
        -fno-omit-frame-pointer
    )
    target_link_options(rkvc_instrumentation INTERFACE
        -fsanitize=undefined
        -fno-omit-frame-pointer
    )
endif()

if(RKVC_ENABLE_COVERAGE)
    target_compile_options(rkvc_instrumentation INTERFACE
        -O0
        -g
        --coverage
    )
    target_link_options(rkvc_instrumentation INTERFACE
        --coverage
    )
endif()

if(RKVC_ENABLE_FAULT_INJECTION)
    target_compile_definitions(rkvc_instrumentation INTERFACE
        RKVC_ENABLE_FAULT_INJECTION=1
    )
endif()

# ── 库目标 ─────────────────────────────────────────────────────────────
if(RKVC_BUILD_SHARED)
    add_library(rkvc_shared SHARED ${RKVC_SOURCES})
    target_include_directories(rkvc_shared
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include>
        PRIVATE
            ${AVCODEC_INCLUDE_DIRS}
            ${MPP_INCLUDE_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/lib
    )
    target_link_directories(rkvc_shared PRIVATE ${AVCODEC_LIB_DIRS} ${MPP_LIB_DIR})
    target_link_libraries(rkvc_shared PRIVATE rkvc_instrumentation ${FFMPEG_LIBS} ${EXTRA_LIBS})
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
            ${MPP_INCLUDE_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/lib
    )
    target_link_directories(rkvc_static PUBLIC ${AVCODEC_LIB_DIRS} ${MPP_LIB_DIR})
    target_link_libraries(rkvc_static PUBLIC rkvc_instrumentation ${FFMPEG_LIBS} ${EXTRA_LIBS})
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

    foreach(EXAMPLE encode_file decode_file stream_encode stream_decode stream_transcode stream_device_pair transcode latency_test psnr_test)
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

    # psnr_test 使用 math 库 (log10)
    if(TARGET example_psnr_test)
        target_link_libraries(example_psnr_test PRIVATE m)
    endif()
endif()

# ── CLI 工具 (安装到 bin/) ────────────────────────────────────────────
if(RKVC_BUILD_TOOLS)
    set(RKVC_TOOL_SOURCES
        tools/rkvc_bench.c
        tools/rkvc_encode.c
        tools/rkvc_decode.c
        tools/rkvc_info.c
    )

    set(RKVC_TOOLS "")
    foreach(TOOL_SRC ${RKVC_TOOL_SOURCES})
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${TOOL_SRC}")
            get_filename_component(TOOL_NAME ${TOOL_SRC} NAME_WE)
            add_executable(${TOOL_NAME} ${TOOL_SRC})
            target_link_libraries(${TOOL_NAME} PRIVATE rkvc)
            target_include_directories(${TOOL_NAME}
                PRIVATE ${AVCODEC_INCLUDE_DIRS})
            target_link_directories(${TOOL_NAME}
                PRIVATE ${AVCODEC_LIB_DIRS})
            # RPATH: 安装后从 bin/ 找 ../lib/ 中的 .so
            set_target_properties(${TOOL_NAME} PROPERTIES
                INSTALL_RPATH "$ORIGIN/../lib"
                BUILD_RPATH   "${CMAKE_CURRENT_BINARY_DIR};${AVCODEC_LIB_DIRS}"
            )
            list(APPEND RKVC_TOOLS ${TOOL_NAME})
        endif()
    endforeach()
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

    # 安装 CLI 工具到 bin/
    if(RKVC_TOOLS)
        install(TARGETS ${RKVC_TOOLS}
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
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
    enable_testing()

    set(RKVC_TEST_SOURCES
        tests/test_types.c
        tests/test_frame.c
        tests/test_contracts.c
        tests/test_internal.c
        tests/test_hardware.c
        tests/test_scale.c
    )

    if(RKVC_ENABLE_FAULT_INJECTION)
        list(APPEND RKVC_TEST_SOURCES tests/test_fault_injection.c)
    endif()

    set(RKVC_TEST_TARGETS "")

    foreach(TEST_SRC ${RKVC_TEST_SOURCES})
        get_filename_component(TEST_NAME ${TEST_SRC} NAME_WE)
        add_executable(${TEST_NAME} ${TEST_SRC})
        target_link_libraries(${TEST_NAME} PRIVATE rkvc_static rkvc_instrumentation ${CMOCKA_LIBRARIES})
        target_include_directories(${TEST_NAME}
            PRIVATE
                ${CMAKE_CURRENT_SOURCE_DIR}/include
                ${CMAKE_CURRENT_SOURCE_DIR}/lib
                ${CMOCKA_INCLUDE_DIRS}
                ${AVCODEC_INCLUDE_DIRS}
        )
        target_link_directories(${TEST_NAME}
            PRIVATE
                ${AVCODEC_LIB_DIRS}
                ${CMOCKA_LIBRARY_DIRS}
        )
        add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
        set_tests_properties(${TEST_NAME} PROPERTIES LABELS unit)
        if(TEST_NAME STREQUAL "test_hardware")
            set_tests_properties(${TEST_NAME} PROPERTIES
                LABELS integration
                SKIP_RETURN_CODE 77
            )
        endif()
        if(RKVC_ENABLE_ASAN OR RKVC_ENABLE_UBSAN)
            set_tests_properties(${TEST_NAME} PROPERTIES
                ENVIRONMENT
                    "ASAN_OPTIONS=detect_leaks=0:halt_on_error=1:abort_on_error=1;UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1"
            )
        endif()
        list(APPEND RKVC_TEST_TARGETS ${TEST_NAME})
    endforeach()

    add_custom_target(check
        COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
        DEPENDS ${RKVC_TEST_TARGETS}
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        COMMENT "Building and running rkvc unit tests"
        VERBATIM
    )
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

# ── CPack 打包 ───────────────────────────────────────────────────────
set(CPACK_PACKAGE_NAME "rkvc")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "RK3588 H.265 Video Codec Library (RKMPP)")
set(CPACK_PACKAGE_CONTACT "rkvc@example.com")
set(CPACK_PACKAGE_VENDOR "rkvc")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")

# DEB 包配置
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libcmocka-dev")
set(CPACK_DEBIAN_PACKAGE_SECTION "libs")
set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "arm64")
set(CPACK_DEBIAN_FILE_NAME "rkvc_${PROJECT_VERSION}_arm64.deb")

# TGZ 配置
set(CPACK_GENERATOR "TGZ;DEB")

# 输出目录
set(CPACK_PACKAGE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/packages")

include(CPack)
