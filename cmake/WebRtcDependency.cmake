set(BUPT_VOLUMETRIC_WEBRTC_ROOT
    "${CMAKE_CURRENT_LIST_DIR}/../../extern/webrtc/src"
    CACHE PATH "Pinned WebRTC source checkout")
set(BUPT_VOLUMETRIC_WEBRTC_OUT
    "${BUPT_VOLUMETRIC_WEBRTC_ROOT}/out/Default"
    CACHE PATH "Pinned WebRTC build output")

function(bupt_volumetric_apply_webrtc_abi)
  if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang" OR NOT MSVC)
    message(FATAL_ERROR "Pinned WebRTC requires clang-cl")
  endif()
  if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Pinned WebRTC requires a 64-bit target")
  endif()
  set(_generated "${BUPT_VOLUMETRIC_WEBRTC_OUT}/gen")
  set(_clang_builtins
      "${BUPT_VOLUMETRIC_WEBRTC_ROOT}/third_party/llvm-build/Release+Asserts/lib/clang/23/lib/windows/clang_rt.builtins-x86_64.lib")
  file(GLOB _libcxx_objects
       "${BUPT_VOLUMETRIC_WEBRTC_OUT}/obj/buildtools/third_party/libc++/libc++/*.obj")
  if(NOT _libcxx_objects)
    message(FATAL_ERROR "Missing Chromium libc++ objects")
  endif()
  foreach(_required "${BUPT_VOLUMETRIC_WEBRTC_OUT}/args.gn" "${_clang_builtins}")
    if(NOT EXISTS "${_required}")
      message(FATAL_ERROR "Missing pinned WebRTC ABI artifact: ${_required}")
    endif()
  endforeach()
  include_directories(
    "${BUPT_VOLUMETRIC_WEBRTC_ROOT}" "${_generated}"
    "${BUPT_VOLUMETRIC_WEBRTC_ROOT}/buildtools/third_party/libc++"
    "${BUPT_VOLUMETRIC_WEBRTC_ROOT}/third_party/abseil-cpp"
    "${BUPT_VOLUMETRIC_WEBRTC_ROOT}/third_party/libc++/src/include")
  add_compile_definitions(
    _HAS_NODISCARD _LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_EXTENSIVE
    _LIBCPP_DISABLE_VISIBILITY_ANNOTATIONS _LIBCPP_INSTRUMENTED_WITH_ASAN=0
    WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE NDEBUG CHROMIUM)
  add_compile_options(/MT /GR- /bigobj /utf-8 /Zc:twoPhase /Zc:inline -m64)
  add_link_options(/DEFAULTLIB:libcpmt.lib /INCREMENTAL:NO)
  add_library(bupt_volumetric::libcxx INTERFACE IMPORTED GLOBAL)
  target_link_libraries(bupt_volumetric::libcxx INTERFACE
    ${_libcxx_objects} "${_clang_builtins}")
endfunction()

