# Zostavenie AgentAloud.  Zaklad je gcc -MMD: vypise vedla kazdeho .o subor
# .d so zoznamom hlaviciek a make podla neho prelozi presne to, co treba.
# Zastaraly objekt tak nevznikne a nemusi sa tomu predchadzat prekladom
# vsetkeho.
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

# Vypisuje sa meno zdrojaka, nie cely prikaz -- v tom sa chyba prekladaca
# hlada horsie nez sama chyba.  `make V=1` vrati povodny ukecany vystup;
# pri nom sa meno vypise aj tak, aby sa dvojica meno/prikaz dala parovat.
V ?= 0
ifeq ($(V),0)
Q := @
else
Q :=
endif

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
# Symboly von z posielanej appky: 4,69 MB -> 1,81 MB, teda cez tri stvrtiny
# suboru.  Nie je to hlavne DWARF (531 KB, a pozna 64 zdrojakov CRT z msys2
# a ani jeden nas -- CXXFLAGS nema -g), ale COFF tabulka mien: 30 716
# symbolov, ich mena spolu 1,4 MB, najdlhsie 366 znakov.  Take su signatury,
# v ktorych sa vyskytne nlohmann::json.  Straca sa tym gdb backtrace s menami
# funkcii -- bez cisel riadkov, lebo -g tam nie je, a bez toho, kto by ho pri
# pade zachytil: ziadny SetUnhandledExceptionFilter v projekte nie je.  Ked
# raz bude, vrati to `make STRIPFLAG=`.  Len appka: spike ani testy sa nikomu
# neposielaju.
STRIPFLAG ?= -s

# -I$(BUILD) kvoli vygenerovanej hlavicke s verziou, nizsie.
CXXFLAGS := -std=c++20 -O2 $(WARN) -I. -Isrc -I$(BUILD) \
            -DWINVER=0x0A00 -D_WIN32_WINNT=0x0A00 -DUNICODE -D_UNICODE
# winmm je kvoli PlaySound: zvuk pre cakajuci modal sa neda dat cez
# MessageBeep, oba jeho pouzitelne zvuky uz maju iny vyznam (invariant 11).
# winhttp a bcrypt su aktualizacie: stiahnutie a SHA-256 (claude-gui-lkk.53).
# uxtheme vypina temu stavoveho riadku, aby ho NVDA citala spravne (status_bar.cpp).
LDLIBS   := -lole32 -lshell32 -lcomctl32 -luuid -lgdi32 -lwinmm -lwinhttp -lbcrypt -luxtheme

# Verzia.  Nie je napisana v ziadnom zdrojaku -- urcuje ju znacka v gite
# (v2026.10.1, rok.mesiac.poradie), aby druha kopia nemala ako zastarat.
# git describe povie presne tu znacku, ked na
# nej build stoji, a inak ju s priponou (v2026.10.1-5-gabc1234, -dirty) --
# to je vyvojove zostavenie.  --always da hash aj tam, kde znacka este ziadna
# nie je, a hlavne bez chyby na stderr.  Vydanie (vydanie.yml) znacku zalozi
# v lokalnom klone pred prekladom, takze Makefile o vydani nic vediet nemusi.
GIT_DESCRIBE := $(shell git describe --tags --match "v[0-9]*" --always --dirty 2>/dev/null)
ifneq ($(filter v%,$(GIT_DESCRIBE)),)
VERSION := $(patsubst v%,%,$(GIT_DESCRIBE))
else
VERSION := 0.0.0-$(or $(GIT_DESCRIBE),nezname)
endif
# Tri cisla bez pripony a nula, pre FILEVERSION vo VERSIONINFO.
comma := ,
VERSION_CORE := $(firstword $(subst -, ,$(VERSION)))
VERSION_FILE := $(subst .,$(comma),$(VERSION_CORE))$(comma)0
VERSION_H := $(BUILD)/app_version.h

# Nezavisle na AgentAloud, da sa vziat do ineho projektu tak ako je.
WIN_SRCS   := src/win/window.cpp src/win/dialog.cpp src/win/process.cpp \
              src/win/clipboard.cpp src/win/console.cpp src/win/paths.cpp
