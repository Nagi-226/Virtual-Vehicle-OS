# v0.8 Platform Integration Guide

## Scope
This guide defines minimal integration steps for RTOS/STM32 and ROS2 sample paths in v0.8.

## 1) RTOS/STM32 Minimal Path
- Sample file: `demo/rtos_adapter_sample.cpp`
- Expected behavior: queue init/send/receive roundtrip success.
- Validation:
  1. Build sample target
  2. Run sample
  3. Verify output contains `rtos adapter sample ok`

## 2) ROS2 Minimal Path
- Sample file: `demo/ros2_adapter_sample.cpp`
- Expected behavior: topic and qos setup + topic mapping to envelope namespace.
- Validation:
  1. Build sample target
  2. Run sample
  3. Verify output contains `ros2 adapter sample ok`

## 3) Error/Code Mapping Baseline
- Keep transport and adapter errors mapped to existing `vr::core::ErrorCode`
- For platform-specific failures, map first to nearest common code, then add extension later.

## 4) Acceptance Checklist
- [ ] RTOS sample builds and runs
- [ ] ROS2 sample builds and runs
- [ ] CTest includes platform sample tests
- [ ] quality gate remains pass
