EMSDK_ENV := ../emsdk/emsdk_env.sh
BUILD_PLATFORM ?= macos
PLATFORM_BUILD := build/$(BUILD_PLATFORM)
F80_SAMPLE_TRACKER_BIN := $(PLATFORM_BUILD)/bin/showcase-sample-tracker
F80_SAMPLE_TRACKER_TEST_BIN := $(PLATFORM_BUILD)/bin/test-sample-tracker-csv
F80_SAMPLE_PATTERN_TEST_BIN := $(PLATFORM_BUILD)/bin/test-sample-tracker-pattern
F80_SAMPLE_SONG_TEST_BIN := $(PLATFORM_BUILD)/bin/test-sample-tracker-song
F80_SAMPLE_TIMELINE_TEST_BIN := $(PLATFORM_BUILD)/bin/test-sample-tracker-timeline
F80_SAMPLE_AUDIO_TEST_BIN := $(PLATFORM_BUILD)/bin/test-sample-tracker-audio
SAMPLE_APP_NATIVE_OBJS := $(addprefix $(PLATFORM_BUILD)/obj/sample-tracker-app/,sample.o csv.o pattern.o song.o audio.o showcase.o file_dialog.o f80_components.o)
SAMPLE_APP_WASM_OBJS := $(addprefix build/emscripten/obj/sample-tracker-app/,sample.o csv.o pattern.o song.o audio.o showcase.o f80_components.o)
SAMPLE_DEMO_WAVS := tgndshtn-part1.wav tgndstn-part2.wav tgndstn-part3.wav tgndstn-part4.wav
SAMPLE_DEFAULT_SONG := default.song
WASM_SRC := wasm
WEB_DIR := build/emscripten/www
WEB_SAMPLE_TRACKER_JS := $(WEB_DIR)/showcases/sample-tracker/sample-tracker.js
ITCH_DIR := build/emscripten/itch
SDL_CFLAGS := $(shell pkg-config --cflags sdl3)
SDL_LIBS := $(shell pkg-config --libs sdl3)

.PHONY: f80-sample-tracker f80-sample-tracker-wasm f80-sample-tracker-test \
	itch-sample-tracker serve clean check-emsdk

f80-sample-tracker: $(F80_SAMPLE_TRACKER_BIN)

$(F80_SAMPLE_TRACKER_TEST_BIN): showcases/sample_tracker/test_csv.c $(PLATFORM_BUILD)/obj/sample-tracker-app/sample.o $(PLATFORM_BUILD)/obj/sample-tracker-app/csv.o
	mkdir -p $(dir $@)
	cc -std=c11 -Wall -Wextra -O2 -Iinclude $(SDL_CFLAGS) $^ $(SDL_LIBS) -lm -o $@

$(F80_SAMPLE_PATTERN_TEST_BIN): showcases/sample_tracker/test_pattern.c $(PLATFORM_BUILD)/obj/sample-tracker-app/sample.o $(PLATFORM_BUILD)/obj/sample-tracker-app/pattern.o
	mkdir -p $(dir $@)
	cc -std=c11 -Wall -Wextra -O2 -Iinclude $(SDL_CFLAGS) $^ $(SDL_LIBS) -lm -o $@

$(F80_SAMPLE_SONG_TEST_BIN): showcases/sample_tracker/test_song.c $(PLATFORM_BUILD)/obj/sample-tracker-app/sample.o $(PLATFORM_BUILD)/obj/sample-tracker-app/pattern.o $(PLATFORM_BUILD)/obj/sample-tracker-app/song.o
	mkdir -p $(dir $@)
	cc -std=c11 -Wall -Wextra -O2 -Iinclude $(SDL_CFLAGS) $^ $(SDL_LIBS) -lm -o $@

$(F80_SAMPLE_TIMELINE_TEST_BIN): showcases/sample_tracker/test_timeline.c showcases/sample_tracker/showcase.c $(PLATFORM_BUILD)/obj/tracker-app/f80.o $(filter-out $(PLATFORM_BUILD)/obj/sample-tracker-app/showcase.o,$(SAMPLE_APP_NATIVE_OBJS))
	mkdir -p $(dir $@)
	cc -std=c11 -Wall -Wextra -O2 -Iinclude $(SDL_CFLAGS) $< $(PLATFORM_BUILD)/obj/tracker-app/f80.o $(filter-out $(PLATFORM_BUILD)/obj/sample-tracker-app/showcase.o,$(SAMPLE_APP_NATIVE_OBJS)) -lncurses $(SDL_LIBS) -lm -o $@

$(F80_SAMPLE_AUDIO_TEST_BIN): showcases/sample_tracker/test_audio_effects.c $(PLATFORM_BUILD)/obj/sample-tracker-app/sample.o
	mkdir -p $(dir $@)
	cc -std=c11 -Wall -Wextra -O2 -Iinclude $(SDL_CFLAGS) $^ $(SDL_LIBS) -lm -o $@

f80-sample-tracker-test: $(F80_SAMPLE_TRACKER_TEST_BIN) $(F80_SAMPLE_PATTERN_TEST_BIN) $(F80_SAMPLE_SONG_TEST_BIN) $(F80_SAMPLE_TIMELINE_TEST_BIN) $(F80_SAMPLE_AUDIO_TEST_BIN)
	$(F80_SAMPLE_TRACKER_TEST_BIN)
	$(F80_SAMPLE_PATTERN_TEST_BIN)
	$(F80_SAMPLE_SONG_TEST_BIN)
	$(F80_SAMPLE_TIMELINE_TEST_BIN)
	$(F80_SAMPLE_AUDIO_TEST_BIN)

