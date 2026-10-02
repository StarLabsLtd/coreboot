# Reviewed coreboot formatter batches

Coreboot formatter profile SHA256:
`c191fe47d2b59fdceeed23c6e737a4f73b438fe7994559cdea815ddea6ded319`.
Local clang-format version: 20.1.8. Complete ordered C-token comparisons were
performed independently for every file. ABI and control flow are preserved;
imported vendor source is not rewritten. No object-byte identity is claimed.

- PR488, `a71819df7656cffe0dbbb196f6abbe7f1e5654ea`: four TCG/battery/SCSI
  tests. Fresh native gates 18412 returned 0 (2.087s); after three literal
  spacing corrections, native replay returned 0 (1.127s). Initial lint 94143
  returned 1 with three errors and 25 warnings. Final lint 5089 returned 1
  with two calling-convention attribute parser findings and 25 warnings.
- PR489, `93116eda80d43f7a979825f8323bb86906c4f9e9`:
  four ATA/device-path/USB-mass/xHCI tests. Four fresh native binaries 22454
  returned 0 (4.250s). After correcting one literal xHCI closing-brace
  indentation, the changed binary recompiled and all four executed again,
  returning 0 (865ms). Initial lint 92152 returned 1 with two errors and
  three warnings. Final lint 13393 returned 1 with one calling-convention
  attribute parser finding and two unnamed-prototype warnings.
- PR490, `e40149b6648513065ac6e52909cd3cd959351525`:
  three hash/FMP/English headers. Native host tests and both English/FMP PE
  gates 33790 returned 0 (1.457s). Lint returned 1 with 36 unnamed-prototype
  warnings and seven genuine-typedef parser ambiguities. Packed layout,
  size checks and calling-convention tokens are unchanged.
- The following narrowly scoped parser-knowledge batch teaches six actual
  typedefs; it changes neither checkpatch's imported parser nor warning
  exclusions. The full filter/launcher/allowlist regression 5294 returned 0
  (29.659s), including correct pointers and deliberately incorrect star
  placement for every new type. The English header lint then returned 0
  (581ms). Independent source review and six-type good/bad-pointer replay
  accepted the final signed commit
  `1fa6ed628706033d8bf84a295d473e8eafcbef02`, ready PR491.

All failed lint outputs are retained as failures. These bounded successes do
not establish full-tree style cleanliness, native runtime activation, hardware
validation, cross-platform DMA completeness or final reviewer consensus.
