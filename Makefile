CC = gcc
CFLAGS = -Wall -pthread
CFLAGS_DEBUG = -Wall -pthread -g -O0
LDFLAGS = -lm

all: findsp findst

findsp: findsp.c
	$(CC) $(CFLAGS) -o findsp findsp.c $(LDFLAGS)

findsp_debug: findsp.c
	$(CC) $(CFLAGS_DEBUG) -o findsp_debug findsp.c $(LDFLAGS)

findst: findst.c
	$(CC) $(CFLAGS) -o findst findst.c $(LDFLAGS)

memcheck: findsp_debug
	@echo "Running valgrind memory check..."
	@if command -v valgrind >/dev/null 2>&1; then \
		valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes \
			--log-file=valgrind_report.txt \
			./findsp_debug tests/tiny.txt 3 2 out1.txt out2.txt -1 -1 20; \
		echo ""; \
		echo "Valgrind report saved to valgrind_report.txt"; \
		echo ""; \
		echo "=== Memory Leak Summary ==="; \
		grep -A 5 "LEAK SUMMARY" valgrind_report.txt || echo "No leaks detected"; \
		echo ""; \
		echo "=== Errors Summary ==="; \
		grep "ERROR SUMMARY" valgrind_report.txt || echo "No errors"; \
	else \
		echo ""; \
		echo "Valgrind not found. Install with:"; \
		echo "  sudo apt-get install valgrind  (Ubuntu/Debian)"; \
		echo "  sudo yum install valgrind      (CentOS/RHEL)"; \
		echo ""; \
		echo "Running manual memory test instead..."; \
		./findsp_debug tests/tiny.txt 3 2 out1.txt out2.txt -1 -1 20; \
		echo ""; \
		echo "Program completed. Install valgrind for detailed memory analysis."; \
	fi

run-findsp: findsp
	./findsp in.txt 3 2 out1.txt out2.txt -1 -1 20

run-findst: findst
	./findst in.txt 3 2 out1.txt out2.txt -1 -1 20

clean:
	rm -f findsp findst findsp_debug out1.txt out2.txt
	rm -f split-* intermediate-* output-*
	rm -f valgrind_report.txt

.PHONY: all clean memcheck run-findsp run-findst

