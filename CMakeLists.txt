cmake_minimum_required(VERSION 3.22)

project(MobileDumper-7 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(DISABLED_WARNINGS
    -Wno-unused-parameter
    -Wno-unused-function
    -Wno-missing-field-initializers
    -Wno-missing-designated-field-initializers
)

if(NOT DEFINED DUMPER_ROOT)
    set(DUMPER_ROOT "${CMAKE_SOURCE_DIR}")
endif()

option(DUMPER_BUILD_EXECUTABLE "Build as a standalone executable instead of a shared library" OFF)

# ── Platform detection ────────────────────────────────────────────────────────

if(CMAKE_SYSTEM_NAME STREQUAL "Android" OR ANDROID)
    set(PLATFORM_ANDROID TRUE)
elseif(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set(PLATFORM_IOS TRUE)
    enable_language(OBJCXX)
else()
    message(FATAL_ERROR "Unsupported platform: '${CMAKE_SYSTEM_NAME}'. Set -DCMAKE_SYSTEM_NAME=Android or iOS.")
endif()

# ── Third-party ───────────────────────────────────────────────────────────────

set(FMT_PATH "${DUMPER_ROOT}/libs/fmt")

if(PLATFORM_ANDROID)
    set(KITTYMEM_PATH "${DUMPER_ROOT}/libs/KittyMemoryEx/KittyMemoryEx")
elseif(PLATFORM_IOS)
    set(KITTYMEM_PATH "${DUMPER_ROOT}/libs/KittyMemory/KittyMemory")
endif()

file(GLOB KITTYMEM_SRC "${KITTYMEM_PATH}/*.cpp")

# ── Dumper sources ────────────────────────────────────────────────────────────

file(GLOB_RECURSE DUMPER_SRC
    "${DUMPER_ROOT}/Dumper/*.cpp"
    "${DUMPER_ROOT}/Dumper/*.mm"
    "${DUMPER_ROOT}/Dumper/*.c"
)

if(PLATFORM_ANDROID)
    list(FILTER DUMPER_SRC EXCLUDE REGEX ".*MemoryiOS.*")
    list(FILTER DUMPER_SRC EXCLUDE REGEX ".*ProfileiOS.*")
    list(FILTER DUMPER_SRC EXCLUDE REGEX ".*/main_ios\\.mm$")
    list(FILTER DUMPER_SRC EXCLUDE REGEX ".*/UI/iOS/.*")
elseif(PLATFORM_IOS)
    list(FILTER DUMPER_SRC EXCLUDE REGEX ".*MemoryAndroid.*")
    list(FILTER DUMPER_SRC EXCLUDE REGEX ".*ProfileAndroid.*")
    list(FILTER DUMPER_SRC EXCLUDE REGEX ".*/main_android\\.cpp$")
endif()

# ── Target ────────────────────────────────────────────────────────────────────

if(DUMPER_BUILD_EXECUTABLE)
    add_executable(MobileDumper-7 ${DUMPER_SRC} ${KITTYMEM_SRC})
else()
    add_library(MobileDumper-7 SHARED ${DUMPER_SRC} ${KITTYMEM_SRC})
endif()

target_include_directories(MobileDumper-7 PRIVATE
    # KittyMemory
    ${KITTYMEM_PATH}/..

    # fmt header-only
    ${FMT_PATH}/include
)

target_compile_definitions(MobileDumper-7 PRIVATE
    FMT_HEADER_ONLY
    kNO_KEYSTONE
    $<$<BOOL:${DUMPER_BUILD_EXECUTABLE}>:DUMPER_BUILD_EXECUTABLE>
    $<$<CONFIG:Debug>:DEBUG>
    $<$<CONFIG:Release>:NDEBUG>
)

target_compile_options(MobileDumper-7 PRIVATE
    -fexceptions
    ${DISABLED_WARNINGS}
    -fvisibility=hidden
    -fvisibility-inlines-hidden
    $<$<BOOL:${PLATFORM_IOS}>:-fobjc-arc>
    $<$<CONFIG:Debug>:-O0 -g3 -fno-omit-frame-pointer>
    $<$<CONFIG:Release>:-O3 -g0 -fomit-frame-pointer>
)

if(PLATFORM_ANDROID)
    target_link_options(MobileDumper-7 PRIVATE
        $<$<CONFIG:Release>:-s>
    )
endif()

# Apple's linker deprecated accepting -s directly ("-s is obsolete"); strip
# symbols from the built dylib as a separate POST_BUILD step instead.
if(PLATFORM_IOS AND CMAKE_BUILD_TYPE STREQUAL "Release")
    add_custom_command(TARGET MobileDumper-7 POST_BUILD
        COMMAND xcrun strip -x $<TARGET_FILE:MobileDumper-7>
        VERBATIM
    )
endif()

if(PLATFORM_ANDROID)
    target_link_libraries(MobileDumper-7 PRIVATE android log)
elseif(PLATFORM_IOS)
    target_link_libraries(MobileDumper-7 PRIVATE "-framework Foundation -framework UIKit -framework CoreGraphics -framework QuartzCore")
    set_target_properties(MobileDumper-7 PROPERTIES
        XCODE_ATTRIBUTE_IPHONEOS_DEPLOYMENT_TARGET "14.0"
    )
endif()