# Port: co appka od coding agenta potrebuje, bez ohladu na to, ktore CLI to
# je.  Len std typy, ziadny JSON a ziadne windows.h.
AGENT_SRCS := src/agent/backend.cpp
# Vsetko, co hovori s Claudom: proces, rury, JSONL aj control kanal.  Su
# spolu preto, ze prestanu platit naraz -- ked sa zmeni CLI.
#
# Delene na dve: PURE nevlastni ziadny proces, takze sa da testovat.  Vacsina
# z toho je cisty preklad bajtov na udalosti a spat; sessions.cpp cita disk,
# ale iba cita -- test mu podstrci vlastny adresar.  session.cpp vlastni
# proces, teda aj win::Process, a preto ho testy nelinkuju -- keby museli, znamenalo by to,
# ze sa spracovanie protokolu niekde zamotalo so spustanim procesu.
#
# Kazde CLI ma vlastny podadresar s adapterom (proto/claude/, neskor
# proto/codex/); spolocne je len jsonl.
PROTO_PURE_SRCS := src/proto/jsonl.cpp src/proto/claude/events.cpp \
                   src/proto/claude/control.cpp src/proto/claude/ask.cpp \
                   src/proto/claude/sessions.cpp src/proto/claude/translate.cpp \
                   src/proto/codex/translate.cpp
PROTO_SRCS := $(PROTO_PURE_SRCS) src/proto/claude/session.cpp \
              src/proto/claude/claude_backend.cpp \
              src/proto/codex/codex_backend.cpp

# Prikazovy riadok, subor nastaveni a pravidla aktualizacii: ciste, bez
# windows.h a bez siete, aby ich testy videli.
APP_PURE_SRCS := src/arguments.cpp src/settings.cpp src/version.cpp src/update.cpp

# Transkript a jeho mapa rozsahov.  Nevie o windows.h, a prave preto sa da
# testovat bez okna -- co je vacsina toho, preco maju tie testy cenu.
MODEL_SRCS := src/model/utf.cpp src/model/transcript.cpp src/model/bookmarks.cpp \
              src/model/history.cpp

UI_SRCS := src/ui/session_pane.cpp src/ui/main_window.cpp src/ui/speech.cpp \
           src/ui/status_bar.cpp src/ui/session_details.cpp \
           src/ui/ask_dialog.cpp src/ui/permission_dialog.cpp \
           src/ui/command_dialog.cpp src/ui/keys_dialog.cpp \
           src/ui/new_session_dialog.cpp

APP_SRCS := $(WIN_SRCS) $(AGENT_SRCS) $(PROTO_SRCS) $(MODEL_SRCS) $(UI_SRCS) \
            $(APP_PURE_SRCS) src/version_current.cpp src/updater.cpp src/main.cpp
APP_OBJS := $(patsubst src/%.cpp,$(BUILD)/%.o,$(APP_SRCS))

# Dialogove sablony.  Len appka: spike ani testy okno nemaju.
APP_RES := $(BUILD)/ui/app.res.o

SPIKE_SRCS := $(WIN_SRCS) $(AGENT_SRCS) $(PROTO_SRCS) src/spike_console.cpp
SPIKE_OBJS := $(patsubst src/%.cpp,$(BUILD)/%.o,$(SPIKE_SRCS))

TEST_SRCS := $(AGENT_SRCS) $(PROTO_PURE_SRCS) $(MODEL_SRCS) $(APP_PURE_SRCS)
TEST_OBJS := $(patsubst src/%.cpp,$(BUILD)/%.o,$(TEST_SRCS)) \
             $(BUILD)/tests/test_main.o

.PHONY: all app spike test check clean compdb
all: app spike test
# Aplikacia potrebuje DLL vedla seba, nie na PATH: LoadLibrary ho hlada najprv
# v adresari .exe.  Kopiruje sa, nelinkuje -- je pod LGPL, kym AgentAloud je
# pod MIT, a nacitanie za behu tie dve licencie drzi oddelene.
app: $(BIN)/agentaloud.exe $(BIN)/nvdaControllerClient.dll

