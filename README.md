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

## Licence

Apache-2.0, see [LICENSE](LICENSE) and [NOTICE](NOTICE).
