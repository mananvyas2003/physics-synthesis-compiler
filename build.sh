#!/usr/bin/env bash
set -euo pipefail

echo "=================================================="
echo " Building physics-synthesis-compiler for Vercel"
echo "=================================================="

# Detect platform and libraries
UNAME=$(uname -s 2>/dev/null || echo "Unknown")
echo "Platform: ${UNAME}"

LIBS="-lpthread -lm"
if [ "${UNAME}" != "Darwin" ] && [[ "${UNAME}" != MINGW* ]] && [[ "${UNAME}" != MSYS* ]]; then
    LIBS="${LIBS} -ldl"
fi

# 1. Compile synth binary if gcc is available
if command -v gcc >/dev/null 2>&1; then
    echo "Compiling synth with gcc (${LIBS})..."
    gcc -std=c11 -O2 \
        -I. -Icli -Iseed -Iemit -Ispec -Icompose -Ibind -Iverify \
        -Ithird_party/sqlite3 -Ithird_party/cJSON \
        -o synth \
        main.c db.c catalogue.c jlcparts_import.c DFM.c compiler.c \
        physics2_isa.c physics2_interpreter.c physics2_types.c physics2_print.c \
        physics2_symbols.c physics2_typecheck.c seed/seed_topology.c \
        emit/emit_bom.c emit/emit_netlist.c emit/emit_snapshot.c \
        spec/spec_load.c spec/llm_provider.c spec/schematic_load.c spec/gemini_schematic.c \
        compose/compose.c bind/bind_scorer.c verify/verify_report.c \
        cli/cli_common.c cli/cmd_db.c cli/cmd_dfm.c cli/cmd_compile.c \
        cli/cmd_physics2.c cli/cmd_generate.c \
        third_party/sqlite3/sqlite3.c third_party/cJSON/cJSON.c \
        ${LIBS}
    chmod +x synth
    echo "synth compiled successfully:"
    ls -lh synth
    # Copy to api/synth so Vercel Python serverless packaging bundles it
    mkdir -p api
    cp -f synth api/synth
    chmod +x api/synth
    # Smoke check
    ./synth --help | head -n 8 || true
else
    echo "Warning: gcc not found in build container. Checking for existing synth binary..."
    if [ -f "synth" ]; then
        chmod +x synth
        mkdir -p api
        cp -f synth api/synth 2>/dev/null || true
        chmod +x api/synth 2>/dev/null || true
        echo "Found existing synth binary."
    elif [ -f "synth.exe" ]; then
        echo "Found synth.exe."
    else
        echo "Error: No synth binary and no gcc compiler found."
        exit 1
    fi
fi

# 2. Sync public static directory
mkdir -p public
cp -rf web/static/* public/
echo "Static assets synced to public/:"
ls -la public/

echo "=================================================="
echo " Vercel build complete!"
echo "=================================================="
