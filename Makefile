# Night Heist - top-level build.
#
#   make desktop   SDL2 + OpenGL 3.3 build for fast iteration (build/desktop/nightheist)
#   make test      build and run the unit tests (desktop compiler, no SDL)
#   make wii       boot.dol via devkitPPC (needs DEVKITPPC set)
#   make wii-docker  same, inside the official devkitpro/devkitppc Docker image
#   make assets    regenerate generated sources/assets (font table, HBC icon)
#   make package   dist/apps/NightHeist/{boot.dol,meta.xml,icon.png} + zip
#   make clean

CC      ?= cc
PYTHON  ?= python3
DOCKER_IMAGE ?= devkitpro/devkitppc:latest

HOST_CFLAGS := -std=c11 -O2 -g -Wall -Wextra -Wpedantic -Wno-unused-parameter -Isrc
HOST_LIBS   := -lm

CORE_SRC    := $(wildcard src/core/*.c)
GAME_SRC    := $(wildcard src/game/*.c)
DESKTOP_SRC := $(wildcard src/platform/desktop/*.c)
TEST_SRC    := $(wildcard tests/*.c)

SDL_CFLAGS  := $(shell sdl2-config --cflags 2>/dev/null)
SDL_LIBS    := $(shell sdl2-config --libs 2>/dev/null)
ifeq ($(OS),Windows_NT)
GL_LIBS     := -lopengl32
else ifeq ($(shell uname -s),Darwin)
GL_LIBS     := -framework OpenGL
else
GL_LIBS     := -lGL
endif

DESKTOP_BUILD := build/desktop
TEST_BUILD    := build/test
DESKTOP_BIN   := $(DESKTOP_BUILD)/nightheist
TEST_BIN      := $(TEST_BUILD)/nightheist_tests

DESKTOP_OBJ := $(patsubst %.c,$(DESKTOP_BUILD)/%.o,$(CORE_SRC) $(GAME_SRC) $(DESKTOP_SRC))
TEST_OBJ    := $(patsubst %.c,$(TEST_BUILD)/%.o,$(CORE_SRC) $(GAME_SRC) $(TEST_SRC))
# Tests link the pure game sim but not the scene/renderer/app code.
TEST_OBJ    := $(filter-out $(TEST_BUILD)/src/game/app.o $(TEST_BUILD)/src/game/demo_scene.o \
                 $(TEST_BUILD)/src/game/text.o,$(TEST_OBJ))

DIST_DIR := dist/apps/NightHeist

.PHONY: all desktop test wii wii-docker assets package clean

all: desktop test

desktop: $(DESKTOP_BIN)

$(DESKTOP_BIN): $(DESKTOP_OBJ)
	@echo "link $@"
	@$(CC) $^ $(SDL_LIBS) $(GL_LIBS) $(HOST_LIBS) -o $@

$(DESKTOP_BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	@echo "cc $<"
	@$(CC) $(HOST_CFLAGS) $(SDL_CFLAGS) -DNH_PLATFORM_DESKTOP -MMD -MP -c $< -o $@

test: $(TEST_BIN)
	@$(TEST_BIN)

$(TEST_BIN): $(TEST_OBJ)
	@echo "link $@"
	@$(CC) $^ $(HOST_LIBS) -o $@

$(TEST_BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	@echo "cc $<"
	@$(CC) $(HOST_CFLAGS) -DNH_PLATFORM_TEST -MMD -MP -c $< -o $@

wii:
	@$(MAKE) --no-print-directory -f Makefile.wii

wii-docker:
	docker run --rm -u $$(id -u):$$(id -g) -v "$(CURDIR)":/src -w /src $(DOCKER_IMAGE) make wii

assets:
	$(PYTHON) tools/gen_font.py
	$(PYTHON) tools/gen_icon.py

package:
	@test -f build/wii/nightheist.dol || { echo "build/wii/nightheist.dol missing: run 'make wii' or 'make wii-docker' first"; exit 1; }
	@mkdir -p $(DIST_DIR)
	cp build/wii/nightheist.dol $(DIST_DIR)/boot.dol
	cp apps/NightHeist/meta.xml apps/NightHeist/icon.png $(DIST_DIR)/
	cd dist && rm -f NightHeist.zip && $(PYTHON) -m zipfile -c NightHeist.zip apps
	@echo "packaged $(DIST_DIR) and dist/NightHeist.zip"

clean:
	rm -rf build dist

-include $(DESKTOP_OBJ:.o=.d) $(TEST_OBJ:.o=.d)
