#!/bin/bash
#
# Manual Memory Check Script
# Verifies basic memory management without valgrind
#

set -e

echo "================================"
echo "Memory Management Verification"
echo "================================"
echo ""

# Configuration
INPUT="tests/tiny.txt"
M=3
R=2
OUT1="out1_memcheck.txt"
OUT2="out2_memcheck.txt"
MIND=-1
MAXD=-1
SHMSIZE=20

# Colors
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

PASS_COUNT=0
FAIL_COUNT=0

function pass() {
    echo -e "${GREEN}✓ PASS${NC}: $1"
    PASS_COUNT=$((PASS_COUNT + 1))
}

function fail() {
    echo -e "${RED}✗ FAIL${NC}: $1"
    FAIL_COUNT=$((FAIL_COUNT + 1))
}

function info() {
    echo -e "${YELLOW}ℹ INFO${NC}: $1"
}

# Step 1: Clean and compile
echo "[1] Compiling with debug symbols..."
make clean >/dev/null 2>&1
make findsp_debug >/dev/null 2>&1
if [ $? -eq 0 ]; then
    pass "Compiled findsp_debug successfully"
else
    fail "Compilation failed"
    exit 1
fi
echo ""

# Step 2: Clean up any existing files
echo "[2] Cleaning up existing files..."
rm -f split-* intermediate-* output-* $OUT1 $OUT2 2>/dev/null
pass "Cleaned up temporary files"
echo ""

# Step 3: Check shared memory before run
echo "[3] Checking shared memory before run..."
SHM_BEFORE=$(ls /dev/shm/ 2>/dev/null | grep "findsp" | wc -l)
info "Shared memory segments before run: $SHM_BEFORE"
echo ""

# Step 4: Run the program
echo "[4] Running findsp_debug..."
./findsp_debug $INPUT $M $R $OUT1 $OUT2 $MIND $MAXD $SHMSIZE >/dev/null 2>&1
if [ $? -eq 0 ]; then
    pass "Program executed successfully"
else
    fail "Program execution failed"
    exit 1
fi
echo ""

# Step 5: Check output files exist
echo "[5] Verifying output files..."
if [ -f "$OUT1" ] && [ -s "$OUT1" ]; then
    pass "$OUT1 exists and is non-empty"
else
    fail "$OUT1 missing or empty"
fi

if [ -f "$OUT2" ] && [ -s "$OUT2" ]; then
    pass "$OUT2 exists and is non-empty"
else
    fail "$OUT2 missing or empty"
fi
echo ""

# Step 6: Check temporary files cleaned up
echo "[6] Verifying temporary files cleanup..."
SPLIT_COUNT=$(ls split-* 2>/dev/null | wc -l)
INTER_COUNT=$(ls intermediate-* 2>/dev/null | wc -l)
OUTPUT_COUNT=$(ls output-* 2>/dev/null | wc -l)

if [ $SPLIT_COUNT -eq 0 ]; then
    pass "All split-* files cleaned up"
else
    fail "Found $SPLIT_COUNT split-* files remaining"
fi

if [ $INTER_COUNT -eq 0 ]; then
    pass "All intermediate-* files cleaned up"
else
    fail "Found $INTER_COUNT intermediate-* files remaining"
fi

if [ $OUTPUT_COUNT -eq 0 ]; then
    pass "All output-* files cleaned up"
else
    fail "Found $OUTPUT_COUNT output-* files remaining"
fi
echo ""

# Step 7: Check shared memory cleaned up
echo "[7] Verifying shared memory cleanup..."
SHM_AFTER=$(ls /dev/shm/ 2>/dev/null | grep "findsp" | wc -l)
info "Shared memory segments after run: $SHM_AFTER"

if [ "$SHM_AFTER" -eq "$SHM_BEFORE" ]; then
    pass "No shared memory leaks detected"
else
    fail "Found $((SHM_AFTER - SHM_BEFORE)) leaked shared memory segments"
    info "Leaked segments:"
    ls /dev/shm/ | grep "findsp"
