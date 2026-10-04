Pending storage-policy SOURCE proposal, not tested or accepted firmware.

Source worktree: /home/sean/Documents/.cdk2-worktrees/rtc-board-facts-after677
Source branch: agent/native-storage-policy-after688
Signed base: 62352715633271454b9c2edc10c055c12fb5deef
Exact binary Git diff SHA256:
cc47bde1af147a0487f8b57aaa84536ed61b463dac19ca4d4828791aa050f5e5

The two owned changes gate NVMe/AHCI candidate classification on their native
driver policies and add a targeted include-entry fixture. Existing mixed-device
tests remain. The diff has source reviews but has NOT been compiled, executed,
joined into canonical CDK2, or accepted for guest/hardware use.

This copy preserves pending work in Git while the host is unreliable. It does
not change current canonical signed58598 or close any implementation gate.
Before integration: finish native-type fixture style, review the resulting
source again, exercise genuine policy combinations through actual consumers,
retain causal old-source failures, and run joined validation.
