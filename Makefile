EMSDK_ENV := ../emsdk/emsdk_env.sh
BUILD_PLATFORM ?= macos
PLATFORM_BUILD := build/$(BUILD_PLATFORM)
TT_BIN := $(PLATFORM_BUILD)/bin/turbo-tracker
TT_TEST_CSV_BIN := $(PLATFORM_BUILD)/bin/test-tt-csv
TT_TEST_PATTERN_BIN := $(PLATFORM_BUILD)/bin/test-tt-pattern
TT_TEST_SONG_BIN := $(PLATFORM_BUILD)/bin/test-tt-song
TT_TEST_TIMELINE_BIN := $(PLATFORM_BUILD)/bin/test-tt-timeline
TT_TEST_AUDIO_BIN := $(PLATFORM_BUILD)/bin/test-tt-audio
TT_TEST_EDIT_BIN := $(PLATFORM_BUILD)/bin/test-tt-edit

# f80 library module (maintained separately); sources unchanged, f80_* namespace
F80_CORE_NATIVE_OBJS := $(PLATFORM_BUILD)/obj/f80-core/f80.o \
	$(PLATFORM_BUILD)/obj/f80-core/f80_components.o \
	$(PLATFORM_BUILD)/obj/f80-core/f80_file_dialog.o
F80_CORE_WASM_OBJS := build/emscripten/obj/f80-core/f80.o \
	build/emscripten/obj/f80-core/f80_components.o

# turbo-tracker application (our code, tt_ namespace)
TT_APP_NATIVE_OBJS := $(addprefix $(PLATFORM_BUILD)/obj/tt-app/,tt_sample.o tt_csv.o tt_pattern.o tt_song.o tt_edit.o tt_play.o tt_audio.o turbo_tracker.o)
TT_APP_WASM_OBJS := $(addprefix build/emscripten/obj/tt-app/,tt_sample.o tt_csv.o tt_pattern.o tt_song.o tt_edit.o tt_play.o tt_audio.o turbo_tracker.o)

SAMPLE_WAVS := samples/tgndshtn-part1.wav samples/tgndshtn-part2.wav \
	samples/tgndshtn-part3.wav samples/tgndshtn-part4.wav samples/sine-c4.wav
