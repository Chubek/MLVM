.PHONY: test validate query monoids loader test-loader clean

PYTHON ?= python3
export PYTHONPATH := optilisp/src

CC ?= cc
CXX ?= c++
CFLAGS ?= -Wall -Wextra -std=c11
CXXFLAGS ?= -Wall -Wextra -std=c++17
LOADER_SRC := optilisp/loader/Optlilisp.c
LOADER_OBJ := optilisp/loader/Optlilisp.o

test: test-loader
	$(PYTHON) -m unittest discover -s tests -p "test_*.py" -t . -v

validate:
	./tools/validate-kernels

query:
	./tools/query-kernels --summary

monoids:
	./tools/check-monoid-laws --verbose

loader: $(LOADER_OBJ)

$(LOADER_OBJ): $(LOADER_SRC) optilisp/loader/Optlilisp.h
	$(CC) $(CFLAGS) -c $(LOADER_SRC) -o $(LOADER_OBJ)

test-loader: loader
	$(PYTHON) tools/check-loader --cc "$(CC)" --cxx "$(CXX)" --cflags "$(CFLAGS)" --verbose

clean:
	rm -f $(LOADER_OBJ)
	find . -name "__pycache__" -type d -exec rm -rf {} + 2>/dev/null || true
	find . -name "*.pyc" -delete 2>/dev/null || true
