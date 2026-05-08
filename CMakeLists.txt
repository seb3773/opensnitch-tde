cmake_minimum_required(VERSION 3.10)
project(OpenSnitchTQt3 C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CXX_EXTENSIONS ON)

set(CMAKE_CXX_FLAGS "-O2 -flto=auto -fstrict-aliasing -ffunction-sections -fdata-sections -fno-ident -fno-plt -fomit-frame-pointer -fno-stack-protector -fno-math-errno -fvisibility=hidden -fno-threadsafe-statics -fno-exceptions -fno-unwind-tables -fno-asynchronous-unwind-tables -Wno-deprecated-declarations -DQT_SHARED -DQT_TABLET_SUPPORT -DTQT_NO_DEBUG -DQT_NO_EXCEPTIONS -DTQT_THREAD_SUPPORT -D_REENTRANT -fpermissive -std=gnu++17")
set(CMAKE_EXE_LINKER_FLAGS "-Wl,-O1 -Wl,--gc-sections -Wl,--as-needed -s")

# Ensure no per-config C++ flags are appended by CMake.
set(CMAKE_CXX_FLAGS_DEBUG "")
set(CMAKE_CXX_FLAGS_RELEASE "")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "")
set(CMAKE_CXX_FLAGS_MINSIZEREL "")

find_package(PkgConfig REQUIRED)
pkg_check_modules(GRPC REQUIRED grpc++)
pkg_check_modules(PROTOBUF REQUIRED protobuf)
pkg_check_modules(TQT REQUIRED tqt-mt)
pkg_check_modules(SQLITE3 REQUIRED sqlite3)

find_program(PROTOC_COMPILER protoc REQUIRED)
find_program(GRPC_CPP_PLUGIN grpc_cpp_plugin REQUIRED)
find_program(TQMOC_EXECUTABLE NAMES tqmoc moc-tqt moc REQUIRED)
find_program(SSSTRIP_EXECUTABLE NAMES sstrip)

# --- Protobuf/gRPC generation ---
set(PROTO_DIR "${CMAKE_SOURCE_DIR}/proto")
set(PROTO_GEN_DIR "${CMAKE_BINARY_DIR}/grpc_gen")
file(MAKE_DIRECTORY ${PROTO_GEN_DIR})

set(PROTO_SRC "${PROTO_GEN_DIR}/ui.pb.cc")
set(PROTO_HDR "${PROTO_GEN_DIR}/ui.pb.h")
set(GRPC_SRC "${PROTO_GEN_DIR}/ui.grpc.pb.cc")
set(GRPC_HDR "${PROTO_GEN_DIR}/ui.grpc.pb.h")

add_custom_command(
    OUTPUT "${PROTO_SRC}" "${PROTO_HDR}" "${GRPC_SRC}" "${GRPC_HDR}"
    COMMAND ${PROTOC_COMPILER}
    ARGS --cpp_out="${PROTO_GEN_DIR}"
         --grpc_out="${PROTO_GEN_DIR}"
         --plugin=protoc-gen-grpc="${GRPC_CPP_PLUGIN}"
         -I "${PROTO_DIR}"
         "${PROTO_DIR}/ui.proto"
    DEPENDS "${PROTO_DIR}/ui.proto"
)

# --- TQt3 / TDE paths ---
set(TQT_INCLUDE_DIRS "/usr/include/tqt3" "/usr/include/tqt")
set(TDE_INCLUDE_DIRS "/opt/trinity/include")
set(TDE_LIB_DIRS "/opt/trinity/lib" "/opt/trinity/lib64")
set(TQT_LIBRARIES tqt-mt)
set(TDE_LIBRARIES tdecore tdeui tdeio)

