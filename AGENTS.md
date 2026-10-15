# Embedded library rules

- Keep this project independent. Do not replace or modify Trail Mate while
  implementing the library unless the user explicitly requests integration.
- Core code must not allocate, require exceptions/RTTI, or retain unbounded
  containers. Workspaces and geometry buffers belong to the caller.
- Large aggregate assignment can create temporary stack copies on Xtensa.
  Construct cursor scratch in place and verify the real cross-compiler reports.
- Every potentially long operation needs explicit work/storage bounds and
  cancellation. Budget exhaustion is an explicit result, never a hidden grow.
- Preserve legacy compatibility and source provenance. Run real-map baseline
  comparisons before changing legacy behavior; never claim performance from
  desktop startup timing or byte counts alone.
- Use clang-format 14 for new C++ files. Do not reformat provenance-preserved
  legacy files unless their changes are deliberate and documented.
- Run CTest, the installed-consumer check and core-only build. Cross-compile
  when tools are available. Keep new core ESP frames <=240 bytes with the
  current MinSizeRel toolchain; host budget is <=256 bytes.
- Keep machine paths and toolchain locations in ignored AGENTS.local.md.
- Local build caches use .codex-build/<target>. No test/build binaries belong
  in release sources. Tests must verify behavior, failure and resource bounds.