$(PLATFORM_BUILD)/obj/tracker-app/f80.o: src/f80.c include/f80.h
	mkdir -p $(dir $@)
	cc -std=c11 -Wall -Wextra -O2 -Iinclude $(SDL_CFLAGS) -c $< -o $@

$(PLATFORM_BUILD)/obj/sample-tracker-app/%.o: showcases/sample_tracker/%.c $(wildcard showcases/sample_tracker/*.h) include/f80.h
	mkdir -p $(dir $@)
	cc -std=c11 -Wall -Wextra -O2 -Iinclude $(SDL_CFLAGS) -c $< -o $@

$(PLATFORM_BUILD)/obj/sample-tracker-app/showcase.o: include/f80_file_dialog.h

$(PLATFORM_BUILD)/obj/sample-tracker-app/f80_components.o: src/f80_components.c include/f80_components.h include/f80.h
	mkdir -p $(dir $@)
	cc -std=c11 -Wall -Wextra -O2 -Iinclude -c $< -o $@

$(PLATFORM_BUILD)/obj/sample-tracker-app/file_dialog.o: src/f80_file_dialog.c include/f80_file_dialog.h include/f80.h
	mkdir -p $(dir $@)
	cc -std=c11 -Wall -Wextra -O2 -Iinclude -c $< -o $@

$(F80_SAMPLE_TRACKER_BIN): $(PLATFORM_BUILD)/obj/tracker-app/f80.o $(SAMPLE_APP_NATIVE_OBJS)
	mkdir -p $(dir $@)
	cc $^ -lncurses $(SDL_LIBS) -lm -o $@

check-emsdk:
	test -f $(EMSDK_ENV)

web-assets:
	mkdir -p $(WEB_DIR)
	cp -R $(WASM_SRC)/. $(WEB_DIR)/

build/emscripten/obj/tracker-app/f80.o: src/f80.c include/f80.h
	mkdir -p $(dir $@) build/em-cache
	. $(EMSDK_ENV) >/dev/null && EM_CACHE=$(CURDIR)/build/em-cache \
		emcc -std=c11 -Wall -Wextra -O2 -Iinclude/web_curses -Iinclude -c $< -o $@

build/emscripten/obj/sample-tracker-app/%.o: showcases/sample_tracker/%.c $(wildcard showcases/sample_tracker/*.h) include/f80.h
	mkdir -p $(dir $@) build/em-cache
	. $(EMSDK_ENV) >/dev/null && EM_CACHE=$(CURDIR)/build/em-cache \
		emcc -std=c11 -Wall -Wextra -O2 -Iinclude/web_curses -Iinclude -sUSE_SDL=3 -c $< -o $@

build/emscripten/obj/sample-tracker-app/f80_components.o: src/f80_components.c include/f80_components.h include/f80.h
	mkdir -p $(dir $@) build/em-cache
	. $(EMSDK_ENV) >/dev/null && EM_CACHE=$(CURDIR)/build/em-cache \
		emcc -std=c11 -Wall -Wextra -O2 -Iinclude/web_curses -Iinclude -c $< -o $@

$(WEB_SAMPLE_TRACKER_JS): build/emscripten/obj/tracker-app/f80.o $(SAMPLE_APP_WASM_OBJS) $(SAMPLE_DEMO_WAVS) $(SAMPLE_DEFAULT_SONG)
	mkdir -p $(dir $@) build/em-cache
	. $(EMSDK_ENV) >/dev/null && EM_CACHE=$(CURDIR)/build/em-cache \
		emcc $(filter %.o,$^) $(foreach wav,$(SAMPLE_DEMO_WAVS),--embed-file $(wav)@/$(wav)) --embed-file $(SAMPLE_DEFAULT_SONG)@/default.song -sUSE_SDL=3 -lm -s FORCE_FILESYSTEM=1 -s EXIT_RUNTIME=0 \
		-s EXPORTED_FUNCTIONS=_main,_sample_tracker_load_wav,_sample_tracker_recover_wav,_sample_tracker_load_song,_sample_tracker_save_song,_sample_tracker_begin_touch_selection,_sample_tracker_touch_scroll,_sample_tracker_touch_scroll_fields,_sample_tracker_scroll_order,_sample_tracker_scroll_table,_sample_tracker_scroll_table_horizontal,_sample_tracker_scroll_table_actions,_sample_tracker_hold_table_selection,_sample_tracker_drag_table_selection,_sample_tracker_drag_selection,_sample_tracker_begin_wave_gesture,_sample_tracker_pan_wave_touch,_sample_tracker_hold_wave_marker,_sample_tracker_move_wave_marker,_sample_tracker_flush_input,_curses_push_key,_curses_push_mouse,_curses_push_resize \
		-s EXPORTED_RUNTIME_METHODS=ccall,UTF8ToString,FS -o $@

f80-sample-tracker-wasm: check-emsdk web-assets $(WEB_SAMPLE_TRACKER_JS)

itch-sample-tracker: f80-sample-tracker-wasm
	python3 scripts/package_itch.py $(WEB_DIR) showcases/sample-tracker $(ITCH_DIR)/showcase-sample-tracker.zip

serve:
	cd $(WEB_DIR) && python3 -m http.server 8001

clean:
	rm -rf build