# --- MOC generation ---
# Only MOC headers that contain TQ_OBJECT (skip tdeext and non-TQObject headers)
set(MOC_HEADERS
    ${CMAKE_SOURCE_DIR}/src/nodes.h
    ${CMAKE_SOURCE_DIR}/src/rules.h
    ${CMAKE_SOURCE_DIR}/src/systray.h
    ${CMAKE_SOURCE_DIR}/src/prompt_dialog.h
    ${CMAKE_SOURCE_DIR}/src/process_details_dialog.h
    ${CMAKE_SOURCE_DIR}/src/net_identity_resolver.h
    ${CMAKE_SOURCE_DIR}/src/preferences_dialog.h
    ${CMAKE_SOURCE_DIR}/src/rule_dialog.h
    ${CMAKE_SOURCE_DIR}/src/ui_controller.h
    ${CMAKE_SOURCE_DIR}/src/main_window.h
    ${CMAKE_SOURCE_DIR}/src/mvc/tqtabstractlistmodel.h
    ${CMAKE_SOURCE_DIR}/src/mvc/tqtliststore.h
    ${CMAKE_SOURCE_DIR}/src/mvc/tqtmvctableview.h
)
set(MOC_OUTPUTS "")
foreach(HDR ${MOC_HEADERS})
    get_filename_component(BASE ${HDR} NAME_WE)
    set(MOC_OUT "${CMAKE_CURRENT_BINARY_DIR}/moc_${BASE}.cpp")
    add_custom_command(
        OUTPUT ${MOC_OUT}
        COMMAND ${TQMOC_EXECUTABLE} ${HDR} -o ${MOC_OUT}
        DEPENDS ${HDR}
    )
    list(APPEND MOC_OUTPUTS ${MOC_OUT})
endforeach()

# --- Source files ---
file(GLOB SOURCES "src/*.cpp")
list(APPEND SOURCES "src/net_identity_resolver.cpp")
# tde-extended mini-lib sources (vendored)
file(GLOB TDEEXT_SOURCES "src/tdeext/*.cpp" "src/tdeext/*.c")
# MVC list view library sources
file(GLOB MVC_SOURCES "src/mvc/*.cpp")

# --- Embedded icons generation ---
file(GLOB ICON_PNGS "${CMAKE_SOURCE_DIR}/icons/*.png")
set(EMBEDDED_ICONS_HDR "${CMAKE_SOURCE_DIR}/src/embedded_icons.h")
set(GEN_EMBEDDED_ICONS_SH "${CMAKE_SOURCE_DIR}/scripts/gen_embedded_icons.sh")

add_custom_command(
    OUTPUT "${EMBEDDED_ICONS_HDR}"
    COMMAND /usr/bin/env bash "${GEN_EMBEDDED_ICONS_SH}"
    DEPENDS ${ICON_PNGS} "${GEN_EMBEDDED_ICONS_SH}"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    VERBATIM
)

add_custom_target(embedded_icons DEPENDS "${EMBEDDED_ICONS_HDR}")

include_directories(
    ${TQT_INCLUDE_DIRS}
    ${TDE_INCLUDE_DIRS}
    ${PROTO_GEN_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/tdeext
    ${CMAKE_SOURCE_DIR}/src/mvc
    ${SQLITE3_INCLUDE_DIRS}
)

link_directories(
    ${TDE_LIB_DIRS}
)

add_executable(opensnitch-tde ${SOURCES} ${TDEEXT_SOURCES} ${MVC_SOURCES} ${PROTO_SRC} ${GRPC_SRC} ${MOC_OUTPUTS})
add_dependencies(opensnitch-tde embedded_icons)

if (SSSTRIP_EXECUTABLE)
    add_custom_command(TARGET opensnitch-tde POST_BUILD
        COMMAND ${SSSTRIP_EXECUTABLE} $<TARGET_FILE:opensnitch-tde>
        VERBATIM
    )
endif()
target_link_libraries(opensnitch-tde
    ${TQT_LIBRARIES}
    ${TDE_LIBRARIES}
    ${GRPC_LIBRARIES}
    ${PROTOBUF_LIBRARIES}
    ${SQLITE3_LIBRARIES}
    dl
)
