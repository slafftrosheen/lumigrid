#!/bin/bash
# Code validation script for LumiGrid
# Run this before committing to catch common issues

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
FIRMWARE_DIR="$PROJECT_ROOT/firmware"

echo "=== LumiGrid Code Validation ==="
echo ""

# Check 1: Single-byte buffers
echo "[1/5] Checking for single-byte buffers..."
if grep -rn --include="*.c" --include="*.h" 'char.*\[1\]' "$FIRMWARE_DIR/main" "$FIRMWARE_DIR/components" 2>/dev/null | grep -v '//'; then
    echo "❌ ERROR: Single-byte buffers found. Use larger buffers."
    exit 1
else
    echo "✅ No single-byte buffers found"
fi

# Check 2: Format string issues
echo ""
echo "[2/5] Checking for potential format string vulnerabilities..."
# Look for printf-family functions where format is not a string literal
FORMAT_ISSUES=$(grep -rn --include="*.c" --include="*.h" -E 'printf\([^"]*\)' "$FIRMWARE_DIR/main" "$FIRMWARE_DIR/components" 2>/dev/null | grep -v '//' | grep -v 'snprintf\|ESP_LOG' | wc -l)
if [ "$FORMAT_ISSUES" -gt 0 ]; then
    echo "⚠️  WARNING: Found $FORMAT_ISSUES potential format string issues"
    echo "    Ensure format strings are literals, not user-controlled variables"
    grep -rn --include="*.c" --include="*.h" -E 'printf\([^"]*\)' "$FIRMWARE_DIR/main" "$FIRMWARE_DIR/components" 2>/dev/null | grep -v '//' | grep -v 'snprintf\|ESP_LOG' | head -3
else
    echo "✅ No format string issues detected"
fi

# Check 3: Unsafe cJSON usage
echo ""
echo "[3/5] Checking for unsafe cJSON usage..."
# Check for direct value access without type validation
UNSAFE_DIRECT=$(grep -rn --include="*.c" -E '->value(int|string|double)' "$FIRMWARE_DIR/main" "$FIRMWARE_DIR/components" 2>/dev/null | grep -v 'cJSON_Is' | wc -l)
UNSAFE_GET=$(grep -rn --include="*.c" 'cJSON_GetObjectItem.*->value' "$FIRMWARE_DIR/main" "$FIRMWARE_DIR/components" 2>/dev/null | grep -v 'cJSON_Is' | wc -l)
UNSAFE_CJSON=$((UNSAFE_DIRECT + UNSAFE_GET))
if [ "$UNSAFE_CJSON" -gt 0 ]; then
    echo "⚠️  WARNING: Found $UNSAFE_CJSON instances of potentially unsafe cJSON usage"
    echo "    Always check NULL and types with cJSON_IsNumber(), cJSON_IsString(), etc."
    if [ "$UNSAFE_DIRECT" -gt 0 ]; then
        echo "    Direct value access without validation: $UNSAFE_DIRECT"
    fi
else
    echo "✅ No unsafe cJSON usage detected"
fi

# Check 4: Memory allocation safety
echo ""
echo "[4/5] Checking memory allocation safety..."
# Find malloc/calloc/realloc and check if followed by NULL check within 3 lines
MALLOC_FILES=$(grep -rn --include="*.c" -E '\b(malloc|calloc|realloc)\(' "$FIRMWARE_DIR/main" "$FIRMWARE_DIR/components" 2>/dev/null | cut -d: -f1 | sort -u)
UNCHECKED=0
for file in $MALLOC_FILES; do
    # This is a simplified check - actual analysis would need parsing
    ALLOC_LINES=$(grep -n -E '\b(malloc|calloc|realloc)\(' "$file" | cut -d: -f1)
    for line in $ALLOC_LINES; do
        # Check next 3 lines for NULL check or if (!
        END_LINE=$((line + 3))
        if ! sed -n "${line},${END_LINE}p" "$file" | grep -q -E 'if.*!|NULL'; then
            UNCHECKED=$((UNCHECKED + 1))
        fi
    done
done
if [ "$UNCHECKED" -gt 0 ]; then
    echo "⚠️  WARNING: Found $UNCHECKED memory allocations that may lack NULL checks"
    echo "    Review all malloc/calloc/realloc calls for proper error handling"
else
    MALLOC_COUNT=$(grep -rn --include="*.c" -E '\b(malloc|calloc|realloc)\(' "$FIRMWARE_DIR/main" "$FIRMWARE_DIR/components" 2>/dev/null | wc -l)
    echo "✅ Found $MALLOC_COUNT memory allocations (all appear to have checks)"
fi

# Check 5: TODO/FIXME markers
echo ""
echo "[5/5] Checking for TODO/FIXME markers..."
TODO_COUNT=$(grep -rn --include="*.c" --include="*.h" 'TODO\|FIXME\|XXX' "$FIRMWARE_DIR/main" "$FIRMWARE_DIR/components" 2>/dev/null | wc -l)
if [ "$TODO_COUNT" -gt 0 ]; then
    echo "ℹ️  INFO: Found $TODO_COUNT TODO/FIXME markers"
else
    echo "✅ No TODO/FIXME markers found"
fi

echo ""
echo "==================================="
echo "✅ Code validation complete"
echo "==================================="
echo ""
echo "Next steps:"
echo "  1. Build: cd firmware && idf.py build"
echo "  2. Test:  cd firmware && idf.py build test"
echo "  3. Commit your changes"
echo ""
