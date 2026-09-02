# Zostavenie ClaudeLens.  Vzor prevzaty z c:/b/eureka-a4/Makefile aj s jeho
# poucenim: gcc -MMD vypise vedla kazdeho .o subor .d so zoznamom hlaviciek
# a make podla neho prelozi presne to, co treba.  Zastaraly objekt tak
# nevznikne a nemusi sa tomu predchadzat prekladom vsetkeho.
#
# POZOR 1: make si shell vyberie podla PATH -- z cmd.exe je to cmd, z bashu
# sh.exe.  Recepty preto nesmu pouzivat nic, co je len v jednom z nich:
# ziadne `copy`, `if not exist`, `rem`, `mkdir -p`.  `mkdir build` funguje
# v oboch.
#
# POZOR 2: prekladac sa vola absolutnou cestou a globalnemu PATH sa never.
# Ked je na PATH mingw64\bin a preklada sa 32-bitovou vetvou, cc1.exe si
# natiahne 64-bitove libgmp/libisl a loader ho zabije so
# STATUS_INVALID_IMAGE_FORMAT skor, nez stihne cokolvek povedat.
#
# Preco ucrt64 a nie mingw64: UCRT je systemove CRT novsich Windowsov, takze
# odpada msvcrt a jeho zaobchadzanie s UTF-8.  V tejto aplikacii ide o text
# v UTF-8 (protokol) aj UTF-16 (Win32) stale, a jedna vrstva prekvapeni menej
# stoji za to.

UCRT64 ?= C:/msys64/ucrt64
CXX := $(UCRT64)/bin/g++.exe
RC  := $(UCRT64)/bin/windres.exe

BUILD := build
BIN   := bin

WARN     := -Wall -Wextra -Wno-unused-parameter
DEPFLAGS  = -MMD -MP
# Jeden .exe bez msys64 na cielovom stroji.  Pri appke, ktora sa ma dat
# niekomu poslat, to nie je luxus ale podmienka.
STATIC   := -static -static-libgcc -static-libstdc++
# Siroky vstupny bod: cesty s diakritikou prezijú.  Bez -municode hlada linker
# WinMain a padne na undefined reference.
UNICODE_ENTRY := -municode
# Aplikacia je windowsova, nie konzolova.  Konzolovemu .exe pridelí Windows
# konzolu, ci ju chce alebo nie, a vedla okna sa zjavi cierny obdlznik --
# presne to, od coho sa tu odchadza.  Spike konzolu naopak potrebuje.
GUI_SUBSYSTEM := -mwindows

CXXFLAGS := -std=c++20 -O2 $(WARN) -I. -Isrc \
            -DWINVER=0x0A00 -D_WIN32_WINNT=0x0A00 -DUNICODE -D_UNICODE
LDLIBS   := -lole32 -lshell32 -lcomctl32 -luuid -lgdi32

# Nezavisle na ClaudeLens, da sa vziat do ineho projektu tak ako je.
WIN_SRCS   := src/win/window.cpp src/win/dialog.cpp src/win/process.cpp
# Vsetko, co hovori s Claudom: proces, rury, JSONL aj control kanal.  Su
# spolu preto, ze prestanu platit naraz -- ked sa zmeni CLI.
#
# Delene na dve: PURE je cisty preklad bajtov na udalosti a spat a nesiaha na
# nic mimo procesu, takze sa da testovat.  session.cpp vlastni proces, teda
# aj win::Process, a preto ho testy nelinkuju -- keby museli, znamenalo by to,
# ze sa spracovanie protokolu niekde zamotalo so spustanim procesu.
PROTO_PURE_SRCS := src/proto/jsonl.cpp src/proto/events.cpp \
                   src/proto/control.cpp
PROTO_SRCS := $(PROTO_PURE_SRCS) src/proto/session.cpp

# Transkript a jeho mapa rozsahov.  Nevie o windows.h, a prave preto sa da
# testovat bez okna -- co je vacsina toho, preco maju tie testy cenu.
MODEL_SRCS := src/model/utf.cpp src/model/transcript.cpp

UI_SRCS := src/ui/session_pane.cpp src/ui/main_window.cpp

APP_SRCS := $(WIN_SRCS) $(PROTO_SRCS) $(MODEL_SRCS) $(UI_SRCS) src/main.cpp
APP_OBJS := $(patsubst src/%.cpp,$(BUILD)/%.o,$(APP_SRCS))

SPIKE_SRCS := $(WIN_SRCS) $(PROTO_SRCS) src/spike_console.cpp
SPIKE_OBJS := $(patsubst src/%.cpp,$(BUILD)/%.o,$(SPIKE_SRCS))

TEST_SRCS := $(PROTO_PURE_SRCS) $(MODEL_SRCS)
TEST_OBJS := $(patsubst src/%.cpp,$(BUILD)/%.o,$(TEST_SRCS)) \
             $(BUILD)/tests/test_main.o

.PHONY: all app spike test check clean
all: app spike test
app: $(BIN)/claudelens.exe
spike: $(BIN)/spike_console.exe
test: $(BIN)/tests.exe

# `make check` testy aj spusti; `make test` ich len zostavi.
check: test
	./$(BIN)/tests.exe

$(BIN)/claudelens.exe: $(APP_OBJS) | $(BIN)
	$(CXX) $(STATIC) $(UNICODE_ENTRY) $(GUI_SUBSYSTEM) -o $@ $^ $(LDLIBS)

$(BIN)/spike_console.exe: $(SPIKE_OBJS) | $(BIN)
	$(CXX) $(STATIC) $(UNICODE_ENTRY) -o $@ $^ $(LDLIBS)

$(BIN)/tests.exe: $(TEST_OBJS) | $(BIN)
	$(CXX) $(STATIC) -o $@ $^

$(BUILD)/tests/%.o: tests/%.cpp
	mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(DEPFLAGS) -c -o $@ $<

# Jedno pravidlo na vsetky zdroje; adresar objektu kopiruje adresar zdroja,
# takze pribudnutie src/model/ nevyzaduje nic okrem doplnenia do *_SRCS.
$(BUILD)/%.o: src/%.cpp
	mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(DEPFLAGS) -c -o $@ $<

$(BIN):
	mkdir -p $(BIN)

clean:
	rm -rf $(BUILD) $(BIN)

-include $(SPIKE_OBJS:.o=.d)
