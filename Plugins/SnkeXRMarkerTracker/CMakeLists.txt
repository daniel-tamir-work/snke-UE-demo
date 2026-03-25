cmake_minimum_required(VERSION 3.21)
cmake_policy(SET CMP0077 NEW)
project(marker_tracker_client)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# Host-tools-only build (protoc, grpc_cpp_plugin) for cross-compilation
# ---------------------------------------------------------------------------
get_filename_component(_BUILD_DIR_NAME "${CMAKE_BINARY_DIR}" NAME)
if(_BUILD_DIR_NAME MATCHES "host-tools")
  message(STATUS "Building host tools only (protoc, grpc_cpp_plugin)")

  include(FetchContent)
  set(gRPC_BUILD_TESTS       OFF CACHE BOOL "" FORCE)
  set(gRPC_INSTALL           OFF CACHE BOOL "" FORCE)
  set(gRPC_ABSL_PROVIDER     "module" CACHE STRING "" FORCE)
  set(gRPC_CARES_PROVIDER    "module" CACHE STRING "" FORCE)
  set(gRPC_ZLIB_PROVIDER     "module" CACHE STRING "" FORCE)
  set(gRPC_RE2_PROVIDER      "module" CACHE STRING "" FORCE)
  set(gRPC_SSL_PROVIDER      "module" CACHE STRING "" FORCE)
  set(gRPC_PROTOBUF_PROVIDER "module" CACHE STRING "" FORCE)
  set(ABSL_ENABLE_INSTALL         OFF CACHE BOOL "" FORCE)
  set(ABSL_PROPAGATE_CXX_STD      ON  CACHE BOOL "" FORCE)
  set(ABSL_USE_EXTERNAL_GOOGLETEST OFF CACHE BOOL "" FORCE)
  set(RE2_BUILD_TESTING            OFF CACHE BOOL "" FORCE)
  set(RE2_INSTALL                  OFF CACHE BOOL "" FORCE)
  set(protobuf_BUILD_TESTS         OFF CACHE BOOL "" FORCE)
  set(protobuf_INSTALL             OFF CACHE BOOL "" FORCE)
  set(protobuf_BUILD_PROTOC_BINARIES ON CACHE BOOL "" FORCE)

  FetchContent_Declare(
    grpc
    GIT_REPOSITORY https://github.com/grpc/grpc.git
    GIT_TAG        v1.65.5
  )
  FetchContent_MakeAvailable(grpc)
  return()
endif()

if(NOT ANDROID)
  message(FATAL_ERROR "This project is for Android cross-compilation only.")
endif()

# Build all third-party deps as static libraries
set(BUILD_SHARED_LIBS OFF)

# ---------------------------------------------------------------------------
# Proto file list (relative to CMAKE_SOURCE_DIR → unreal/SnkeXRMarkerTracker)
# ---------------------------------------------------------------------------
set(PROTO_FILES
  ../../../../api/common/v1/errors.proto
  ../../../../api/marker_tracker/v1/common.proto
  ../../../../api/marker_tracker/v1/marker_tracker_service.proto
)

set(GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")

# ---------------------------------------------------------------------------
# gRPC / Protobuf – vendored, all static
# ---------------------------------------------------------------------------
include(FetchContent)

set(gRPC_BUILD_TESTS       OFF CACHE BOOL "" FORCE)
set(gRPC_INSTALL           OFF CACHE BOOL "" FORCE)
set(gRPC_ABSL_PROVIDER     "module" CACHE STRING "" FORCE)
set(gRPC_CARES_PROVIDER    "module" CACHE STRING "" FORCE)
set(gRPC_ZLIB_PROVIDER     "module" CACHE STRING "" FORCE)
set(gRPC_RE2_PROVIDER      "module" CACHE STRING "" FORCE)
set(gRPC_SSL_PROVIDER      "module" CACHE STRING "" FORCE)
set(gRPC_PROTOBUF_PROVIDER "module" CACHE STRING "" FORCE)

set(ABSL_ENABLE_INSTALL         OFF CACHE BOOL "" FORCE)
set(ABSL_PROPAGATE_CXX_STD      ON  CACHE BOOL "" FORCE)
set(ABSL_USE_EXTERNAL_GOOGLETEST OFF CACHE BOOL "" FORCE)
set(RE2_BUILD_TESTING            OFF CACHE BOOL "" FORCE)
set(RE2_INSTALL                  OFF CACHE BOOL "" FORCE)
set(protobuf_BUILD_TESTS         OFF CACHE BOOL "" FORCE)
set(protobuf_INSTALL             OFF CACHE BOOL "" FORCE)

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
  grpc
  GIT_REPOSITORY https://github.com/grpc/grpc.git
  GIT_TAG        v1.65.5
)

