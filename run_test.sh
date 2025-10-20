#!/bin/bash

# CS342 Project 1 - findsp Test Script
# Tests the complete MapReduce pipeline

set -e  # Exit on error

# Color codes for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Test parameters
INPUT_FILE="tests/tiny.txt"
M=3
R=2
OUT1="outp1.txt"
OUT2="outp2.txt"
MIND=-1
MAXD=-1
SHMSIZE=20

# Counters
PASS_COUNT=0
FAIL_COUNT=0

echo "========================================"
echo "   CS342 Project 1 - findsp Test"
echo "========================================"
echo ""
echo "Test Configuration:"
echo "  Input: $INPUT_FILE"
echo "  Mappers (M): $M"
echo "  Reducers (R): $R"
echo "  Output files: $OUT1, $OUT2"
echo "  MIND/MAXD: $MIND/$MAXD"
echo "  SHMSIZE: 2^$SHMSIZE = $((2**SHMSIZE)) bytes"
echo ""

# Function to print test result
test_result() {
    local test_name="$1"
    local result="$2"
    
    if [ "$result" = "PASS" ]; then
        echo -e "${GREEN}✓${NC} $test_name: ${GREEN}PASS${NC}"
        PASS_COUNT=$((PASS_COUNT + 1))
    else
        echo -e "${RED}✗${NC} $test_name: ${RED}FAIL${NC}"
        FAIL_COUNT=$((FAIL_COUNT + 1))
    fi
}

# Step 1: Cleanup old files
echo "Step 1: Cleaning up old files..."
rm -f split-* intermediate-* output-* outp*.txt findsp
echo "  Cleaned up"
echo ""

# Step 2: Compilation
echo "Step 2: Compiling findsp..."
if make clean >/dev/null 2>&1 && make findsp >/dev/null 2>&1; then
    test_result "Compilation" "PASS"
else
    test_result "Compilation" "FAIL"
    echo "Compilation failed! Check errors above."
    exit 1
fi
echo ""

# Step 3: Run findsp with timing
echo "Step 3: Running findsp..."
echo "  Command: ./findsp $INPUT_FILE $M $R $OUT1 $OUT2 $MIND $MAXD $SHMSIZE"
echo ""

START_TIME=$(date +%s.%N)
if time ./findsp "$INPUT_FILE" $M $R "$OUT1" "$OUT2" $MIND $MAXD $SHMSIZE > /tmp/findsp_output.log 2>&1; then
    END_TIME=$(date +%s.%N)
    ELAPSED=$(echo "$END_TIME - $START_TIME" | bc)
    test_result "Program execution" "PASS"
    echo "  Execution time: ${ELAPSED}s"
else
    END_TIME=$(date +%s.%N)
    ELAPSED=$(echo "$END_TIME - $START_TIME" | bc)
    test_result "Program execution" "FAIL"
    echo "  Execution time: ${ELAPSED}s"
    echo "  Check /tmp/findsp_output.log for errors"
    exit 1
fi
echo ""

# Step 4: Verify cleanup (intermediate files should be removed)
echo "Step 4: Verifying cleanup..."

# Check that split files were cleaned up
SPLIT_FILES=$(ls split-* 2>/dev/null | wc -l)
if [ $SPLIT_FILES -eq 0 ]; then
    test_result "Split files cleaned up" "PASS"
else
    test_result "Split files cleaned up (found $SPLIT_FILES remaining)" "FAIL"
fi

# Check that intermediate files were cleaned up
INTERMEDIATE_FILES=$(ls intermediate-* 2>/dev/null | wc -l)
if [ $INTERMEDIATE_FILES -eq 0 ]; then
    test_result "Intermediate files cleaned up" "PASS"
else
    test_result "Intermediate files cleaned up (found $INTERMEDIATE_FILES remaining)" "FAIL"
fi

# Check that output files were cleaned up
OUTPUT_FILES=$(ls output-* 2>/dev/null | wc -l)
if [ $OUTPUT_FILES -eq 0 ]; then
    test_result "Output files cleaned up" "PASS"
else
    test_result "Output files cleaned up (found $OUTPUT_FILES remaining)" "FAIL"
fi

echo ""

# Step 5: Verify final output files
echo "Step 5: Verifying final output files..."

if [ -f "$OUT1" ]; then
    test_result "$OUT1 exists" "PASS"
    
    # Check if non-empty
    if [ -s "$OUT1" ]; then
        test_result "$OUT1 non-empty" "PASS"
        
        # Check sorting
        awk -F: '{print $1}' "$OUT1" | sort -n > /tmp/sorted_out1.txt
        awk -F: '{print $1}' "$OUT1" > /tmp/original_out1.txt
        
        if diff /tmp/sorted_out1.txt /tmp/original_out1.txt >/dev/null 2>&1; then
            test_result "$OUT1 sorted by destination" "PASS"
        else
            test_result "$OUT1 sorted by destination" "FAIL"
        fi
        
        rm -f /tmp/sorted_out1.txt /tmp/original_out1.txt
        
        # Show first few lines
        echo "  First 5 lines of $OUT1:"
        head -5 "$OUT1" | sed 's/^/    /'
    else
        test_result "$OUT1 non-empty" "FAIL"
    fi
