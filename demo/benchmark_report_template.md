# Interconnect Benchmark Report

## Summary
- Date: ${DATE}
- Commit: ${COMMIT}
- Driver: demo/interconnect_benchmark_driver.cpp

## Test Config
- message_count_per_direction: ${MESSAGE_COUNT}
- payload_size_bytes: ${PAYLOAD_BYTES}
- publish_gap_ms: ${PUBLISH_GAP_MS}
- transport: posix_mq
- thread_pool_workers: ${WORKERS}
- queue_capacity: ${QUEUE_CAPACITY}

## Results
- elapsed_ms: ${ELAPSED_MS}
- v2r_received: ${V2R_RX}
- r2v_received: ${R2V_RX}
- throughput_msg_per_sec: ${THROUGHPUT}

## Notes
- Record environment: OS/CPU/Memory.
- Attach raw benchmark output.
- Provide variance if multiple runs executed.