FetchContent_GetProperties(grpc)
if(NOT grpc_POPULATED)
  FetchContent_Populate(grpc)
  set(BUILD_SHARED_LIBS OFF)
  add_subdirectory(${grpc_SOURCE_DIR} ${grpc_BINARY_DIR} EXCLUDE_FROM_ALL)

  if(TARGET zlib)
    set_target_properties(zlib PROPERTIES EXCLUDE_FROM_ALL TRUE)
  endif()

  # Android libc already includes pthreads; strip explicit -lpthread references
  get_property(_all_targets DIRECTORY ${grpc_SOURCE_DIR} PROPERTY BUILDSYSTEM_TARGETS)
  foreach(_t ${_all_targets})
    if(TARGET ${_t})
      get_target_property(_ll ${_t} LINK_LIBRARIES)
      if(_ll)
        list(REMOVE_ITEM _ll pthread)
        set_target_properties(${_t} PROPERTIES LINK_LIBRARIES "${_ll}")
      endif()
      get_target_property(_ill ${_t} INTERFACE_LINK_LIBRARIES)
      if(_ill)
        list(REMOVE_ITEM _ill pthread)
        set_target_properties(${_t} PROPERTIES INTERFACE_LINK_LIBRARIES "${_ill}")
      endif()
    endif()
  endforeach()
endif()

# Resolve whichever grpc++ target is available
if(TARGET gRPC::grpc++_unsecure)
  set(GRPCPP_TARGET gRPC::grpc++_unsecure)
elseif(TARGET grpc++_unsecure)
  set(GRPCPP_TARGET grpc++_unsecure)
elseif(TARGET gRPC::grpc++)
  set(GRPCPP_TARGET gRPC::grpc++)
elseif(TARGET grpc++)
  set(GRPCPP_TARGET grpc++)
else()
  message(FATAL_ERROR "Could not find a gRPC++ target after FetchContent.")
endif()

message(STATUS "Using pre-generated proto files from ${GENERATED_DIR}")

# ---------------------------------------------------------------------------
# Expected generated proto sources (populated by build.sh before cmake runs)
# ---------------------------------------------------------------------------
set(PROTO_SRCS)
set(PROTO_HDRS)
set(GRPC_SRCS)
set(GRPC_HDRS)

file(MAKE_DIRECTORY "${GENERATED_DIR}")

foreach(PROTO_FILE ${PROTO_FILES})
  get_filename_component(PROTO_NAME   ${PROTO_FILE} NAME_WE)
  get_filename_component(PROTO_FPATH  "${CMAKE_SOURCE_DIR}/${PROTO_FILE}" ABSOLUTE)
  get_filename_component(PROTO_DIR    ${PROTO_FPATH} PATH)
  string(REGEX REPLACE "^.*api/" "" PROTO_REL "${PROTO_DIR}")

  file(MAKE_DIRECTORY "${GENERATED_DIR}/${PROTO_REL}")

  list(APPEND PROTO_SRCS "${GENERATED_DIR}/${PROTO_REL}/${PROTO_NAME}.pb.cc")
  list(APPEND PROTO_HDRS "${GENERATED_DIR}/${PROTO_REL}/${PROTO_NAME}.pb.h")
  list(APPEND GRPC_SRCS  "${GENERATED_DIR}/${PROTO_REL}/${PROTO_NAME}.grpc.pb.cc")
  list(APPEND GRPC_HDRS  "${GENERATED_DIR}/${PROTO_REL}/${PROTO_NAME}.grpc.pb.h")
endforeach()

# ---------------------------------------------------------------------------
# Shared library target
# ---------------------------------------------------------------------------
add_library(marker_tracker_client SHARED
  ../../src/lib_marker_tracker_client.cpp
  ${PROTO_SRCS}
  ${GRPC_SRCS}
)

target_include_directories(marker_tracker_client PRIVATE
  ../../include
  ${GENERATED_DIR}
  $<TARGET_PROPERTY:protobuf::libprotobuf,INTERFACE_INCLUDE_DIRECTORIES>
)

# Android system libraries
find_library(ANDROID_LOG_LIB log)
find_library(ANDROID_LIB     android)
find_library(ATOMIC_LIB      atomic)

target_link_libraries(marker_tracker_client PRIVATE
  ${GRPCPP_TARGET}
  protobuf::libprotobuf
  ${ANDROID_LOG_LIB}
  ${ANDROID_LIB}
  $<$<BOOL:${ATOMIC_LIB}>:${ATOMIC_LIB}>
  dl
  m
)

# Strip unneeded symbols to keep the .so small; export only the C API
set_target_properties(marker_tracker_client PROPERTIES
  OUTPUT_NAME              "marker_tracker_client"
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/Unreal Engine/SnkeXRMarkerTracker/Lib/Android/arm64-v8a"
  POSITION_INDEPENDENT_CODE ON
  CXX_VISIBILITY_PRESET    hidden
  C_VISIBILITY_PRESET      hidden
)

add_dependencies(marker_tracker_client ${GRPCPP_TARGET} protobuf::libprotobuf)
