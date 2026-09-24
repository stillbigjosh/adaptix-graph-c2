#!/bin/bash
set -e

if [ -z "$1" ]; then
    echo "Usage: $0 /path/to/AdaptixC2"
    echo "  Builds objects_graph with all beacon sources compiled with -DBEACON_GRAPH."
    exit 1
fi

ADAPTIX_DIR="$1"
AGENT_DIR="$ADAPTIX_DIR/AdaptixServer/extenders/beacon_agent"
BEACON_SRC="$AGENT_DIR/src_beacon/beacon"
FILES_DIR="$AGENT_DIR/src_beacon/files"
OBJ_DIR="$ADAPTIX_DIR/dist/extenders/beacon_agent/objects_graph"

if [ ! -d "$BEACON_SRC" ]; then
    echo "Error: $BEACON_SRC not found."
    exit 1
fi

if [ ! -f "$FILES_DIR/config.tpl" ]; then
    echo "Error: $FILES_DIR/config.tpl not found."
    exit 1
fi

CXX_X64="x86_64-w64-mingw32-g++"
CXX_X86="i686-w64-mingw32-g++"

COMMON_FLAGS="-I $BEACON_SRC \
    -fpermissive -w -masm=intel -fPIC \
    -fno-stack-protector -fno-strict-overflow \
    -fno-delete-null-pointer-checks -fno-strict-aliasing \
    -fno-builtin -fno-exceptions \
    -fno-unwind-tables -fno-asynchronous-unwind-tables"

MINIZ_FLAGS="-DMINIZ_NO_STDIO -DMINIZ_NO_ARCHIVE_APIS -DMINIZ_NO_ARCHIVE_WRITING_APIS -DMINIZ_NO_TIME -DMINIZ_NO_ASSERT"

echo "[*] Building objects_graph..."

rm -rf "$OBJ_DIR"
mkdir -p "$OBJ_DIR"

cp "$FILES_DIR/config.tpl" "$OBJ_DIR/config.cpp"
cp "$FILES_DIR/stub.x64.bin" "$OBJ_DIR/stub.x64.bin"
cp "$FILES_DIR/stub.x86.bin" "$OBJ_DIR/stub.x86.bin"
echo "    Copied config template and stubs"

SOURCES=$(find "$BEACON_SRC" -maxdepth 1 -name "*.cpp" ! -name "config.cpp" -printf "%f\n" | sort)

echo "    Compiling $(echo "$SOURCES" | wc -l) source files (x64 + x86)..."
for src in $SOURCES; do
    name="${src%.cpp}"
    extra_flags=""
    if [ "$name" = "miniz" ]; then
        extra_flags="$MINIZ_FLAGS"
    fi
    $CXX_X64 -c $COMMON_FLAGS -DBEACON_GRAPH $extra_flags "$BEACON_SRC/$src" -o "$OBJ_DIR/${name}.x64.o"
    $CXX_X86 -c $COMMON_FLAGS -DBEACON_GRAPH $extra_flags "$BEACON_SRC/$src" -o "$OBJ_DIR/${name}.x86.o"
done

echo "    Compiling main variants..."
$CXX_X64 -c $COMMON_FLAGS -DBEACON_GRAPH "$BEACON_SRC/main.cpp" -o "$OBJ_DIR/main.x64.o"
$CXX_X64 -c $COMMON_FLAGS -DBEACON_GRAPH -DBUILD_SVC "$BEACON_SRC/main.cpp" -o "$OBJ_DIR/main_service.x64.o"
$CXX_X64 -c $COMMON_FLAGS -DBEACON_GRAPH -D_WIN32_WINNT=0x0600 -DBUILD_DLL "$BEACON_SRC/main.cpp" -o "$OBJ_DIR/main_dll.x64.o"
$CXX_X64 -c $COMMON_FLAGS -DBEACON_GRAPH -DBUILD_SHELLCODE "$BEACON_SRC/main.cpp" -o "$OBJ_DIR/main_shellcode.x64.o"

$CXX_X86 -c $COMMON_FLAGS -DBEACON_GRAPH "$BEACON_SRC/main.cpp" -o "$OBJ_DIR/main.x86.o"
$CXX_X86 -c $COMMON_FLAGS -DBEACON_GRAPH -DBUILD_SVC "$BEACON_SRC/main.cpp" -o "$OBJ_DIR/main_service.x86.o"
$CXX_X86 -c $COMMON_FLAGS -DBEACON_GRAPH -D_WIN32_WINNT=0x0600 -DBUILD_DLL "$BEACON_SRC/main.cpp" -o "$OBJ_DIR/main_dll.x86.o"
$CXX_X86 -c $COMMON_FLAGS -DBEACON_GRAPH -DBUILD_SHELLCODE "$BEACON_SRC/main.cpp" -o "$OBJ_DIR/main_shellcode.x86.o"

rm -f "$OBJ_DIR"/ConnectorHTTP.*.o "$OBJ_DIR"/ConnectorSMB.*.o \
      "$OBJ_DIR"/ConnectorTCP.*.o "$OBJ_DIR"/ConnectorDNS.*.o
echo "    Cleaned non-graph connector objects"

echo "[*] objects_graph built at $OBJ_DIR/"
echo "    $(ls "$OBJ_DIR"/*.o 2>/dev/null | wc -l) object files ready"
