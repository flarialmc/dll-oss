cmake_minimum_required(VERSION 3.24.0)
project(Flarial)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")

get_property(IS_MULTI_CONFIG GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if (IS_MULTI_CONFIG)
    set(CMAKE_CONFIGURATION_TYPES "Debug;Release;Test" CACHE STRING "Supported build configurations" FORCE)
else()
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release Test)
    if (NOT CMAKE_BUILD_TYPE)
        set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
    endif()
endif()

set(CLIENT_VERSION "2.00") # Update this for every new release

set(FLARIAL_BUILD_TYPE_EXPR "$<IF:$<CONFIG:Debug>,Debug,$<IF:$<CONFIG:Test>,Test,Release>>")
add_compile_definitions(
        $<$<CONFIG:Debug>:__DEBUG__>
        $<$<CONFIG:Test>:__TEST__>
        $<$<NOT:$<OR:$<CONFIG:Debug>,$<CONFIG:Test>>>:__RELEASE__>
)

execute_process(
        COMMAND git rev-parse --short HEAD
        OUTPUT_VARIABLE COMMIT_HASH
        OUTPUT_STRIP_TRAILING_WHITESPACE
)

add_compile_definitions(COMMIT_HASH="${COMMIT_HASH}")

# Optimize for speed
add_compile_options($<$<CONFIG:Release>:/O2>)
add_compile_options($<$<CONFIG:Release>:/Ot>)
add_compile_options($<$<CONFIG:Release>:/Ox>)
add_compile_options($<$<CONFIG:Release>:/Oy>)

# Enable intrinsic functions
add_compile_options($<$<CONFIG:Release>:/Oi>)

# Separate functions for the linker to improve the optimization process
add_compile_options($<$<CONFIG:Release>:/Gy>)

# Optimize global data to reduce the binary size
add_compile_options($<$<CONFIG:Release>:/Gw>)

# Enable string pooling to reduce binary size by consolidating duplicate strings
add_compile_options($<$<CONFIG:Release>:/GF>)

# Optimize floating point operations for speed (may introduce minor precision differences)
# add_compile_options($<$<CONFIG:Release>:/fp:fast>)

# Inline any suitable function to improve performance by reducing function call overhead
add_compile_options($<$<CONFIG:Release>:/Ob2>)

add_compile_options(/bigobj)
add_compile_options(/utf-8)
add_compile_options(/FS)
add_compile_options($<$<COMPILE_LANGUAGE:CXX>:/EHsc>)

# Preserve the intended runtime / debug-info behavior for both single-config and multi-config generators.
add_compile_options($<$<OR:$<CONFIG:Release>,$<CONFIG:Test>>:/fp:fast>)
add_compile_options($<$<OR:$<CONFIG:Release>,$<CONFIG:Test>>:/Zi>)
add_compile_options($<$<CONFIG:Debug>:/Od>)
add_compile_options($<$<CONFIG:Debug>:/Zi>)
add_link_options($<$<OR:$<CONFIG:Release>,$<CONFIG:Test>>:/DEBUG>)
add_link_options($<$<OR:$<CONFIG:Release>,$<CONFIG:Test>>:/OPT:REF>)
add_link_options($<$<OR:$<CONFIG:Release>,$<CONFIG:Test>>:/OPT:ICF>)
add_link_options(/NODEFAULTLIB:MSVCRTD)

# Generate version and build date
string(TIMESTAMP BUILD_DATE "%Y-%m-%d %H:%M:%S" UTC)
set(FLARIAL_BUILD_DATE "${BUILD_DATE}")

add_compile_definitions(
        FLARIAL_VERSION=\"${CLIENT_VERSION}\"
        FLARIAL_BUILD_TYPE=\"${FLARIAL_BUILD_TYPE_EXPR}\"
        FLARIAL_BUILD_DATE=\"${FLARIAL_BUILD_DATE}\"
)

file(GLOB_RECURSE SOURCE_FILES CONFIGURE_DEPENDS "src/*.cpp" "src/*.hpp")
file(GLOB_RECURSE LIB_FILES CONFIGURE_DEPENDS "lib/*.cpp" "lib/*.h")
add_library(Flarial SHARED main.cpp ${SOURCE_FILES} ${LIB_FILES} src/Assets/Assets.rc
        src/SDK/Client/Network/Packet/AnimatePacket.hpp
        src/Client/Module/Modules/Subtitles/Subtitles.cpp
        src/Client/Module/Modules/Subtitles/Subtitles.hpp
        src/Client/Events/Game/SoundEnginePlayEvent.hpp
        src/SDK/Client/Network/Packet/TakeItemActorPacket.hpp)

