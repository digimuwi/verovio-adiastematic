# Scribal ductus for calligraphic neumes

Notes on the "ductus" work layered onto the adiastematic (staffless) calligraphic neume
renderer in `CalligraphicNeume` / `View::DrawNeumeAdiastematic`. Captures *why* the code is
shaped the way it is, so the intent survives the next reading.

## Starting point

The renderer is an idealised **broad-edge pen**: a nib held at one fixed angle for the whole
hand, swept along each component's centreline, with width

```
w(i) = base_nib(direction) · scale
       └── |T × n̂| law ──┘
```

The width is **uniform along each stroke** — the fixed nib alone gives the thick/thin (thin
where the stroke runs along the nib edge, broad across it). Geometrically honest, and after
experiment (below) deliberately left that way. "Ductus" is the lived execution a pen machine
otherwise throws away; two orthogonal, fully-deterministic factors were explored on top:

```
A  attack & release   →  a WIDTH change     (an along-stroke weight envelope)
I  italic slant       →  a POSITION change  (an anisotropic shear of the centrelines)
```

**A was tried and removed** (see below); only **I**, the slant, is live — it touches position,
not weight, and reads nothing from the ink width.

## A — attack & release (tried, then removed)

**Removed.** An along-stroke weight envelope — the pen landing firm, lifting to a hairline, and its
ink easing over a long gesture — was built, iterated three times, and then taken back out. Strokes are
now a uniform broad-nib width (only the `|T × n̂|` direction law shapes them). This note keeps the
attempt on record so it is not naively re-tried.

The idea was to multiply the width by a `cap ∈ (0, 1]` along the stroke, so a mark lands blunt, holds
a body, and lifts to a fine tail — the "written, not computed" look. The cuts:

1. **Fractional envelope** — attack/release as fractions of the run. Read well on a long ligature but
   made a short `rellen="s"` tractulus (~17 pen px) *all taper*: a heavy head and a long dying whisker,
   a "tadpole". Shrinking the fraction only trades the short case against the long one.

2. **Absolute landing/lift** — measure the transients in fixed ink (a hand lands and lifts the same
   however long the stroke). Better in principle, but the lone tractulus *still* came out a wedge, for
   two reasons a densely-sampled ligature had hidden: a lone straight stroke is only three points and
   `InkRun`'s single-seg path never densified it, so the envelope had nowhere to sit; and the lift always
   ran to a hairline, wrong for a short quick stroke that really ends in a blunt stub.

3. **Absolute + length-tempered** — densify sparse single strokes, cap the lift *depth* by length
   (short ⇒ blunt stub, not a hairline), pace the easing in absolute ink too. This finally made the
   tractulus an even little bar with a blunt end — a genuine fix.

And yet, side by side, the **plain uniform broad-nib stroke simply looked better** — cleaner, calmer,
less fussy — so the whole envelope (and the `taperStart` / `taperEnd` plumbing, and the single-stroke
densification added only to feed it) was removed. The lesson worth keeping: a terminal taper needs room
to develop (land · body · lift), and a short adiastematic stroke has no such room, so any along-stroke
weighting fights the mark instead of decorating it. If ever revisited it must be *earned by length* and
never applied to short strokes — but the uniform baseline is the current, deliberate choice.

Only **I** (the slant) remains as a live ductus factor.

## I — italic slant (anisotropic, "true italic")

A right hand pulls the pen toward the **upper-right**. In pen space (+x right, **+y down**) that
is the shear `x' = x − s·y` with `s = tan(slant)`: points above the baseline (y < 0) move right,
points below it drift left.

Two deliberate choices:

1. **True italic, not oblique.** The shear is applied to the **centrelines before the nib is
   swept**, so the fixed nib meets the leaned strokes at new angles and the thick/thin
   redistributes — exactly what a constant-angle pen does on a slant. (Shearing the *finished*
   ink would skew the apparent nib angle and turn puncta into parallelograms — the "fake italic"
   look. We rejected that.)

