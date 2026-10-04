UNEXECUTED bounded HOST retry for source88b8b8d3 on signed661.
Independent source and outer review must precede execution. No VM/hardware,
firmware build, real efivarfs operation, canonical write or executable seed.
The real Linux helper now treats an unavailable syscall as error1 instead of
unsupported3; ENOTTY/EOPNOTSUPP remain3. Kernel ioctl documentation explicitly
rejects historical ENOSYS as an unsupported-ioctl convention:
https://cdn.kernel.org/doc/html/latest/driver-api/ioctl.html
Caller init permits delete only on0/3, so this is honest pre-firmware refusal.

The fixture includes actual helper source and models only OS open/ioctl/close/
perror boundaries. Tests assert immutable-bit clearing with other bits preserved,
no set on failed get, exact close/report counts, argument/open/get/set failures,
and success. The unsupported syscall errno is supplied by host Perl Errno,
recorded explicitly, not hardcoded or a copied helper policy implementation.
Four strict O0/O2/ordinary ASAN/UBSAN modes, real helper compile and raw changed
file style scans must pass. The Perl Errno module bytes are also bound. Exact signed661 helper with the same final fixture
must abort134 at actual result assertion, separately qualified opposition0.

Tracked source bytes/diff/head/status, selected tools/compiler support/SAN libs,
compiler-M headers, old source and recipes compare before/after. Finite input
closure is not dynamic-loader/runtime attestation. Prep failure remains raw and
unclosed; later execution/closure/aggregate statuses are separate. Source style
scans use existing source configuration and bind source/HOME/.scripts checker
configuration presence and bytes. Preserve failures and retry into a new directory.

Original /home/sean/efivar-unlock-contract-host.phWpE0 remains execution1,
closure0, aggregate1. All four strict fixture modes, actual helper compile,
old-helper134/opposition0 and helper style passed. Only the new fixture style
failed two warnings (mutable argv pointer declaration and ARRAY_SIZE idiom).
This new source changes only that fixture to mutable strings with explicit
three-pointer argv and the existing local ARRAY_SIZE idiom. Behavior and all
production source stay unchanged. The original run/source/recipes are preserved.
