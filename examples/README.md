# Feature song catalog

Each catalog entry demonstrates one tracker effect with one focused `.song`
file. Keep examples in `examples/<feature-id>/`, and name the song
`<feature-id>.song`. Add one leaf under `Effects` in `catalog/navigation.xml`,
pointing it at `app.html?init_song=...&amp;init_play=1` and its guide. Every
catalog example should autoplay on load. If an effect has several examples,
use a parent titled `Effect name (CODE)` and nest the demonstrations below it.
Sort the effect parents alphabetically by effect code. For a single example,
the leaf itself may carry that title.

When non-effect demonstrations are added, put them under a separate `Features`
group; do not give them effect-code suffixes.

Feature songs can share the four demo WAVs already embedded by the app, using
paths such as `../../samples/tgndshtn-part1.wav`. The shared
`samples/sine-c4.wav` is available for pitch-focused examples. Do not copy WAVs
into each example folder. If a new shared sample is needed, add it under
`samples/` and to the embedded `SAMPLE_WAVS` list in the Makefile.
The `make catalog` target embeds every `examples/*/*.song` file at the
same path in the generated app and packages the catalog into
`build/emscripten/catalog/`, ready to upload as a directory.

For each new feature, include:

- one song that demonstrates the behavior clearly and starts playing from the
  beginning;
- a short `README.md` explaining what to listen for, the relevant rows and
  effects, and the intended playback settings;
- one navigation entry grouped by feature.

Keep each song narrow in scope. Prefer one pattern and only as many rows as
the demonstration needs. Verify its `.song` file loads by running the native
app with `INIT_SONG=examples/<feature-id>/<feature-id>.song`. Set
`INIT_PLAY=1` to start automatically; otherwise startup is silent. Catalog
links set it for examples. Build the website catalog with `make catalog-wasm`.