# Full WebRTC compile definitions + include dirs for targets that include
# WebRTC headers (the rtc library and its tests). Kept target-scoped so the
# pure-algorithm preset (runtime OFF) never pays for them. The values mirror
# bupt_pc's proven WebRTC ABI.
function(bupt_volumetric_apply_webrtc_runtime_abi target)
  target_compile_definitions(${target} PRIVATE
    _HAS_NODISCARD
    _CRT_NONSTDC_NO_WARNINGS
    _WINSOCK_DEPRECATED_NO_WARNINGS
    _LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_EXTENSIVE
    _LIBCPP_DISABLE_VISIBILITY_ANNOTATIONS
    _LIBCPP_INSTRUMENTED_WITH_ASAN=0
    __STD_C
    _CRT_RAND_S
    _CRT_SECURE_NO_DEPRECATE
    _SCL_SECURE_NO_DEPRECATE
    _WINDOWS
    WIN32
    WINAPI_FAMILY=WINAPI_FAMILY_DESKTOP_APP
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _UNICODE
    UNICODE
    NTDDI_VERSION=0x0A00000C
    _WIN32_WINNT=0x0A00
    WINVER=0x0A00
    NDEBUG
    NVALGRIND
    DYNAMIC_ANNOTATIONS_ENABLED=0
    WEBRTC_ENABLE_PROTOBUF=1
    WEBRTC_STRICT_FIELD_TRIALS=0
    WEBRTC_INCLUDE_INTERNAL_AUDIO_DEVICE
    RTC_ENABLE_VP9
    RTC_DAV1D_IN_INTERNAL_DECODER_FACTORY
    WEBRTC_HAVE_SCTP
    WEBRTC_ENCODER_PSNR_STATS
    WEBRTC_ENABLE_AVX2
    WEBRTC_DEPRECATE_PLAN_B
    WEBRTC_NON_STATIC_TRACE_EVENT_HANDLERS=1
    WEBRTC_WIN
    ABSL_ALLOCATOR_NOTHROW=1
    ABSL_FLAGS_STRIP_NAMES=0
    CHROMIUM
    LIBYUV_DISABLE_NEON
    LIBYUV_DISABLE_SVE
    LIBYUV_DISABLE_SME
    LIBYUV_DISABLE_LSX
    LIBYUV_DISABLE_LASX
    HAVE_WEBRTC_VIDEO
    UNSAFE_BUFFERS_BUILD)
  target_include_directories(${target} PRIVATE
    "${BUPT_VOLUMETRIC_WEBRTC_ROOT}/third_party/perfetto/include"
    "${BUPT_VOLUMETRIC_WEBRTC_OUT}/gen/third_party/perfetto/build_config"
    "${BUPT_VOLUMETRIC_WEBRTC_OUT}/gen/third_party/perfetto"
    "${BUPT_VOLUMETRIC_WEBRTC_ROOT}/third_party/libyuv/include"
    "${BUPT_VOLUMETRIC_WEBRTC_ROOT}/third_party/jsoncpp/source/include")
endfunction()

# Imports webrtc.lib + Windows system libraries, gated on a provenance check
# that the archive was rebuilt after the Task 8 patch (so we never silently link
# the pre-patch baseline library).
function(bupt_volumetric_import_webrtc)
  if(TARGET bupt_volumetric::webrtc)
    return()
  endif()

  set(_library "${BUPT_VOLUMETRIC_WEBRTC_OUT}/obj/webrtc.lib")
  if(NOT EXISTS "${_library}" OR IS_DIRECTORY "${_library}")
    message(FATAL_ERROR "Missing pinned WebRTC archive: ${_library}")
  endif()

  set(_patched_sources
    "${BUPT_VOLUMETRIC_WEBRTC_ROOT}/modules/rtp_rtcp/source/ulpfec_generator.cc"
    "${BUPT_VOLUMETRIC_WEBRTC_ROOT}/modules/rtp_rtcp/source/video_fec_generator.h"
    "${BUPT_VOLUMETRIC_WEBRTC_ROOT}/modules/rtp_rtcp/source/ulpfec_generator.h"
    "${BUPT_VOLUMETRIC_WEBRTC_ROOT}/video/rtp_video_stream_receiver2.cc"
    "${BUPT_VOLUMETRIC_WEBRTC_ROOT}/video/rtp_video_stream_receiver2.h")
  file(TIMESTAMP "${_library}" _lib_epoch "%s" UTC)
  foreach(_src ${_patched_sources})
    file(TIMESTAMP "${_src}" _src_epoch "%s" UTC)
    if(_src_epoch GREATER _lib_epoch)
      message(FATAL_ERROR
        "webrtc.lib predates the Task 8 patch (${_src}). Rebuild it with:\n"
        "  ninja -C ${BUPT_VOLUMETRIC_WEBRTC_OUT} webrtc\n"
        "  (with DEPOT_TOOLS_WIN_TOOLCHAIN=0)")
    endif()
  endforeach()

  add_library(bupt_volumetric::webrtc STATIC IMPORTED GLOBAL)
  set_target_properties(bupt_volumetric::webrtc PROPERTIES
    IMPORTED_LOCATION "${_library}" SYSTEM FALSE)
  target_link_libraries(bupt_volumetric::webrtc INTERFACE
    bupt_volumetric::libcxx
    advapi32 comdlg32 dbghelp dnsapi gdi32 msimg32 odbc32 odbccp32
    oleaut32 shell32 shlwapi user32 usp10 uuid version wininet winmm
    winspool ws2_32 delayimp kernel32 ole32 crypt32 iphlpapi secur32
    dmoguids wmcodecdspuuid amstrmid msdmo strmiids bcrypt)
endfunction()
