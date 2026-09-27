# Use the compiler and Ninja binaries from the pinned WebRTC checkout. Keeping
# these paths relative prevents a machine-local compiler from changing the ABI.
get_filename_component(_volumetric_cmake_dir "${CMAKE_CURRENT_LIST_FILE}" DIRECTORY)
get_filename_component(_volumetric_workspace "${_volumetric_cmake_dir}/../.." ABSOLUTE)
set(_volumetric_llvm_bin
    "${_volumetric_workspace}/extern/webrtc/src/third_party/llvm-build/Release+Asserts/bin")
set(CMAKE_C_COMPILER "${_volumetric_llvm_bin}/clang-cl.exe" CACHE FILEPATH "" FORCE)
set(CMAKE_CXX_COMPILER "${_volumetric_llvm_bin}/clang-cl.exe" CACHE FILEPATH "" FORCE)
set(CMAKE_MAKE_PROGRAM
    "${_volumetric_workspace}/extern/webrtc/src/third_party/ninja/ninja.exe"
    CACHE FILEPATH "" FORCE)

file(GLOB _volumetric_rc_candidates
  "C:/Program Files (x86)/Windows Kits/10/bin/*/x64/rc.exe"
  "C:/Program Files/Windows Kits/10/bin/*/x64/rc.exe")
list(SORT _volumetric_rc_candidates COMPARE NATURAL ORDER DESCENDING)
list(LENGTH _volumetric_rc_candidates _volumetric_rc_count)
if(_volumetric_rc_count EQUAL 0)
  message(FATAL_ERROR "Windows SDK rc.exe not found")
endif()
list(GET _volumetric_rc_candidates 0 _volumetric_rc)
get_filename_component(_volumetric_sdk_bin "${_volumetric_rc}" DIRECTORY)
set(CMAKE_RC_COMPILER "${_volumetric_rc}" CACHE FILEPATH "" FORCE)
set(CMAKE_MT "${_volumetric_sdk_bin}/mt.exe" CACHE FILEPATH "" FORCE)
