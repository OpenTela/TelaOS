# TLSF — the allocator under ESP-IDF's heap

Two-Level Segregated Fit allocator by Matthew Conte (BSD-3-Clause, see `LICENSE`), in the
fork ESP-IDF's `heap` component builds `multi_heap` on: https://github.com/espressif/tlsf,
commit `19d937a`. The simulator manages its model of the watch's DRAM and PSRAM with it, so
fragmentation and the largest free block behave as on the chip.

Local change (marked "TelaOS simulator" in `tlsf.c`): `control_construct` measures the
control block from its aligned pointers. Summing the sizes missed the padding before
`blocks[]` on a 64-bit host, and the last entries of `blocks[]` overlapped the pool
(`tlsf_check` failed on a fresh heap). On the 32-bit chips the two computations agree.
