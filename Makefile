# delta-agent - Makefile
# Sem dependencia externa: gcc/clang + a libc do sistema.
#
#   make          -> bin/delta (Linux/macOS) ou bin/delta.exe (Windows/MinGW)
#   make test     -> testes de nucleo (cores, json, largura, markdown)
#   make dist     -> dist/Delta.exe (Windows) ou dist/Delta (Linux/macOS)
#
# macOS: usa o clang do Xcode Command Line Tools (xcode-select --install).
# Linux: gcc (build-essential) e curl em runtime para falar com os provedores.

CC      ?= gcc
ifeq ($(origin CC),default)
CC      := gcc
endif

BUILD   := build
IS_WIN  := $(if $(findstring Windows_NT,$(OS)),1,)

ifeq ($(IS_WIN),1)
  BIN      := bin/delta.exe
  TESTBIN  := bin/delta-tests.exe
  DIST     := dist/Delta.exe
  LDFLAGS  := -static -lwinhttp -luser32 -lkernel32 -lmsvcrt -lws2_32 -lshell32 -lole32 -luuid
  LDLIBS   :=
  EXTRA_OBJ := $(BUILD)/delta-res.o
else
  BIN      := bin/delta
  TESTBIN  := bin/delta-tests
  DIST     := dist/Delta
  LDFLAGS  :=
  LDLIBS   := -lm
  EXTRA_OBJ :=
endif

CFLAGS  := -std=c11 -O2 -Wall -Wextra -Wno-unused-parameter -Isrc
ifeq ($(IS_WIN),)
  CFLAGS += -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=200809L -D_XOPEN_SOURCE=700
endif

CORE_SRC := src/util.c src/json.c src/okcolor.c src/theme.c src/ui.c
APP_SRC  := src/term.c src/clipboard.c src/i18n.c src/tools.c src/tools_ext.c \
            src/extensions.c src/provider.c src/modelsdev.c src/agent.c src/codegraph.c \
            src/update.c src/setup.c src/main.c
ALL_SRC  := $(CORE_SRC) $(APP_SRC)

OBJ      := $(patsubst src/%.c,$(BUILD)/%.o,$(ALL_SRC))
CORE_OBJ := $(patsubst src/%.c,$(BUILD)/%.o,$(CORE_SRC))
DEP      := $(OBJ:.o=.d)

.PHONY: all clean test run dist html skills

all: $(BIN)

$(BIN): $(OBJ) $(EXTRA_OBJ) | bin
	$(CC) $(OBJ) $(EXTRA_OBJ) -o $@ $(LDFLAGS) $(LDLIBS)
	@printf "==> %s  %s bytes\n" "$(BIN)" "$$(stat -c%s $(BIN) 2>/dev/null || stat -f%z $(BIN))"

$(BUILD)/delta-res.o: assets/delta.rc assets/delta.ico | $(BUILD)
	windres -i assets/delta.rc -O coff -o $@

$(BUILD)/%.o: src/%.c
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

$(TESTBIN): $(CORE_OBJ) tests/test_main.c | bin
	$(CC) $(CFLAGS) $(CORE_OBJ) tests/test_main.c -o $@ $(LDFLAGS) $(LDLIBS)

test: $(TESTBIN)
	@./$(TESTBIN)

run: $(BIN)
	@./$(BIN)

dist: $(BIN)
	@mkdir -p dist
	cp $(BIN) $(DIST)
	@printf "==> %s %s bytes (arquivo unico)\n" "$(DIST)" "$$(stat -c%s $(DIST) 2>/dev/null || stat -f%z $(DIST))"

# regenera o logo embutido (precisa de python)
html:
	python tools/embed-logo.py > src/asset_logo.h

# regenera as skills embutidas (edite skills/<nome>/SKILL.md e rode isto)
skills:
	python tools/embed-skills.py > src/skills_seed.h

bin:
	@mkdir -p bin

clean:
	@rm -rf $(BUILD) bin

-include $(DEP)
