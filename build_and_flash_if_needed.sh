#!/bin/zsh

# Input Validation: Check if we received exactly 2 arguments
if [ "$#" -ne 2 ]; then
    echo "❌ Error: Missing arguments."
    echo "Usage: $0 <IDF_PATH> <BUILD_DIR>"
    exit 1
fi

IDF_PATH="$1"
BUILD_DIR="$2"
HASH_STORE_FILE="$BUILD_DIR/.last_flash_hash"

# Validation: Ensure paths actually exist
if [ ! -d "$IDF_PATH" ]; then
    echo "❌ Error: IDF_PATH does not exist: $IDF_PATH"
    exit 1
fi

# Load Environment
source "$IDF_PATH/export.sh" > /dev/null 2>&1
echo "🔨 Building in: $BUILD_DIR"
idf.py -B "$BUILD_DIR" build

if [ $? -ne 0 ]; then
    echo "❌ Build Failed! Aborting."
    exit 1
fi

# Find Hash File
IDF_HASH_FILE=$(find "$BUILD_DIR" -maxdepth 1 -name "*.bin_timestamp" -print -quit)

if [ -f "$IDF_HASH_FILE" ]; then
    CURRENT_HASH=$(awk '{print $1}' "$IDF_HASH_FILE")
else
    # Fallback to manual calculation if timestamp file missing
    APP_BIN=$(find "$BUILD_DIR" -maxdepth 1 -name "*.bin" -print -quit)
    if [ -z "$APP_BIN" ]; then
        echo "❌ Error: Could not find binary or timestamp file."
        exit 1
    fi
    CURRENT_HASH=$(python3 -c "import hashlib; print(hashlib.md5(open('$APP_BIN','rb').read()).hexdigest())")
fi

# Read Stored Hash
if [ -f "$HASH_STORE_FILE" ]; then
    STORED_HASH=$(cat "$HASH_STORE_FILE")
else
    STORED_HASH=""
fi

# Compare & Flash
if [ "$CURRENT_HASH" != "$STORED_HASH" ]; then
    echo "🚀 Binary changed (Hash Mismatch). Flashing..."

    # Pass -B to ensure we flash from the correct directory
	  # Make sure the script does not take longer than needed by setting timeout
    timeout 15 idf.py -B "$BUILD_DIR" flash
    FLASH_EXIT_CODE=$?

    if [ $FLASH_EXIT_CODE -eq 0 ]; then
        echo "$CURRENT_HASH" > "$HASH_STORE_FILE"
        echo "✅ Flash Success. Hash stored."
    else
        echo "❌ Flashing Failed! (Exit Code: $FLASH_EXIT_CODE)"
        echo "⚠️  The hash was NOT updated."
        exit 1
    fi
else
    echo "✅ Binary unchanged ($CURRENT_HASH). Skipping Flash."
fi