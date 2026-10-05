#!/bin/sh
# Pohodlne zostavenie AgentAloud z bashu.  Vsetko, co robi navyse oproti
# holemu `make`, je jedna vec: predradi ucrt64 na PATH.  Prekladac sa v
# Makefile vola absolutnou cestou, ale `make` na PATH nie je (v ucrt64 je len
# mingw32-make.exe), a `cc1.exe` si libgmp/libisl hlada po PATH -- ked tam
# najde inu vetvu, loader ho zabije bez slova (vid globalny CLAUDE.md).
#
#   ./build.sh              # all: app, spike aj testy
#   ./build.sh check        # testy zostavi a spusti
#   ./build.sh clean all
#   ./build.sh V=1 app      # ukecany vystup, cele prikazy
#
# Argumenty idu do make tak, ako prisli, takze funguje aj `-j8` a `-B`.
set -e

cd "$(dirname "$0")"

UCRT64_BIN=${UCRT64_BIN:-/c/msys64/ucrt64/bin}
if [ ! -x "$UCRT64_BIN/g++.exe" ]; then
	echo "build.sh: g++ nenajdeny v $UCRT64_BIN" >&2
	echo "build.sh: nastav UCRT64_BIN na bin adresar msys2 ucrt64" >&2
	exit 1
fi
PATH="$UCRT64_BIN:$PATH"
export PATH

# Databaza pre clangd sa obnovi len vtedy, ked je Makefile novsi -- je to
# beztak jeden printf.  Bez toho by sa na nu zabudlo prave vtedy, ked
# pribudne zdrojak, teda ked ju treba najviac.  Pri `clean` nie: zmazal by
# ju cielovy make hned po tom, co ju tento vyrobil.
case " $* " in
	*" clean "*) ;;
	*) make compdb >/dev/null ;;
esac

exec make "$@"
