# Refactorings

This document is still relevant, but is now clearly categorized as a historical and technical work log.

## Purpose Of This Document

This file documents larger structural changes that do more than add features: they improve maintainability, latency, or separation of responsibilities.

It is therefore:

- still relevant as architecture and maintenance history
- not intended as a complete task list
- not intended as a replacement for [ToDo.md](ToDo.md)

## Important Refactorings Already Implemented

- cleanup of dead and unused paths in `TrainController`
- consolidation of `DuploHub` command construction
- color sensor debouncing via stability timer
- optimization of LED updates via dirty flag in `StatusLED`
- separation of `connected` and `ready`
- moved post-connect initialization out of BLE callback into a non-blocking main-loop flow
- faster init, reconnect, and command acceptance via reduced delays and shorter reconnect scans

## Refactoring Track Currently In Progress

### Legoino Consolidation Via `myLegoHub`

This topic remains relevant.

Goal:

- `DuploHub` should work only against a stable project facade
- DUPLO-specific low-level details should be concentrated in `myLegoHub`
- direct dependency on Legoino internals should be reduced further

Next sensible steps:

1. explicitly complete the required API in `myLegoHub`
2. migrate direct `hub.*` calls in `DuploHub` to this facade step by step
3. run build and hardware test for each migration slice

## No Longer Relevant As Active Refactoring Topic

The following points are completed and no longer need a separate tracking structure:

- SystemMemory file naming and consistency
- replay endless loop removed
- unimplemented `DuploHub` header API cleaned up
- water button behavior corrected
- status LED base logic unified

Status: 19.03.2026
