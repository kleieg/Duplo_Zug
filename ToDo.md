# ToDo

This file is still relevant.

It has been cleaned up to the current state and now contains only open or intentionally deferred topics. Completed items were moved to ongoing documentation, the changelog, or the daily project log.

## Open Software Topics

- continue Legoino consolidation via the `myLegoHub` facade
  - extend the project API in `myLegoHub`
  - migrate `DuploHub` to the facade step by step
  - run hardware tests for each migration step

- further clean up logging
  - focus debug output on meaningful runtime and diagnostics paths
  - optionally separate log levels via build flags

## Open Product/Feature Topics

- store a preferred train and prioritize it during reconnect
- continue pairing concept only if usage remains simple for children

## Open Hardware Topics

- true controller battery monitoring requires hardware changes
- an additional pairing button is only useful if pairing is actually implemented

## Intentionally Deferred

- extensive pairing workflow with 60-second fallback
  - technically feasible, but currently not prioritized
  - only useful if multi-train operation is truly required

## Completed And No Longer Tracked Here

- replay now runs only once
- input during replay is correctly restricted
- color sensor debounced
- sleep/wake implemented
- init and reconnect latency significantly reduced
- ready state correctly separated from BLE connected
- reconnect after power-cycle improved to about 2 to 3 seconds

Status: 19.03.2026
