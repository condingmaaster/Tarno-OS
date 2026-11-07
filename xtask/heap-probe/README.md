# Kernel heap probe (host only)

Reproducible checks for `kernel/src/small_heap.rs`, using the same locked dependency
versions as THOS. No network or privileged execution is required.

```sh
cargo test --manifest-path xtask/heap-probe/Cargo.toml --target x86_64-unknown-linux-gnu --offline --release
cargo run --manifest-path xtask/heap-probe/Cargo.toml --target x86_64-unknown-linux-gnu --offline --release
```

The benchmark compares the old heap and the optional bounded cache on fragmented
mixed-size allocation churn. Its ratio describes that workload, not whole-OS speed.
Guest verification is still required. The cache retains at most 511.5 KiB and
returns free blocks to the backing heap if an allocation otherwise fails.