$(BIN)/nvdaControllerClient.dll: vendor/nvda/nvdaControllerClient.dll | $(BIN)
	@echo "  COPY   $@"
	$(Q)cp $< $@
spike: $(BIN)/spike_console.exe
test: $(BIN)/tests.exe
compdb: compile_commands.json

# `make check` testy aj spusti; `make test` ich len zostavi.
check: test
	$(Q)./$(BIN)/tests.exe

$(BIN)/agentaloud.exe: $(APP_OBJS) $(APP_RES) | $(BIN)
	@echo "  LINK   $@"
	$(Q)$(CXX) $(STATIC) $(UNICODE_ENTRY) $(GUI_SUBSYSTEM) $(STRIPFLAG) -o $@ $^ $(LDLIBS)

$(BIN)/spike_console.exe: $(SPIKE_OBJS) | $(BIN)
	@echo "  LINK   $@"
	$(Q)$(CXX) $(STATIC) $(UNICODE_ENTRY) -o $@ $^ $(LDLIBS)

$(BIN)/tests.exe: $(TEST_OBJS) | $(BIN)
	@echo "  LINK   $@"
	$(Q)$(CXX) $(STATIC) -o $@ $^

# Zdroje sablon.  Kodovanie sa hovori dvakrat: --codepage=65001 tu a
# #pragma code_page(65001) v samotnom .rc.  Odskusane, ze staci ktorekolvek
# z toho a bez oboch windres precita UTF-8 bajty ako codepage 1252, takze
# diakritika v popiskoch skonci ako dvojica znakov -- a prelozi sa to, takze
# to zlyha ticho.  Prepinac je tu preto, aby druhy .rc nezavisel na tom, ci si
# niekto spomenul na pragmu.
#
# Zavislost na resource.h sa pise rucne: -MMD generuje gcc, nie windres, takze
# zmena identifikatora by sa inak neprejavila az do `make clean`.
$(BUILD)/ui/app.res.o: src/ui/resource.h src/ui/app.manifest src/app_name.h $(VERSION_H)

# Hlavicka s verziou sa prepisuje pri kazdom behu, ale subor sa vymeni LEN
# ked sa obsah zmenil.  Make po recepte cas suboru preveri znova, takze
# nezmeneny subor neprelozi nic -- novu hlavicku dostane len novy commit
# alebo nova znacka.  Pravidlo a nie zapis pri citani Makefile:
# `make clean all` by hlavicku zapisanu pri citani hned zmazal a `all` by ju
# uz nemal odkial vziat.
#
# main.o a version_current.o sa na nu odkazuju rucne: -MMD ju pozna az po
# prvom preklade.
$(BUILD)/main.o $(BUILD)/version_current.o: $(VERSION_H)
$(VERSION_H): FORCE
	@mkdir -p $(dir $@)
	$(Q)printf '%s\n' \
	  '// Generated by the Makefile from git describe -- do not edit.' \
	  '#define APP_VERSION "$(VERSION)"' \
	  '#define APP_FILEVERSION $(VERSION_FILE)' > $@.tmp
	$(Q)if cmp -s $@.tmp $@; then rm $@.tmp; \
	  else mv $@.tmp $@; echo "  GEN    $@ ($(VERSION))"; fi
FORCE:
$(BUILD)/%.res.o: src/%.rc
	@mkdir -p $(dir $@)
	@echo "  RC     $<"
	$(Q)$(RC) --codepage=65001 -I. -Isrc -I$(BUILD) -DUNICODE -D_UNICODE -O coff -o $@ $<

$(BUILD)/tests/%.o: tests/%.cpp
	@mkdir -p $(dir $@)
	@echo "  CXX    $<"
	$(Q)$(CXX) $(CXXFLAGS) $(DEPFLAGS) -c -o $@ $<

