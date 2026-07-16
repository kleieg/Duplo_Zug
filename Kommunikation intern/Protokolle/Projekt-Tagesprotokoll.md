# Project Daily Log

## 2026-03-19

- Analyzed the cause of perceived delay after BLE connection establishment.
- Determined that the connection callback logic blocked the BLE task for several seconds, which delayed command execution significantly.
- Moved DUPLO hub initialization into a non-blocking step sequence in the main loop.
- Introduced a new ready state: blue still signals "connected, initialization running", and green starts blinking only when commands can actually be processed.
- Correctly gated button and encoder input until ready state.
- Successfully built firmware with `platformio run -e esp32-s3-fh4r2`.
- Further analyzed reconnect delay after train power cycle.
- Reduced Legoino reconnect scan window from 10 s to 2 s and prevented repeated restart of scan in every BLE loop.
- Tuned BLE client connection parameters to a shorter supervision timeout so train power loss is detected faster.
- Successfully rebuilt firmware after reconnect tuning with `platformio run -e esp32-s3-fh4r2`.
- Hardware test confirmed: reconnect after train power cycle now in about 2-3 s.
- Hardware test confirmed: after reconnect, the train responds to commands immediately.
- Extended project-internal header files with more consistent `@file`, class, and API descriptions.
- Synchronized `README.md`, `ARCHITECTURE.md`, and `Benutzerhandbuch_Kurz_DE.md` with the currently implemented ready, reconnect, and sleep/wake behavior.
- Streamlined `Refactorings.md` as technical history and reduced `ToDo.md` to still-open topics.
