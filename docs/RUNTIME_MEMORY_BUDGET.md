# Runtime memory

The runtime distinguishes free 8-bit heap, largest 8-bit block, internal heap, DMA heap and minimum-ever heap. A single free-heap number cannot prove an RMT or JSON allocation will succeed.

`RuntimeMemory.hpp` declares setup/normal/max-topology targets, not measurements. The default post-topology/operation reserve is 16 KiB. Admission requires remaining total capacity and a sufficiently large contiguous block; output allocation also has internal-memory constraints.

Heavy operations may return `insufficient_runtime_memory`. Inspect diagnostics and reduce topology/competing work rather than retrying blindly. Factory/network-apply workers are lazy; setup suppresses unnecessary services.

Use `/api/v1/diagnostics/memory`, `/api/v1/health`, output and serial diagnostics. Counters include allocation failures, active services and task stack watermarks. The pinned Xtensa port's stack watermark units are bytes; do not apply a generic word-size multiplier.

Policy lives in `scripts/boot_memory_policy.json`, `check_invariants.py`, `verify_boot_stack.py`, `audit_runtime_hardening_stack.py` under `LumigridLedNode/`. ELF audits apply to that build only. Hardware acceptance needs setup/idle/full-topology/DMX/reconnect/heavy-operation measurements with exact revision and load context.

Sources: `LumigridLedNode/components/factory/include/RuntimeMemory.hpp`, `RuntimeMemory.cpp`, `components/output/OutputController.cpp`, `main/LumigridRuntime.cpp`. No fresh hardware memory measurements accompany this rewrite.
