# Neumes on lines: test set and calibration

Regression and calibration files for calligraphic neumes on a staff (`--neume-calligraphic`), one MEI
file per notation family. Each file reproduces the measured facsimile examples of the 2026-10-27 review
(`examples.csv` of the facsimile set). The pitches are those of the examples: 12th-13th century melodies
as the review established them, not an edition of any manuscript. The files are synthetic. No image,
transcription or private MEI is included.

## Conventions

- `staffDef/@notationsubtype` names the hand (`hildegard`, `german`, `hufnagel`, ...).
- Every syllable that reproduces an example has a comment with the example id and what it shows. Every
  `nc` has `xml:id="<example>-n<k>"`, where k is the note number of the example's pitch points.
  `<family>-x<k>` ids are synthetic extras, not measured, added to show the hand.
- Every `nc` carries explicit shape attributes (`@tilt`, `@curve`, `@rellen`, `@con`, `@s-shape`,
  `@angled`, `@place`, `@ligated`, `<liquescent>`, `<quilisma>`, `<oriscus>`). An `nc` with none is a
  dot. Shapes are never inferred from `neume/@type`.
- Guidonian letters map to MEI as follows: Γ = g2, A B = a2 b2, C-G = c3-g3, a b = a3 b3, c-g = c4-g4
  (small c is middle C).
- Each file has one `staffDef`, because verovio ignores a second `scoreDef` inside `score`. A clef
  change inside the layer handles a moved clef.
- `<sb/>` separates the groups. The breaks show only with `--breaks encoded` or `--breaks line`.

## Files

| file | hand | reproduces | notes |
|---|---|---|---|
| `hildegard.mei` | hildegard | hildegard-01 to -17 | Dendermonde and Riesencodex. Clef moves in -03/-04, -07 and -11 |
| `german.mei` | german | german-01 to -06 | Köln 1161 |
| `hufnagel.mei` | hufnagel | hufnagel-01 to -05, extras | -05 (Stams, 5 lines) is set on 4 lines with c on line 2, so e and g stay on lines |
| `messine.mei` | messine | messine-01 to -04, extras | Bellelay |
| `messine-german.mei` | messine-german | messine-german-01, -11 to -16, extras | Berlin 40078; -01 is a later Hungarian fragment |
| `cistercian.mei` | cistercian | cistercian-01 to -03, extras | Rein, 5 lines. -03 is a custos (`<custos>`) after gau- |
| `aquitanian.mei` | aquitanian | aquitanian-01 to -03, extras | One line (`lines="1"`), which is G: an invisible G clef with `dis="8"` below |
| `beneventan.mei` | beneventan | beneventan-01, -03, extras | F clef. The pitches of -01 are relative |
| `central-italian.mei` | central-italian | central-italian-01, -02, extras | Bodmer 74 |
| `nonantolan.mei` | nonantolan | nonantolan-01 to -03, extras | |
| `square.mei` | square | square-01 to -03, extras | The oblique is `@ligated="true"` on both ends |
| `mei-listing-283.mei` | hildegard | MEI Guidelines Listing 283 | See the next section |

Left out (staff furniture with no pitch points): hildegard-07, -11, -12 and -13 keep their notes, but
the red f stroke drawn through a space is not encoded. hufnagel-06 (g, c and F letters stacked; MEI has
one clef per staff), messine-german-02 (a b clef; MEI has none, and the page is too faded) and
beneventan-02 (a red line above the text with no established pitch) are omitted. The custodes of
aquitanian-02, beneventan-01, cistercian-03 and square-03 are encoded as `<custos>`. They are not
`nc`s, so they are not calibrated.

## MEI Guidelines Listing 283

`mei-listing-283.mei` is Listing 283 of the MEI Guidelines v5 (section 6.4.10 "Basic Encoding –
Syllable"), the opening of Hildegard's *O splendidissima gemma*. The source is
`source/examples/neumes/neumes-sample169.txt` in
[music-encoding/music-encoding](https://github.com/music-encoding/music-encoding/blob/v5.1/source/examples/neumes/neumes-sample169.txt),
tag v5.1. It is copyright the MEI Board and licensed under the Educational Community License 2.0, the
licence of that repository (its `LICENSE` file and README). The Guidelines sources carry the same
notice. ECL 2.0 permits redistribution with the notice and a copy of the licence, so the file carries
an attribution and licence comment, and the licence text is in `LICENSE-ECL-2.0.txt`.

Changes, also listed in the file: the `<music>` root is wrapped in `<mei>` with a minimal `meiHead`,
and a `scoreDef` is added (5 lines, `notationsubtype="hildegard"`, C clef on line 5, following the
Guidelines' note that it was encoded on a 5-line staff with C and F clefs on lines 5 and 3). No pitch
or shape was changed. The second note, `d` in octave 2, is probably a typo for octave 3. It is kept as
published.

## Calibration

`calibration.csv` has one row per check on a measured pitch point:

- `nc_id`, `family`, `example` and `pitch` (MEI pname+oct).
- `pitch_point`: what the scribe's pitch point is.
- `check` (see below), `target_steps` and `tol_steps`.
- `facsimile_offset_steps`: where the scribe's point sits relative to the nominal line or space in the
  crop, positive above. This is for reference only.
- `source`: which pitch point or note of `examples.csv` the row comes from.

A step is half a line distance. The pixel numbers of `examples.csv` were converted with each crop's own
line spacing.

| check | measured |
|---|---|
| `centre` | middle of the nc's ink, offset from the pitch (dots, squares, an S-oriscus waist) |
| `head` | top of the ink lowered by half a nib, offset from the pitch (virga head, arch top, pes head) |
| `foot` | bottom of the ink raised by half a nib, offset from the pitch (clivis end, pes foot, quilisma belly) |
| `extent_above` | how far the ink reaches above the pitch (flag, corner, epiphonus overshoot) |
| `extent_below` | how far the ink reaches below the pitch (virga stem, cephalicus overshoot, curl end) |

Half a nib is 0.23 step (3 of the 13 pen px per step). A connected gesture is cut into per-nc slices
that overlap by a few knots, so the checker drops the overlap (`--margin-knots`, default 4, the
`kMargin` of `CalligraphicNeume::InkRun`). Tolerances are 0.25-0.4 step, depending on how precise the
source is.

Run it with Python 3 (standard library only):

```sh
python3 doc/neumes-on-lines/calibration.py                                  # build/verovio
python3 doc/neumes-on-lines/calibration.py --verovio /path/to/verovio --family hildegard german
python3 doc/neumes-on-lines/calibration.py --fails-only --keep-svg /tmp/svg
```

The script renders each family file with `--neume-calligraphic --xml-id-seed 1 -r data`, reads each
nc's `<g id>` and the staff lines of its own staff segment from the SVG, and prints PASS or FAIL per row
with the measured value. It exits with 1 if any row fails. Some rows are expected to fail for a while:
the targets are what the facsimiles measure, not what the renderer does now.

Results on 2026-10-27, 238 rows:

| binary | pass | fail |
|---|---|---|
| PR #1 head (`vrv-base`, 85fa4c4) | 172 | 66 |
| this branch, build of 09:48 (85fa4c4-dirty, hands and landing rules) | 189 | 49 |
