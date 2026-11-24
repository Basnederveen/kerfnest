# kerfnest

Deterministic 2D irregular nesting for sheet-metal parts: it places flat patterns (lines, arcs,
circles, holes) on stock sheets, uses the least sheet area, and puts small parts inside the
holes of large ones.

- **Exact and safe.** All solver geometry is integer (1 unit = 1 µm, [Clipper2](https://github.com/PolyClipping/Clipper2)).
  Parts are approximated only outward, so a result never puts two parts closer than the spacing.
- **Deterministic.** Same input, same build: byte-identical placements, at any thread count and
  any machine load. The search runs on a work budget, never on a clock.
- **Refuses instead of guessing.** A part that fits no sheet, an open contour or a run out of
  budget is a typed refusal, never a partial nest.
- **Rotation only.** Allowed angles are whole degrees; parts are never mirrored.

Status: early. Sheet-by-sheet beam fill over no-fit polygons, then a deterministic local search on
the part order and sheet sizes. Fill density on small sheet counts is the known weak spot.

## Build

```sh
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

C++17. Dependencies are fetched by CMake at pinned versions: Clipper2 (BSL-1.0); for the tests,
doctest (MIT); for `kerfnest-cli`, nlohmann/json (MIT).

## Example

[`examples/flange-and-washers.json`](examples/flange-and-washers.json): three 200×200 flanges
with a Ø120 hole and eight Ø80 washers on one 1000×500 sheet, 5 mm spacing, 10 mm margin.

```sh
build/Release/kerfnest-cli examples/flange-and-washers.json result.json
```

Everything lands on one sheet, and three of the washers sit inside the flange holes:

```json
{
 "placements": [
  { "partId": "flange", "sheet": 0, "x": 10.0,    "y": 10.0,   "angleDeg": 0 },
  { "partId": "washer", "sheet": 0, "x": 110.282, "y": 95.019, "angleDeg": 90 },
  ...
 ],
 "sheetTypeIds": ["1000x500"]
}
```

A placement is the part's own contour rotated about its origin by `angleDeg`, then moved by
`(x, y)`. When there is no complete nest, the result is a refusal instead:
`{"refusal": {"code": "part_exceeds_all_sheets", "message": "..."}}`.

## Licence

Apache-2.0, see [LICENSE](LICENSE) and [NOTICE](NOTICE).
