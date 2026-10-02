# Linear public-service and bounded style replays

These are raw root receipts, not a whole-roadmap sign-off.

- Public consumer `a526141b584b0e36249835a264ba33612206eced` is the
  reviewed linear copy of `05da8a58a1e73ca25d72715ea5cc8c0bd56646e3`.
  The actual selected four-boot component replay (session 43914) returned 0
  in 18.708s; default replay (35474) returned 0 in 17.518s. Both used the
  independently rebuilt producer with ROM SHA256
  `d80a815d7fba0a7404e9aeb7217d2f49356801666d6a73f67e4ad2e1e92fc357`.
  These do not establish whole-image EBS or virtual-address runtime execution.
- Hash test formatter commit `5420adbfb62eb729de06ca22133614124ebf2026`
  passed three fresh native gates (25612, exit 0, 2.098s). Acceptance fixture
  formatter commit `16aaa8a0a30a71a49df867aca129144d0a2e87a4` passed five
  native binaries (95607, exit 0, 2.489s). Independent full token comparison
  accepted both batches. CHECK stringification whitespace can differ; no
  object-byte identity or complete style-clean claim is made.
- The subsequent four-file standard-width test cleanup was still uncommitted
  when its native gate returned 0 (65557, 2.237s). Its first-file-only lint
  returned 1 with three warnings and no errors. The all-four lint (98697)
  returned 1 with two real initializer closing-brace indentation errors and
  five warnings. These failures remain failures; the unchanged native gate
  does not imply a complete style pass.
- Reviewed final width/indentation commit
  `6ccd3f13f4099477c2b26d953e38bbc50da82192` is ready PR487. After the two
  literal closing-brace corrections, the fresh four native tests returned 0
  (45783, 1.328s). Final all-four lint returned 1 (73716, 2.991s), with zero
  errors and three existing software-hash warnings. No failure is relabelled.

Production still retains the protected full-boot guard. Hardware validation,
full-tree style, platform DMA coverage, capsule round trips and final reviewer
consensus remain open.