function(flarial_assign_unique_object_names target)
    if (NOT MSVC)
        return()
    endif()

    get_target_property(target_sources ${target} SOURCES)
    if (NOT target_sources)
        return()
    endif()

    set(compiled_sources)
    foreach(source IN LISTS target_sources)
        if (IS_ABSOLUTE "${source}")
            set(source_path "${source}")
        else()
            set(source_path "${CMAKE_CURRENT_SOURCE_DIR}/${source}")
        endif()

        get_filename_component(source_ext "${source_path}" EXT)
        string(TOLOWER "${source_ext}" source_ext_lower)
        if (NOT source_ext_lower STREQUAL ".c" AND
            NOT source_ext_lower STREQUAL ".cc" AND
            NOT source_ext_lower STREQUAL ".cxx" AND
            NOT source_ext_lower STREQUAL ".cpp")
            continue()
        endif()

        get_filename_component(source_name "${source_path}" NAME_WE)
        string(TOLOWER "${source_name}" source_name_lower)
        set(source_count_var "FLARIAL_SOURCE_BASENAME_COUNT_${source_name_lower}")
        if (DEFINED ${source_count_var})
            math(EXPR ${source_count_var} "${${source_count_var}} + 1")
        else()
            set(${source_count_var} 1)
        endif()

        list(APPEND compiled_sources "${source}")
    endforeach()

    foreach(source IN LISTS compiled_sources)
        if (IS_ABSOLUTE "${source}")
            set(source_path "${source}")
        else()
            set(source_path "${CMAKE_CURRENT_SOURCE_DIR}/${source}")
        endif()

        get_filename_component(source_name "${source_path}" NAME_WE)
        string(TOLOWER "${source_name}" source_name_lower)
        set(source_count_var "FLARIAL_SOURCE_BASENAME_COUNT_${source_name_lower}")
        if (NOT ${${source_count_var}} GREATER 1)
            continue()
        endif()

        file(RELATIVE_PATH rel_source_path "${CMAKE_CURRENT_SOURCE_DIR}" "${source_path}")
        get_filename_component(rel_source_dir "${rel_source_path}" DIRECTORY)
        set(unique_object_name "${rel_source_dir}_${source_name}")
        string(REGEX REPLACE "[^A-Za-z0-9_]" "_" unique_object_name "${unique_object_name}")

        # Visual Studio uses a flat object-output directory by default; duplicate
        # basenames like Manager.cpp would otherwise collide under the same .obj path.
        set_property(SOURCE "${source}" APPEND PROPERTY COMPILE_OPTIONS
                "/Fo$(IntDir)${unique_object_name}.obj")
    endforeach()
endfunction()

flarial_assign_unique_object_names(Flarial)

target_include_directories(Flarial PRIVATE
        "."
        "src"
        "src/Client"
        "src/Client/Module"
        "lib"
        "lib/lua/lua-5.4.7/include"
)