# Jedno pravidlo na vsetky zdroje; adresar objektu kopiruje adresar zdroja,
# takze pribudnutie src/model/ nevyzaduje nic okrem doplnenia do *_SRCS.
$(BUILD)/%.o: src/%.cpp
	@mkdir -p $(dir $@)
	@echo "  CXX    $<"
	$(Q)$(CXX) $(CXXFLAGS) $(DEPFLAGS) -c -o $@ $<

$(BIN):
	@mkdir -p $(BIN)

clean:
	$(Q)rm -rf $(BUILD) $(BIN) compile_commands.json

# Databaza prekladovych prikazov pre clangd.  Generuje sa z tych istych
# premennych, ktorymi sa preklada, takze sa nema ako rozist s Makefilom --
# nastroj typu `bear`, ktory build odpocuva, by tu navyse musel presvedcit
# make, ze nic nie je hotove.
#
# Cesty musia byt windowsove: clangd je nativny .exe a `/c/vcs/...` z bashu
# mu nic nehovori -- ako vsetko v tomto projekte to zlyha ticho, subor sa
# tvari platny a diagnostika je len prazdna.  Preklada `cygpath -m`; ked
# nie je (make spusteny z cmd.exe), $(CURDIR) uz windowsovy je.
COMPDB_SRCS := $(sort $(APP_SRCS) $(SPIKE_SRCS)) tests/test_main.cpp
COMPDB_DIR  := $(shell cygpath -m "$(CURDIR)" 2>/dev/null || echo "$(CURDIR)")

# Databaza hovori aj TOOLCHAIN, nielen prepinace, a to je tu podstatnejsie
# nez inde.  Klient (LSP plugin Claude Code) spusta holy `clangd
# --background-index`, teda BEZ --query-driver -- a taky clangd sa prekladaca
# nesmie spytat, kde ma hlavicky.  Namiesto toho si na Windows vezme, co najde
# sam: MSVC a Windows SDK.  Prelozi to bez chyby, takze to vyzera spravne, ale
# analyzuje sa iny toolchain nez ten, ktorym sa prekada -- iny <windows.h>,
# ine STL, _MSC_VER namiesto __GNUC__ a mingw vetvy neviditelne.
#
# --target preto hovori, ze prekladame mingw (inak by v hlavickach libstdc++
# platili MSVC vetvy), a -isystem cesty su presne tie, ktore vypise nas g++.
# Vytiahnut ich odtial je jedine miesto, kde nezostarnu pri upgrade gcc; kym
# tu bola verzia napisana rukou, prezila by prvy `pacman -Syu` a nikto by si
# nevsimol.  `< /dev/null` a nie `-E /dev/null`: g++ je nativny .exe a msysovu
# cestu neotvori.
#
# Obnova visi na Makefile, lebo pribudnutie zdrojaka je vzdy aj jeho zmena.
# Po upgrade gcc staci `touch Makefile`.
COMPDB_SYSINC = $(shell $(CXX) -E -x c++ - -v < /dev/null 2>&1 \
    | sed -n '/^#include <\.\.\.>/,/^End of search/p' \
    | sed -n 's/^ /-isystem /p' | tr '\n' ' ')
COMPDB_TARGET := --target=x86_64-w64-mingw32

compile_commands.json: Makefile
	@echo "  GEN    $@"
	$(Q)printf '[\n' > $@
	$(Q)sep=' '; for s in $(COMPDB_SRCS); do \
	  printf '%s{"directory":"%s","file":"%s","command":"%s %s %s %s -c %s"}\n' \
	    "$$sep" '$(COMPDB_DIR)' "$$s" '$(CXX)' '$(CXXFLAGS)' \
	    '$(COMPDB_TARGET)' '$(COMPDB_SYSINC)' "$$s" >> $@; \
	  sep=','; \
	done
	$(Q)printf ']\n' >> $@

# VSETKY objekty, nielen spike.  Kym tu bol len $(SPIKE_OBJS), zmena hlavicky
# neprelozila nic z app/ ani z testov a pouzil sa zastaraly objekt -- presne
# to, comu ma -MMD predchadzat.
-include $(sort $(APP_OBJS:.o=.d) $(SPIKE_OBJS:.o=.d) $(TEST_OBJS:.o=.d))
