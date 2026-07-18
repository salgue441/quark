# quark design rules

## Memory reclamation

- Use hazard pointers (`quark::HazardDomain`) for lock-free structures.
- Domains must outlive all threads that call `thread_handle` / `retire`.
- Destroying a domain with live handles aborts. Prefer `default_domain()` or
  container-owned domains released via `release_thread_handle` before destroy.
- Reclaimers may be custom callables; the reclaim path must not use logging.

## Concurrency

- Contended CAS loops must use `quark::Backoff` (pause → yield → sleep).
- Prefer acquire/release for publish/observe. Use seq_cst only with a written
  justification in the PR.

## Testing

- Unit/stress tests use Catch2. Sanitizers: ASan and TSan in separate builds.
- libFuzzer covers randomized protect/retire/flush sequences.
