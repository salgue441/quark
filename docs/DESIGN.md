# quark design rules

## Memory reclamation

- Use hazard pointers (`quark::HazardDomain`) for lock-free structures that
  share nodes across threads (e.g. Michael-Scott queue, hash map).
- `SpscQueue` does **not** use hazard pointers: the single producer and
  consumer alone own slot lifetimes.
- `MsQueue` uses `HazardDomain` + `TaggedPtr` for node reclamation and ABA
  safety; contended CAS loops use `Backoff`.
- Domains must outlive all threads that call `thread_handle` / `retire`.
- Destroying a domain with live handles aborts. Prefer `default_domain()` or
  container-owned domains released via `release_thread_handle` before destroy.
- `MsQueue` owns a domain by default or borrows `HazardDomain&`; workers must
  `release_thread_handle` before the domain is destroyed.
- `MsQueue` destructor assumes exclusive access and drains remaining nodes.
- Reclaimers may be custom callables; the reclaim path must not use logging.

## Concurrency

- Contended CAS / retry loops must use `quark::Backoff` (pause → yield → sleep).
- `SpscQueue::push_wait` / `pop_wait` use `Backoff` internally; `try_*` leaves
  retry policy to the caller.
- `MsQueue` exposes `try_*` and `Result` `push`/`pop` only — **no** `*_wait`.
  `try_push` fails only on allocation failure; `try_pop` fails when empty.
- Prefer acquire/release for publish/observe. Use seq_cst only with a written
  justification in the PR.

## Testing

- Unit/stress tests use Catch2. Sanitizers: ASan and TSan in separate builds.
- libFuzzer covers randomized protect/retire/flush sequences.
