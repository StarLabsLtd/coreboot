# Reproducible PR458 style diagnostic baseline

Source: clean signed CDK2 `301023dfacc71d562f4f70c60f943a15fa925204`.
The independent worker reaped the actual wrapper with exit 1:

```
LC_ALL=C TMPDIR=/home/sean sh util/lint/lint-007-checkpatch
```

The wrapper stream contains 1,708 error and 205 warning diagnostics across
92 named paths (97 per-file summary rows). Its exact old selector admitted
886 tracked inputs under `Kconfig src include util tests`. Anchoring C/H
suffixes excludes 14 accidental non-source matches, leaving 872 inputs;
both lists are retained. The fixed list is selection evidence, not a claim
that a full fixed-selector scan passed.

The separately reaped raw Perl per-file comparator contains 1,744 errors
and 205 warnings, with 113 nonzero inputs and explicit file/exit labels.
The wrapper removes 36 source-exact allowlisted errors. Classification of
the remaining path-associated diagnostics separates 853 imported/vendor,
476 fixture/utility, 116 public-boundary header and 463 owned implementation
records, plus five unnamed records from accidental non-source inputs.
These are parser diagnostics, not counts of proven defects or a complete
semantic audit. ABI types and imported code are not blanket style waivers.
This does not rewrite the uncertain provenance of the earlier inherited
stream in `style-rescan-6d760`.

SHA256 of source tools obtained directly from that Git revision:

```
bc0b58016884170006a418d688a61ebfc35033edf5be10ef9937f507bab74a3b  util/lint/lint-007-checkpatch
b79c7e3d09a9eb8319b7eeed8117f43db9200285e4ffbbf543603d5348535ef9  util/lint/helper_functions.sh
fbf1852a8ae1a82de2c5c2c2c5eaed40bd076f34de8a36797edce469791f3330  util/lint/cdk2-checkpatch
945a529e4a1e7183ede7e025b87cee4b50d28b7b642406f9502b1a2392dce18a  util/lint/checkpatch.pl
3e409928f8c789d41f016127bd39d4f148ccff28a2d5b9cba87591bd8573a48d  util/lint/cdk2-checkpatch-filter.c
9956cc473400f3e54099b7a89f4c20ab16128ee1ce8cc56947ff4d8e45884513  util/lint/cdk2-typedefs.checkpatch
03d365728b29a60725314b21739f6ffcebc72c15a0d1a66b24684c1680a1f115  util/lint/checkpatch-paths
fad95ce1509744fce71ec19c0eb7ea0a03026571b8f169ffae16870defa973bd  util/lint/cdk2-checkpatch-allowlist.tsv
```

Retained raw file hashes:

```
e381ca329ac51aa8c5e5bdaccfc7002dd73bf03ddd4c79062ae3f2d48346d496  cdk2-consumer-301-lint-007.log
b3a9578a39d9fca03e506ca5dd2120ffeecf0bb249a1d4b115d1d02df3dccb0f  cdk2-consumer-301-checkpatch-raw-correct.log
6cda3237b6f7c582baa8705865d41f5df639645cd7ecf53c45d59a4078dfc68d  cdk2-consumer-301-checkpatch-inputs.txt
79ad02f791cf79396ec431a60b63775058cae7ddd4a84b9cc4e0e8970d4ac31a  cdk2-consumer-301-checkpatch-inputs-fixed.txt
```

No full-tree style acceptance, production protected-mode activation, hardware
validation or project completion is established by this scan.
