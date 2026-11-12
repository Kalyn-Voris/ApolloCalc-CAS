# SMOKE-DIFF-STURM (v2.5.0 F3)

C (ac_pro_sturm) vs JS (core_ratroot sturm 扩段)，共 62 checks

| case | verdict | detail |
|---|---|---|
| st-lin [dual count] | OK | C=1 JS=1 want=1 |
| st-lin [dual count] | OK | C=1 JS=1 want=1 |
| st-lin [dual isolate] | OK | C="[[-4, 4]]" JS="[[-4, 4]]" want="[[-4, 4]]" |
| st-cubic [dual count] | OK | C=3 JS=3 want=3 |
| st-cubic [dual count] | OK | C=3 JS=3 want=3 |
| st-cubic [dual count] | OK | C=2 JS=2 want=2 |
| st-cubic [dual count] | OK | C=3 JS=3 want=3 |
| st-cubic [dual count] | OK | C=3 JS=3 want=3 |
| st-cubic [dual count] | OK | C=3 JS=3 want=3 |
| st-cubic [dual isolate] | OK | C="[[-2, -1], [-1, 0], [0, 2]]" JS="[[-2, -1], [-1, 0], [0, 2]]" want="[[-2, -1], [-1, 0], [0, 2]]" |
| st-x2p1 [dual count] | OK | C=0 JS=0 want=0 |
| st-x2p1 [dual count] | OK | C=0 JS=0 want=0 |
| st-x2p1 [dual count] | OK | C=0 JS=0 want=0 |
| st-x2p1 [dual isolate] | OK | C="[]" JS="[]" want="[]" |
| st-x4p1 [dual count] | OK | C=0 JS=0 want=0 |
| st-x4p1 [dual isolate] | OK | C="[]" JS="[]" want="[]" |
| st-quartic4 [dual count] | OK | C=4 JS=4 want=4 |
| st-quartic4 [dual count] | OK | C=2 JS=2 want=2 |
| st-quartic4 [dual count] | OK | C=4 JS=4 want=4 |
| st-quartic4 [dual isolate] | OK | C="[[-4, -2], [-2, 0], [0, 1], [1, 2]]" JS="[[-4, -2], [-2, 0], [0, 1], [1, 2]]" want="[[-4, -2], [-2, 0], [0, 1], [1, 2]]" |
| st-sff-dual [dual count] | OK | C=2 JS=2 want=2 |
| st-sff-dual [dual isolate] | OK | C="[[-4, 0], [0, 4]]" JS="[[-4, 0], [0, 4]]" want="[[-4, 0], [0, 4]]" |
| st-endpoints [dual count] | OK | C=2 JS=2 want=2 |
| st-endpoints [dual count] | OK | C=2 JS=2 want=2 |
| st-endpoints [dual count] | OK | C=2 JS=2 want=2 |
| st-endpoints [dual count] | OK | C=1 JS=1 want=1 |
| st-endpoints [dual count] | OK | C=0 JS=0 want=0 |
| st-endpoints [dual count] | OK | C=1 JS=1 want=1 |
| st-endpoints [dual count] | OK | C=0 JS=0 want=0 |
| st-neglc [dual count] | OK | C=3 JS=3 want=3 |
| st-content [dual count] | OK | C=2 JS=2 want=2 |
| st-content [dual isolate] | OK | C="[[-2, 0], [0, 2]]" JS="[[-2, 0], [0, 2]]" want="[[-2, 0], [0, 2]]" |
| st-const [dual count] | OK | C=0 JS=0 want=0 |
| st-const [dual isolate] | OK | C="[]" JS="[]" want="[]" |
| st-close [dual count] | OK | C=1 JS=1 want=1 |
| st-close [dual isolate] | OK | C="[[-2, 0], [0, 2]]" JS="[[-2, 0], [0, 2]]" want="[[-2, 0], [0, 2]]" |
| st-close2 [dual count] | OK | C=1 JS=1 want=1 |
| st-close2 [dual isolate] | OK | C="[[-1024, 0], [0, 1024]]" JS="[[-1024, 0], [0, 1024]]" want="[[-1024, 0], [0, 1024]]" |
| st-mignotte-iso [dual isolate] | OK | C="[[0, 1/2], [1/2, 1]]" JS="[[0, 1/2], [1/2, 1]]" want="[[0, 1/2], [1/2, 1]]" |
| st-eparam [dual count] | OK | C=-4 JS=-4 want=-4 |
| st-eparam [dual count] | OK | C=-4 JS=-4 want=-4 |
| st-eparam [dual count] | OK | C=-4 JS=-4 want=-4 |
| st-eparam [dual count] | OK | C=-4 JS=-4 want=-4 |
| st-eparse [dual count] | OK | C=-1 JS=-1 want=-1 |
| st-eparse-eq [dual count] | OK | C=-1 JS=-1 want=-1 |
| st-eparse-slash [dual count] | OK | C=-1 JS=-1 want=-1 |
| st-eparse-empty [dual count] | OK | C=-1 JS=-1 want=-1 |
| st-eparse-dup [dual count] | OK | C=-1 JS=-1 want=-1 |
| st-edeg9 [dual count] | OK | C=-2 JS=-2 want=-2 |
| st-ecoef-zero [dual count] | OK | C=-3 JS=-3 want=-3 |
| st-ecoef-1000 [dual count] | OK | C=-3 JS=-3 want=-3 |
| st-deg5 [cOnlyDEG count] | OK | C=1 JS=-2 (C want 1, JS want -2) |
| st-deg5 [cOnlyDEG isolate] | OK | C="[[-2, 2]]" JSrc=-2 |
| st-deg5-rep [cOnlyDEG count] | OK | C=2 JS=-2 (C want 2, JS want -2) |
| st-deg5-rep [cOnlyDEG isolate] | OK | C="[[-4, 0], [0, 4]]" JSrc=-2 |
| st-deg8 [cOnlyDEG count] | OK | C=0 JS=-2 (C want 0, JS want -2) |
| st-deg8 [cOnlyDEG isolate] | OK | C="[]" JSrc=-2 |
| st-dense8 [cOnlyDEG count] | OK | C=0 JS=-2 (C want 0, JS want -2) |
| st-ovf-pass-9741 [cOnlyOVF count] | OK | C=0 JS=0 (C want 0, JS want 0) |
| st-ovf-reject-9742 [cOnlyOVF count] | OK | C=0 JS=-3 (C want 0, JS want -3) |
| st-ovf-mignotte-endpoint [cOnlyOVF count] | OK | C=2 JS=-3 (C want 2, JS want -3) |
| st-ovf-cubic-endpoint [cOnlyOVF count] | OK | C=1 JS=-3 (C want 1, JS want -3) |

**62/62 aligned, exit 0**
