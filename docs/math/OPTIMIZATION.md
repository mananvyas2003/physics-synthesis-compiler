# Bounded Optimization Problems

No general global optimizer. These are tractable, checkable problems.

| Problem | Decision vars | Objective | Hard constraints | Soft | Feasible region | Search | Validate |
|---------|---------------|-----------|------------------|------|-----------------|--------|----------|
| E-series R pick | discrete R ∈ E24/E96 | min \|R−R*\| | package, V/P rating, 2× derate | tol class | catalogue ∩ ratings | enumerate series | Physics2 DC / analytical |
| LED series R | R discrete | I_led in [1,20] mA | power on R, Vf window | prefer mid current | R>(Vin−Vf)/Imax | enumerate | LED analytical |
| RC cutoff | R,C discrete | min \|fc−fc*\| | ratings | cost | positive R,C | nested E-series | fc=1/(2πRC) |
| Bind MPN | MPN choice | min cost (real table) | derating floors | package match | DB candidates | score sort | ratings checks |
| Height DFM | footprint | feasibility | max height | — | profile | filter | vendor height rule |
| BOM cost | MPN set | Σ unit_cost | electrical+DFM | availability | product of candidates | greedy / ILP later | re-verify |
| Thermal margin | derate factor | max margin | P≤Pmax | ambient | rating>0 | scale | power calc |
| Board area | packages | min Σ area | DFM clearance (future) | — | catalogue dims | greedy | geometry engine |

Deterministic math should replace: MPN substring cost heuristic, Gemini recipe topology as “optimization.”
