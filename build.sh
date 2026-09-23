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
        -I. -Icli -Iseed -Iemit -Ispec -Icompose -Ibind -Iverify -Inlp \
        -Ithird_party/sqlite3 -Ithird_party/cJSON -Ivendor_next/src \
        -o synth \
        main.c db.c catalogue.c jlcparts_import.c dfm_compose.c mfg_dfm.c vendor_bridge.c compiler.c \
        unit_parse.c diag_error.c part_lib.c \
        physics2_isa.c physics2_interpreter.c physics2_types.c physics2_print.c \
        physics2_symbols.c physics2_typecheck.c seed/seed_topology.c \
        emit/emit_bom.c emit/emit_netlist.c emit/emit_snapshot.c \
        spec/spec_load.c spec/llm_provider.c spec/schematic_load.c \
        nlp/nlp.c nlp/nlp_runtime.c \
        compose/compose.c bind/bind_scorer.c verify/verify_report.c \
        cli/cli_common.c cli/cmd_db.c cli/cmd_dfm.c cli/cmd_compile.c \
        cli/cmd_physics2.c cli/cmd_generate.c cli/cmd_parse.c \
        vendor_next/src/vec.c vendor_next/src/intern.c vendor_next/src/range.c \
        vendor_next/src/constraint.c vendor_next/src/component.c vendor_next/src/net.c \
        vendor_next/src/design.c vendor_next/src/component_model.c \
        vendor_next/src/model_registry.c vendor_next/src/diagnostic.c \
        vendor_next/src/dfm.c vendor_next/src/part_provider.c vendor_next/src/mna.c \
        vendor_next/src/e_series.c vendor_next/src/kicad_generic_provider.c \
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
