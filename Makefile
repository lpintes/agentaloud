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
# wmain namiesto main: argv pride ako wchar_t** a cesty s diakritikou prezijú.
# Bez -municode hlada linker WinMain a padne na undefined reference.
UNICODE_ENTRY := -municode

CXXFLAGS := -std=c++20 -O2 $(WARN) -I. -Isrc \
            -DWINVER=0x0A00 -D_WIN32_WINNT=0x0A00 -DUNICODE -D_UNICODE
LDLIBS   := -lole32 -lshell32 -lcomctl32 -luuid

# Nezavisle na ClaudeLens, da sa vziat do ineho projektu tak ako je.
WIN_SRCS   := src/win/window.cpp src/win/dialog.cpp src/win/process.cpp
# Vsetko, co hovori s Claudom: proces, rury, JSONL aj control kanal.  Su
# spolu preto, ze prestanu platit naraz -- ked sa zmeni CLI.
PROTO_SRCS := src/proto/jsonl.cpp src/proto/events.cpp src/proto/control.cpp \
              src/proto/session.cpp

SPIKE_SRCS := $(WIN_SRCS) $(PROTO_SRCS) src/spike_console.cpp
SPIKE_OBJS := $(patsubst src/%.cpp,$(BUILD)/%.o,$(SPIKE_SRCS))

.PHONY: all spike clean
all: spike
spike: $(BIN)/spike_console.exe

$(BIN)/spike_console.exe: $(SPIKE_OBJS) | $(BIN)
	$(CXX) $(STATIC) $(UNICODE_ENTRY) -o $@ $^ $(LDLIBS)

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
