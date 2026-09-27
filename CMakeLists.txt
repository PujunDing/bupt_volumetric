cmake_minimum_required(VERSION 3.25)
project(bupt_volumetric LANGUAGES C CXX)

option(BUPT_VOLUMETRIC_BUILD_TESTS "Build volumetric prototype tests" ON)
option(BUPT_VOLUMETRIC_ENABLE_RUNTIME "Build sender/receiver applications" OFF)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(CompilerOptions)
include(WebRtcDependency)
include(GoogleTestImport)
bupt_volumetric_apply_webrtc_abi()

add_library(bupt_volumetric_model STATIC
  src/config/app_config.cc
  src/latency/latency_clock.cc
  src/latency/latency_recorder.cc
  src/model/simulated_octree_frame.cc
  src/model/semantic_region.cc
  src/model/semantic_block.cc
  src/metrics/run_manifest.cc
  src/protection/fec_profile.cc
  src/protection/fec_wire_accounting.cc
  src/protection/protection_budget.cc
  src/protection/protection_policy.cc
  src/receiver/block_assembler.cc
  src/receiver/frame_availability_tracker.cc
  src/signaling/protocol.cc
  src/signaling/signaling_server.cc
  src/signaling/signaling_client.cc
  src/simulation/frame_generator.cc
  src/simulation/semantic_partitioner.cc
  src/transport/access_unit_planner.cc
  src/transport/block_planner.cc
  src/transport/semantic_admission_controller.cc
  src/transport/semantic_packetizer.cc
  src/transport/simulated_access_unit.cc
  src/transport/transport_feedback.cc
  src/transport/transport_budget.cc
  src/transport/wire_header.cc)
target_include_directories(bupt_volumetric_model PUBLIC
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>")
target_link_libraries(bupt_volumetric_model PUBLIC bupt_volumetric::libcxx)
bupt_volumetric_apply_compiler_options(bupt_volumetric_model)

# Runtime codec layer (OCTREE-SIM factory/encoder/decoder) links the patched
# WebRTC archive. Gated behind the runtime flag so the pure-algorithm preset
# never forces a webrtc.lib link.
if(BUPT_VOLUMETRIC_ENABLE_RUNTIME)
  bupt_volumetric_import_webrtc()
  add_library(bupt_volumetric_rtc STATIC
    src/apps/metrics_window.cc
    src/apps/signal_loopback.cc
    src/rtc/control_channel.cc
    src/rtc/data_channel_control_binding.cc
    src/rtc/octree_sim_encoder_factory.cc
    src/rtc/octree_sim_encoder.cc
    src/rtc/octree_sim_decoder_factory.cc
    src/rtc/octree_sim_decoder.cc
    src/rtc/rtc_configuration.cc
    src/rtc/rtc_stats_mapper.cc
    src/rtc/semantic_payload_observer_adapter.cc
    src/rtc/simulated_frame_source.cc
    src/rtc/peer_connection_session.cc
    src/signaling/tcp_transport.cc)
  target_include_directories(bupt_volumetric_rtc PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>")
  target_link_libraries(bupt_volumetric_rtc PUBLIC
    bupt_volumetric_model bupt_volumetric::webrtc)
  bupt_volumetric_apply_webrtc_runtime_abi(bupt_volumetric_rtc)
  bupt_volumetric_apply_compiler_options(bupt_volumetric_rtc)

  # Runnable process composition roots.
  add_executable(bupt_volumetric_signaling_server src/apps/signaling_server_main.cc)
  target_link_libraries(bupt_volumetric_signaling_server PRIVATE bupt_volumetric_rtc)
  bupt_volumetric_apply_webrtc_runtime_abi(bupt_volumetric_signaling_server)
  bupt_volumetric_apply_compiler_options(bupt_volumetric_signaling_server)

  add_executable(bupt_volumetric_sender src/apps/sender_main.cc)
  target_link_libraries(bupt_volumetric_sender PRIVATE bupt_volumetric_rtc)
  bupt_volumetric_apply_webrtc_runtime_abi(bupt_volumetric_sender)
  bupt_volumetric_apply_compiler_options(bupt_volumetric_sender)

  add_executable(bupt_volumetric_receiver src/apps/receiver_main.cc)
  target_link_libraries(bupt_volumetric_receiver PRIVATE bupt_volumetric_rtc)
  bupt_volumetric_apply_webrtc_runtime_abi(bupt_volumetric_receiver)
  bupt_volumetric_apply_compiler_options(bupt_volumetric_receiver)
endif()

if(BUPT_VOLUMETRIC_BUILD_TESTS)
  enable_testing()
  bupt_volumetric_import_gtest()
  include(GoogleTest)
  add_library(bupt_volumetric_test_main OBJECT
    "${CMAKE_CURRENT_SOURCE_DIR}/../test/test_volumetric_support/test_main.cc")
  target_link_libraries(bupt_volumetric_test_main PRIVATE GTest::gtest)
  bupt_volumetric_apply_compiler_options(bupt_volumetric_test_main)

  function(bupt_volumetric_add_test target_name source_file)
    add_executable(${target_name} ${source_file}
      $<TARGET_OBJECTS:bupt_volumetric_test_main>)
    target_link_libraries(${target_name} PRIVATE bupt_volumetric_model GTest::gtest)
    bupt_volumetric_apply_compiler_options(${target_name})
    gtest_discover_tests(${target_name})
  endfunction()

  # Tests that touch the WebRTC codec layer link the runtime library and get the
  # full WebRTC ABI definitions; only enabled with the runtime preset.
  function(bupt_volumetric_add_rtc_test target_name source_file)
    add_executable(${target_name} ${source_file}
      $<TARGET_OBJECTS:bupt_volumetric_test_main>)
    target_link_libraries(${target_name} PRIVATE bupt_volumetric_rtc GTest::gtest)
    bupt_volumetric_apply_webrtc_runtime_abi(${target_name})
    bupt_volumetric_apply_compiler_options(${target_name})
    gtest_discover_tests(${target_name})
  endfunction()
  add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../test/test_volumetric_build_contract"
                   "${CMAKE_BINARY_DIR}/tests/build_contract")
  add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../test/test_volumetric_model"
                   "${CMAKE_BINARY_DIR}/tests/model")
  add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../test/test_volumetric_packetization"
                   "${CMAKE_BINARY_DIR}/tests/packetization")
  add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../test/test_volumetric_protection"
                   "${CMAKE_BINARY_DIR}/tests/protection")
  add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../test/test_volumetric_congestion"
                   "${CMAKE_BINARY_DIR}/tests/congestion")
  add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../test/test_volumetric_receiver"
                   "${CMAKE_BINARY_DIR}/tests/receiver")
  add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../test/test_volumetric_signaling"
                   "${CMAKE_BINARY_DIR}/tests/signaling")
  add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../test/test_volumetric_latency"
                   "${CMAKE_BINARY_DIR}/tests/latency")
  add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../test/test_volumetric_loopback_e2e"
                   "${CMAKE_BINARY_DIR}/tests/loopback_e2e")
  if(BUPT_VOLUMETRIC_ENABLE_RUNTIME)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../test/test_volumetric_rtc"
                     "${CMAKE_BINARY_DIR}/tests/rtc")
  endif()
  add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../test/test_volumetric_metrics"
                   "${CMAKE_BINARY_DIR}/tests/metrics")
endif()