SAMPLE_DEFAULT_SONG := default.song
SAMPLE_FEATURE_SONGS := $(wildcard examples/*/*.song)
WASM_SRC := wasm
WEB_DIR := build/emscripten/www
WEB_TT_JS := $(WEB_DIR)/turbo-tracker/turbo-tracker.js
CATALOG_DIR := build/emscripten/catalog
ITCH_DIR := build/emscripten/itch
SDL_CFLAGS := $(shell pkg-config --cflags sdl3)
SDL_LIBS := $(shell pkg-config --libs sdl3)
SDL2_CFLAGS := $(shell pkg-config --cflags sdl2)
SDL2_LIBS := $(shell pkg-config --libs sdl2)
SDL3_CONTROLLER_DIAG := $(PLATFORM_BUILD)/bin/sdl3-controller-diagnostic
SDL2_CONTROLLER_DIAG := $(PLATFORM_BUILD)/bin/sdl2-controller-diagnostic

.PHONY: native wasm test serve clean check-emsdk itch catalog catalog-wasm controller-diagnostic

name:
	@echo "turbo-tracker"

native: $(TT_BIN)

test: $(TT_TEST_CSV_BIN) $(TT_TEST_PATTERN_BIN) $(TT_TEST_SONG_BIN) $(TT_TEST_TIMELINE_BIN) $(TT_TEST_AUDIO_BIN) $(TT_TEST_EDIT_BIN)
	$(TT_TEST_CSV_BIN)
	$(TT_TEST_PATTERN_BIN)
	$(TT_TEST_SONG_BIN)
	$(TT_TEST_TIMELINE_BIN)
	$(TT_TEST_AUDIO_BIN)
	$(TT_TEST_EDIT_BIN)

controller-diagnostic: $(SDL3_CONTROLLER_DIAG) $(SDL2_CONTROLLER_DIAG)

$(SDL3_CONTROLLER_DIAG): src/sdl_controller_diagnostic.c
	mkdir -p $(dir $@)
	cc -std=c11 -Wall -Wextra -O2 $(SDL_CFLAGS) $< $(SDL_LIBS) -o $@

$(SDL2_CONTROLLER_DIAG): src/sdl2_controller_diagnostic.c
	mkdir -p $(dir $@)
	cc -std=c11 -Wall -Wextra -O2 $(SDL2_CFLAGS) $< $(SDL2_LIBS) -o $@

wasm: check-emsdk web-assets $(WEB_TT_JS)

catalog: catalog-wasm

catalog-wasm: wasm
	rm -rf $(CATALOG_DIR)
	mkdir -p $(CATALOG_DIR)/turbo-tracker $(CATALOG_DIR)/examples
	cp -R catalog/. $(CATALOG_DIR)/
	cp -R wasm/. $(CATALOG_DIR)/
	cp $(WEB_TT_JS) $(CATALOG_DIR)/turbo-tracker/
	cp $(WEB_DIR)/turbo-tracker/turbo-tracker.wasm $(CATALOG_DIR)/turbo-tracker/
	cp -R examples/. $(CATALOG_DIR)/examples/

# ---- f80 library module (kept as-is, f80_* namespace) ----
$(PLATFORM_BUILD)/obj/f80-core/f80.o: src/f80.c include/f80.h
	mkdir -p $(dir $@)
	cc -std=c11 -Wall -Wextra -O2 -Iinclude $(SDL_CFLAGS) -c $< -o $@

$(PLATFORM_BUILD)/obj/f80-core/f80_components.o: src/f80_components.c include/f80_components.h include/f80.h
	mkdir -p $(dir $@)
	cc -std=c11 -Wall -Wextra -O2 -Iinclude -c $< -o $@

$(PLATFORM_BUILD)/obj/f80-core/f80_file_dialog.o: src/f80_file_dialog.c include/f80_file_dialog.h include/f80.h
	mkdir -p $(dir $@)
	cc -std=c11 -Wall -Wextra -O2 -Iinclude -c $< -o $@

WASM_CCOPTS := -std=c11 -Wall -Wextra -O3 -flto -msimd128 -Iinclude/web_curses -Iinclude
WASM_LDOPTS := -O3 -flto -msimd128

build/emscripten/obj/f80-core/f80.o: src/f80.c include/f80.h
	mkdir -p $(dir $@) build/em-cache
	. $(EMSDK_ENV) >/dev/null && EM_CACHE=$(CURDIR)/build/em-cache \
		emcc $(WASM_CCOPTS) -c $< -o $@

build/emscripten/obj/f80-core/f80_components.o: src/f80_components.c include/f80_components.h include/f80.h
	mkdir -p $(dir $@) build/em-cache
	. $(EMSDK_ENV) >/dev/null && EM_CACHE=$(CURDIR)/build/em-cache \
		emcc $(WASM_CCOPTS) -c $< -o $@

# ---- turbo-tracker application (tt_ namespace) ----
$(PLATFORM_BUILD)/obj/tt-app/%.o: src/%.c $(wildcard src/*.h) include/f80.h
	mkdir -p $(dir $@)
	cc -std=c11 -Wall -Wextra -O2 -Iinclude $(SDL_CFLAGS) -c $< -o $@

$(PLATFORM_BUILD)/obj/tt-app/turbo_tracker.o: include/f80_file_dialog.h

$(TT_BIN): $(F80_CORE_NATIVE_OBJS) $(TT_APP_NATIVE_OBJS)
	mkdir -p $(dir $@)
	cc $^ -lncurses $(SDL_LIBS) -lm -o $@

build/emscripten/obj/tt-app/%.o: src/%.c $(wildcard src/*.h) include/f80.h
	mkdir -p $(dir $@) build/em-cache
	. $(EMSDK_ENV) >/dev/null && EM_CACHE=$(CURDIR)/build/em-cache \
		emcc $(WASM_CCOPTS) -sUSE_SDL=3 -c $< -o $@

$(WEB_TT_JS): build/emscripten/obj/f80-core/f80.o $(F80_CORE_WASM_OBJS) $(TT_APP_WASM_OBJS) $(SAMPLE_WAVS) $(SAMPLE_DEFAULT_SONG) $(SAMPLE_FEATURE_SONGS) wasm/init_song.js
	mkdir -p $(dir $@) build/em-cache
	. $(EMSDK_ENV) >/dev/null && EM_CACHE=$(CURDIR)/build/em-cache \
		emcc $(filter %.o,$^) -lm -sUSE_SDL=3 -s FORCE_FILESYSTEM=1 \
		-s ALLOW_MEMORY_GROWTH=1 -s EXIT_RUNTIME=0 $(WASM_LDOPTS) \
		$(foreach wav,$(SAMPLE_WAVS),--embed-file $(wav)@/$(wav)) \
		--embed-file $(SAMPLE_DEFAULT_SONG)@/default.song \
		$(foreach song,$(SAMPLE_FEATURE_SONGS),--embed-file $(song)@/$(song)) \
		--pre-js wasm/init_song.js \
		-s EXPORTED_FUNCTIONS=_main,_tt_tracker_load_wav,_tt_tracker_recover_wav,_tt_tracker_load_song,_tt_tracker_save_song,_tt_tracker_audio_recover,_tt_tracker_begin_touch_selection,_tt_tracker_touch_scroll,_tt_tracker_touch_scroll_fields,_tt_tracker_scroll_order,_tt_tracker_hold_order_selection,_tt_tracker_drag_order_selection,_tt_tracker_scroll_table,_tt_tracker_scroll_table_horizontal,_tt_tracker_scroll_table_actions,_tt_tracker_hold_table_selection,_tt_tracker_drag_table_selection,_tt_tracker_drag_selection,_tt_tracker_begin_wave_gesture,_tt_tracker_pan_wave_touch,_tt_tracker_hold_wave_marker,_tt_tracker_move_wave_marker,_tt_tracker_flush_input,_curses_push_key,_curses_push_mouse,_curses_push_resize \
		-s EXPORTED_RUNTIME_METHODS=ccall,UTF8ToString,FS -o $@

# ---- tests ----
$(TT_TEST_CSV_BIN): src/test_csv.c $(PLATFORM_BUILD)/obj/tt-app/tt_sample.o $(PLATFORM_BUILD)/obj/tt-app/tt_csv.o
	mkdir -p $(dir $@)
	cc -std=c11 -Wall -Wextra -O2 -Iinclude $(SDL_CFLAGS) $^ $(SDL_LIBS) -lm -o $@

$(TT_TEST_PATTERN_BIN): src/test_pattern.c $(PLATFORM_BUILD)/obj/tt-app/tt_sample.o $(PLATFORM_BUILD)/obj/tt-app/tt_pattern.o
	mkdir -p $(dir $@)
	cc -std=c11 -Wall -Wextra -O2 -Iinclude $(SDL_CFLAGS) $^ $(SDL_LIBS) -lm -o $@

$(TT_TEST_SONG_BIN): src/test_song.c $(PLATFORM_BUILD)/obj/tt-app/tt_sample.o $(PLATFORM_BUILD)/obj/tt-app/tt_pattern.o $(PLATFORM_BUILD)/obj/tt-app/tt_song.o
	mkdir -p $(dir $@)
	cc -std=c11 -Wall -Wextra -O2 -Iinclude $(SDL_CFLAGS) $^ $(SDL_LIBS) -lm -o $@

$(TT_TEST_TIMELINE_BIN): src/test_timeline.c $(F80_CORE_NATIVE_OBJS) $(filter-out $(PLATFORM_BUILD)/obj/tt-app/turbo_tracker.o,$(TT_APP_NATIVE_OBJS))
	mkdir -p $(dir $@)
	cc -std=c11 -Wall -Wextra -O2 -Iinclude $(SDL_CFLAGS) $< $(F80_CORE_NATIVE_OBJS) $(filter-out $(PLATFORM_BUILD)/obj/tt-app/turbo_tracker.o,$(TT_APP_NATIVE_OBJS)) -lncurses $(SDL_LIBS) -lm -o $@

$(TT_TEST_AUDIO_BIN): src/test_audio_effects.c $(PLATFORM_BUILD)/obj/tt-app/tt_sample.o
	mkdir -p $(dir $@)
	cc -std=c11 -Wall -Wextra -O2 -Iinclude $(SDL_CFLAGS) $^ $(SDL_LIBS) -lm -o $@

$(TT_TEST_EDIT_BIN): src/test_edit.c $(PLATFORM_BUILD)/obj/tt-app/tt_edit.o $(PLATFORM_BUILD)/obj/tt-app/tt_sample.o $(PLATFORM_BUILD)/obj/tt-app/tt_song.o $(PLATFORM_BUILD)/obj/tt-app/tt_pattern.o
	mkdir -p $(dir $@)
	cc -std=c11 -Wall -Wextra -O2 -Iinclude $(SDL_CFLAGS) $^ $(SDL_LIBS) -lm -o $@

# ---- web + itch ----
check-emsdk:
	test -f $(EMSDK_ENV)

web-assets:
	mkdir -p $(WEB_DIR)
	cp -R $(WASM_SRC)/. $(WEB_DIR)/

itch: wasm
	python3 scripts/package_itch.py $(WEB_DIR) turbo-tracker $(ITCH_DIR)/turbo-tracker.zip

serve:
	cd $(WEB_DIR) && python3 -m http.server 8001

clean:
	rm -rf build