add_library(miniz STATIC lib/miniz/miniz.c)
target_include_directories(miniz PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/lib/miniz")

target_link_libraries(Flarial PRIVATE miniz)

set_target_properties(miniz PROPERTIES
        MSVC_RUNTIME_LIBRARY "MultiThreaded"
)

add_subdirectory(lib/lua)
add_subdirectory(lib/freetype)


add_library(MinHook SHARED IMPORTED GLOBAL)
set_target_properties(MinHook PROPERTIES IMPORTED_IMPLIB "${CMAKE_CURRENT_SOURCE_DIR}/lib/minhook/minhook.lib")

add_library(FreeType SHARED IMPORTED GLOBAL)
set_target_properties(FreeType PROPERTIES IMPORTED_IMPLIB "${CMAKE_CURRENT_SOURCE_DIR}/lib/freetype/libs/freetype.lib")

add_library(libcurl STATIC IMPORTED GLOBAL)
set_target_properties(libcurl PROPERTIES
        MSVC_RUNTIME_LIBRARY "MultiThreaded"
        INTERFACE_COMPILE_DEFINITIONS "CURL_STATICLIB"
        IMPORTED_LOCATION "${CMAKE_CURRENT_SOURCE_DIR}/lib/curl/libcurl.lib"
)

add_library(zlib SHARED IMPORTED GLOBAL)
set_target_properties(zlib PROPERTIES
        MSVC_RUNTIME_LIBRARY "MultiThreaded"
)
set_target_properties(zlib PROPERTIES IMPORTED_IMPLIB "${CMAKE_CURRENT_SOURCE_DIR}/lib/curl/zlib.lib")

include(FetchContent)
set(FETCHCONTENT_UPDATES_DISCONNECTED ON CACHE BOOL "" FORCE)

FetchContent_Declare(
        entt
        GIT_REPOSITORY https://github.com/skypjack/entt.git
        GIT_TAG        303801c23bf116404fc687bdc103a3a696c17d97
)

FetchContent_Declare(
        nes
        GIT_REPOSITORY https://github.com/DisabledMallis/NuvolaEventSystem.git
        GIT_TAG        main
)

FetchContent_Declare(
        libhat
        GIT_REPOSITORY https://github.com/BasedInc/libhat.git
        GIT_TAG        v0.9.0
)

FetchContent_Declare(
        fmt
        GIT_REPOSITORY https://github.com/fmtlib/fmt.git
        GIT_TAG        12.1.0
)

FetchContent_Declare(
        magic_enum
        GIT_REPOSITORY https://github.com/Neargye/magic_enum.git
        GIT_TAG        master
)

FetchContent_Declare(
        LuaBridge
        GIT_REPOSITORY https://github.com/kunitoki/LuaBridge3.git
        GIT_TAG        master
)

FetchContent_Declare(
        safetyhook
        GIT_REPOSITORY https://github.com/cursey/safetyhook.git
        GIT_TAG main
)

FetchContent_Declare(
       jsoncpp
       GIT_REPOSITORY https://github.com/EquinoxHouse/jsoncpp.git
       GIT_TAG mcbe
)

FetchContent_Declare(
        discord-rpc
        GIT_REPOSITORY https://github.com/EclipseMenu/discord-presence.git
        GIT_TAG main
)

FetchContent_Declare(
        rift
        GIT_REPOSITORY https://github.com/EclipseMenu/rift.git
        GIT_TAG v2.0.2
)

set(FMT_MODULE OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(libhat entt nes fmt magic_enum LuaBridge safetyhook jsoncpp discord-rpc rift)

target_link_libraries(Flarial PRIVATE jsoncpp safetyhook libcurl ws2_32.lib crypt32.lib normaliz.lib wldap32.lib bcrypt.lib secur32.lib iphlpapi.lib advapi32.lib LuaBridge lua_static libhat fmt::fmt EnTT::EnTT NES windowscodecs.lib urlmon.lib dwrite.lib d3d12.lib dxgi.lib d3d11.lib d2d1.lib wininet.lib setupapi.lib version FreeType magic_enum runtimeobject discord-rpc rift)

# curl 8.17 protocol-handler table is referenced only internally; without WHOLEARCHIVE,
# LTCG strips Curl_handler_http/https/ws/wss and curl_easy_perform returns CURLE_UNSUPPORTED_PROTOCOL.
target_link_options(Flarial PRIVATE "/WHOLEARCHIVE:libcurl.lib")

target_link_libraries(Flarial PUBLIC MinHook)

if (EXISTS "${CMAKE_SOURCE_DIR}/compiledoom.txt")
    message(WARNING "Compiling with Doom Module, Flarial may freeze or crash.")
    add_compile_definitions(COMPILE_DOOM)
    file(GLOB_RECURSE DOOM_SOURCES "src/Client/Module/Modules/Doom/doomgeneric/*.c" "src/Client/Module/Modules/Doom/doomgeneric/*.h")
    add_library(DoomModule ${DOOM_SOURCES}
            src/SDK/Client/Network/Packet/TakeItemActorPacket.hpp)
    target_link_libraries(Flarial PRIVATE DoomModule)
endif ()

target_precompile_headers(Flarial PRIVATE "src/PCH.hpp")

if (CMAKE_BUILD_TYPE STREQUAL "Release")
set_property(TARGET Flarial PROPERTY UNITY_BUILD ON)
set_property(TARGET Flarial PROPERTY UNITY_BUILD_BATCH_SIZE 16)
endif ()