2. **Anisotropic pull.** A uniform shear over-leans firm downstrokes. Worked example — a clivis
   `nc1=ne` (rise) then `nc2=s` (fall), s≈0.21:

   | point             | height y | x-pull       |
   |-------------------|----------|--------------|
   | ne peak (top)     | −9       | +1.9 (right) |
   | south foot (bot.) | +17      | −3.6 (left)  |

   The firm descent's foot is pulled left *more* than the flick's peak is pulled right — drunk,
   not scribal. So the lean strength is modulated by how aligned the stroke is with the up-right
   **pull axis** `û = (1, −1)/√2`:

   ```
   a        = T · û                     // alignment in [−1, 1]
   strength = 1 − bias · (1 − a) / 2    // a=+1 → full lean, a=−1 → (1 − bias)
   x'       = x − strength · tan · y
   ```

   An up-right flick (a≈+1) leans fully; a firm descent (a≈−0.71) leans by ~half; a directionless
   dab takes the neutral middle. Raising `bias` toward 1 makes the clivis foot pull drop below the
   peak's — the intuition we were chasing.

### Why it stays seamless and deterministic

The modulation is computed **per-point on the already-densified spine**, where the sharp clivis
corner has been smoothed into continuously-varying tangents. So `strength` ramps smoothly across
the joint — no tear. Every term is a pure function of the MEI-derived centreline; there is **no
randomness**, so output is byte-identical given a fixed `--xml-id-seed`.

## Where it lives

| concern                | location |
|------------------------|----------|
| envelope + slant math  | anonymous namespace in `src/calligraphicneume.cpp` (`SmoothStep`, `SlantStrength`, `SlantShear`, `SlantApply`, the `ATTACK_*`/`RELEASE_*` constants, `PULL_AXIS`) |
| envelope application    | `NibEdges` width line |
| slant application       | `InkRun` — the dense spine (multi-seg), the single-seg centreline, the punctum dab; plus the episema accent in `Build` step 3 (so it rides the same lean, carrying its anchor across to the leaned stroke end) |
| public surface          | `struct Slant { double tan; double bias; bool IsNone(); }` and the `Build(…, Slant)` param in `include/vrv/calligraphicneume.h` |
| options                 | `neumeCalligraphicSlant` (degrees, default **10**, range −30…30) and `neumeCalligraphicSlantBias` (default **0.3**, range 0…1), registered under the neume group |
| plumbing                | `View::DrawNeumeAdiastematic` reads the options, converts degrees → tan, passes a `Slant` to `Build` |

Everything is behind the existing `neumeCalligraphic` opt-in, so non-calligraphic neume output
is unchanged.

## The pen as a parameter (additive, for synthetic-data variation)

`CalligraphicNeume::Pen { angleDeg, width, thin, episema }` parameterizes what used to be the
`NIB_ANGLE / NIB_W / NIB_MIN / EPISEMA_NIB` constants (the punctum dab footprint scales with
`width`). A `Build(ncs, scale, slant, pen)` overload takes it; the old three-arg `Build` and
the verovio render path use `Pen{}`. When first introduced, `Pen{}` reproduced the historical
constants **byte-identically** (verified against the pre-patch binary); the default nib angle has
since been re-tuned to match the ductus repo's standard hand — **130°** (== −50°, the historic
−45° == 135° steepened by 5°), tuned by eye against the manuscript in its Nib Lab (2026-07-22)
and used as the centre of its synthetic-corpus pen sampling (`sample_pen`: nib ~N(130°, 12)).
`width/thin/episema` keep the historical 6.0 / 1.8 / 0.55 — the corpus varies them but centres
on exactly these. Only the nib's axis matters (`|T x n̂|`), so `angleDeg` and `angleDeg ± 180`
are the same pen. The overload exists so the ductus training-data generator (`ductus_dump2` in
the ductus repo, via `@` pen-directive lines) can vary the hand — nothing in the rendering path
reads anything but the default.

## Deliberately not done (parked)

- **B — pressure / gravity** (down-biased weight, weighted feet at turns).
- **D — terminal furniture** (heads, feet, hooks; punctum asymmetry).
- **C / E — pen-roll and seeded micro-tremor** (the latter only ever via a stable id/index hash,
  never a clock or RNG, to preserve reproducibility).

If revisited, keep the "weight is a product of orthogonal factors" framing:
`w = base_nib · pressure · envelope · scale`.