else
    test_result "$OUT1 exists" "FAIL"
fi

echo ""

if [ -f "$OUT2" ]; then
    test_result "$OUT2 exists" "PASS"
    
    # Check if non-empty
    if [ -s "$OUT2" ]; then
        test_result "$OUT2 non-empty" "PASS"
        
        # Check format (should be "dest count")
        if awk '{if (NF != 2 || $1 !~ /^[0-9]+$/ || $2 !~ /^[0-9]+$/) exit 1}' "$OUT2"; then
            test_result "$OUT2 format (dest count)" "PASS"
        else
            test_result "$OUT2 format (dest count)" "FAIL"
        fi
        
        # Check sorting
        awk '{print $1}' "$OUT2" | sort -n > /tmp/sorted_out2.txt
        awk '{print $1}' "$OUT2" > /tmp/original_out2.txt
        
        if diff /tmp/sorted_out2.txt /tmp/original_out2.txt >/dev/null 2>&1; then
            test_result "$OUT2 sorted by destination" "PASS"
        else
            test_result "$OUT2 sorted by destination" "FAIL"
        fi
        
        rm -f /tmp/sorted_out2.txt /tmp/original_out2.txt
        
        # Show first few lines
        echo "  First 5 lines of $OUT2:"
        head -5 "$OUT2" | sed 's/^/    /'
    else
        test_result "$OUT2 non-empty" "FAIL"
    fi
else
    test_result "$OUT2 exists" "FAIL"
fi

echo ""

# Step 6: Cross-verify OUT1 and OUT2
echo "Step 6: Cross-verifying OUT1 and OUT2..."

if [ -f "$OUT1" ] && [ -f "$OUT2" ]; then
    MISMATCH_COUNT=0
    
    while IFS=: read -r dest sources; do
        # Count sources (words in second field)
        source_count=$(echo "$sources" | wc -w)
        
        # Get count from OUT2
        out2_count=$(grep "^$dest " "$OUT2" 2>/dev/null | awk '{print $2}')
        
        if [ -n "$out2_count" ]; then
            if [ "$source_count" -ne "$out2_count" ]; then
                MISMATCH_COUNT=$((MISMATCH_COUNT + 1))
            fi
        fi
    done < "$OUT1"
    
    if [ $MISMATCH_COUNT -eq 0 ]; then
        test_result "OUT1 source counts match OUT2 counts" "PASS"
    else
        test_result "OUT1 source counts match OUT2 counts ($MISMATCH_COUNT mismatches)" "FAIL"
    fi
else
    echo "  Skipping cross-verification (missing output files)"
fi

echo ""

# Step 7: Check for memory leaks (if valgrind available)
echo "Step 7: Memory leak check..."
if command -v valgrind &> /dev/null; then
    echo "  Running valgrind (this may take a moment)..."
    if valgrind --leak-check=summary --error-exitcode=1 \
        ./findsp "$INPUT_FILE" $M $R "$OUT1" "$OUT2" $MIND $MAXD $SHMSIZE \
        >/dev/null 2>/tmp/valgrind_output.txt; then
        test_result "No memory leaks" "PASS"
    else
        test_result "No memory leaks" "FAIL"
        echo "  Check /tmp/valgrind_output.txt for details"
    fi
else
    echo "  Valgrind not available, skipping memory leak check"
fi

echo ""

# Final Summary
echo "========================================"
echo "           TEST SUMMARY"
echo "========================================"
echo ""
echo -e "Total tests: $((PASS_COUNT + FAIL_COUNT))"
echo -e "${GREEN}Passed: $PASS_COUNT${NC}"
echo -e "${RED}Failed: $FAIL_COUNT${NC}"
echo ""

if [ $FAIL_COUNT -eq 0 ]; then
    echo -e "${GREEN}════════════════════════════════════════${NC}"
    echo -e "${GREEN}   ALL TESTS PASSED! ✓${NC}"
    echo -e "${GREEN}════════════════════════════════════════${NC}"
    echo ""
    echo "Your findsp implementation is working correctly!"
    echo ""
    exit 0
else
    echo -e "${RED}════════════════════════════════════════${NC}"
    echo -e "${RED}   SOME TESTS FAILED ✗${NC}"
    echo -e "${RED}════════════════════════════════════════${NC}"
    echo ""
    echo "Please review the failed tests above."
    echo "Check /tmp/findsp_output.log for program output."
    echo ""
    exit 1
fi

