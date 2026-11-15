# RBS editor icons

Unmodified SVG sources: Lucide **0.468.0**, https://github.com/lucide-icons/lucide/tree/0.468.0/icons.
ISC / Feather MIT notices: [lucide/LICENSE](lucide/LICENSE).
No Hazel branding or image assets are included.

Order: mouse-pointer-2, move-3d, rotate-3d, scaling, play, pause, rotate-ccw,
sliders-horizontal, camera, lightbulb, box, trees, component. The final five are
shared by Hierarchy entities and presentation groups. All use the upstream
24-unit canvas, 2-unit round stroke.
The 20/25/30/40 pixel glyph atlases correspond to 100/125/150/200% UI scale.
Each cell has a two-pixel transparent gutter. The runtime tints white glyphs.

Rebuild with `tools/bake_rbs_icons.cpp` (MSVC C++17 or newer), then pass this
directory as its sole argument. NanoSVG is pinned at
`239e102ec2c691f2902e20ace2ed36ee4a35cfe6` in `tools/vendor/nanosvg` (zlib license).
The baker is an offline development tool; the editor loads cached atlases through
the existing asset registry and never parses SVG at runtime.


Camera / Light extension: Lucide sun.svg uses the same pinned 0.468.0 sources and
license. `custom/stage-spotlight.svg` is original GEngine artwork, inspired by studio
lighting fixtures and Unity Scene View's icon language; it contains no Unity assets.
It uses a barrel housing, circular front aperture, mounting yoke and short light rays.
The enum/atlas appends sun and stage-spotlight after component. Existing glyph order
is retained. Additional 28/35/42/56 pixel atlases serve 28 logical-pixel billboards;
20/25/30/40 remain the compact toolbar/Hierarchy sizes. All are baked offline with
transparent gutters and loaded by the existing asset registry. Runtime light tint:
#E6C477, camera tint #D7E2EF. Reference-only Unity illustrations live in campaign
logs, never in the runtime asset package.

Editor views follow-up: eye.svg and eye-off.svg are unmodified Lucide 0.468.0 assets
under the same notices. Three original studio-light designs are retained as
custom/stage-spotlight.svg (B, barrel/lens/yoke), stage-spotlight-a.svg (angled fixture)
and stage-spotlight-c.svg (round housing). The atlas appends eye, eye-off, A and C;
existing enum indices remain stable. Billboard defaults are 40 logical pixels and
44 for Spot, with a 75-200% preference. Tight-fit 44/55/66/88/110/132/176 rasters
cover the requested size and DPI range without runtime parsing or texture creation.
Artwork selection remains available for the Owner's three-design comparison.
Actual matched Viewport comparisons selected A (angled fixture) as the default.
The Hierarchy uses the same chosen artwork; B and C remain available for review.
