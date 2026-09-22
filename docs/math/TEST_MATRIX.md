# Test Matrix (proposed + current)

Legend: **Y** = strong math assert today · **P** = partial/structural · **N** = missing · **—** = N/A

| Primitive | DC | AC | Transient | Nonlinear | Invalid | Boundary |
|-----------|----|----|-----------|-----------|---------|----------|
| R | Y g01–g03 | N | — | — | Y g23 floating | N singular R=0 |
| C | N (omit open) | N | Y g25 BE step | — | N | N C=0 |
| L | N (need short DC) | N | P BE only | — | N | N L=0 |
| V | Y via divider | N | — | — | Y g24 conflict V | N |
| I | N dedicated | N | — | — | N | N |
| VCVS/VCCS/CCVS/CCCS | P stamp only | N | — | — | N | N |
| Diode | Y g22 + mna_test | N | N | Y Newton | N | P exp clamp |
| BJT/MOS | N fail-closed verify | N | N | N | Y unsupported | — |
| Logic | N stamp false | — | — | — | — | — |

## Existing tests → mathematical fact

| Test | Fact |
|------|------|
| g01 | Four-entry G stamp |
| g02 | Vout=5 for 10 V equal R |
| g03 | Parallel divider algebra |
| g22 | Physics2 diode OP ≈ vendor |
| g23 | Floating island → solve fails |
| g24 | Conflicting V sources → solve fails |
| g25 | BE C step Vmid = 5/(1+RC/dt) |
| mna_test | vendor Shockley OP + KCL residual |
| g17–g20 | LED/RC/RL analytical or verify.passed |
| g21 | IC fail-closed passed=0 |
| g04–g16 | integration / emit / corpus |

## Priority missing tests

1. Controlled-source analytic OP  
2. Residual norm on diode after Newton  
3. AC smoke once complex MNA exists  
4. Singular R=0 / C=0 boundary  