fi
echo ""

# Step 8: Run program multiple times
echo "[8] Running stress test (5 iterations)..."
LEAK_DETECTED=0
for i in {1..5}; do
    rm -f $OUT1 $OUT2 2>/dev/null
    ./findsp_debug $INPUT $M $R $OUT1 $OUT2 $MIND $MAXD $SHMSIZE >/dev/null 2>&1
    
    # Check for leftover files
    if [ $(ls split-* intermediate-* output-* 2>/dev/null | wc -l) -gt 0 ]; then
        LEAK_DETECTED=1
        break
    fi
    
    # Check for shared memory
    SHM_COUNT=$(ls /dev/shm/ 2>/dev/null | grep "findsp" | wc -l)
    if [ "$SHM_COUNT" -gt "$SHM_BEFORE" ]; then
        LEAK_DETECTED=1
        break
    fi
done

if [ $LEAK_DETECTED -eq 0 ]; then
    pass "No resource leaks in 5 iterations"
else
    fail "Resource leak detected during stress test"
fi
echo ""

# Step 9: Check file descriptor limits
echo "[9] Checking file descriptor usage..."
info "Starting findsp_debug and monitoring FDs..."
./findsp_debug $INPUT $M $R $OUT1 $OUT2 $MIND $MAXD $SHMSIZE >/dev/null 2>&1 &
PID=$!

# Wait a bit for the program to start
sleep 0.5

# Check FDs while running
if [ -d "/proc/$PID/fd" ]; then
    FD_COUNT=$(ls /proc/$PID/fd 2>/dev/null | wc -l)
    info "Open file descriptors during execution: $FD_COUNT"
    
    if [ $FD_COUNT -lt 100 ]; then
        pass "File descriptor usage is reasonable (<100)"
    else
        fail "High file descriptor usage: $FD_COUNT"
    fi
else
    info "Program completed too fast to monitor FDs"
fi

# Wait for completion
wait $PID
echo ""

# Step 10: Check for valgrind
echo "[10] Checking for valgrind..."
if command -v valgrind >/dev/null 2>&1; then
    info "Valgrind is installed. Running detailed check..."
    echo ""
    
    valgrind --leak-check=full \
             --show-leak-kinds=all \
             --track-origins=yes \
             --log-file=valgrind_report_manual.txt \
             ./findsp_debug $INPUT $M $R $OUT1 $OUT2 $MIND $MAXD $SHMSIZE >/dev/null 2>&1
    
    # Check for leaks
    if grep -q "All heap blocks were freed" valgrind_report_manual.txt; then
        pass "Valgrind: No memory leaks detected"
    elif grep -q "no leaks are possible" valgrind_report_manual.txt; then
        pass "Valgrind: No memory leaks detected"
    else
        fail "Valgrind: Memory leaks detected"
        info "Check valgrind_report_manual.txt for details"
    fi
    
    # Check for errors
    ERROR_COUNT=$(grep "ERROR SUMMARY:" valgrind_report_manual.txt | awk '{print $4}')
    if [ "$ERROR_COUNT" = "0" ]; then
        pass "Valgrind: No errors detected"
    else
        fail "Valgrind: $ERROR_COUNT errors detected"
    fi
    
    echo ""
    echo "Full valgrind report saved to valgrind_report_manual.txt"
else
    info "Valgrind not installed. Install with:"
    info "  sudo apt-get install valgrind"
fi
echo ""

# Summary
echo "================================"
echo "Summary"
echo "================================"
echo -e "${GREEN}PASS${NC}: $PASS_COUNT"
echo -e "${RED}FAIL${NC}: $FAIL_COUNT"
echo ""

if [ $FAIL_COUNT -eq 0 ]; then
    echo -e "${GREEN}All memory checks passed!${NC}"
    exit 0
else
    echo -e "${RED}Some checks failed. Review the output above.${NC}"
    exit 1
fi

