
# ============================================================
# CB-CTT - GREEDY, TABU SEARCH Y PORTAFOLIO PARALELO
# C++17 - Linux / WSL
# ============================================================

CXX = g++
CXXFLAGS = -std=c++17 -O3 -Wall -Wextra -pedantic

# ============================================================
# EJECUTABLES Y OBJETOS
# ============================================================

TARGET = cbctt_solver

SRCS = main.cpp Instance.cpp Evaluator.cpp GreedyInitializer.cpp Neighborhood.cpp
OBJS = $(SRCS:.cpp=.o)

TS_OBJS = ts_main.o Instance.o Evaluator.o GreedyInitializer.o Neighborhood.o DeltaEvaluator.o TabuSearch.o AdaptiveControl.o

PORTFOLIO_OBJS = portfolio_main.o Instance.o Evaluator.o

# ============================================================
# COMPILACION
# ============================================================

# Compilar los tres ejecutables
all: $(TARGET) cbctt_ts cbctt_portfolio

# Greedy original
$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJS)

# Tabu Search individual
cbctt_ts: $(TS_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $(TS_OBJS)

# Portafolio paralelo de cuatro variantes TS
cbctt_portfolio: $(PORTFOLIO_OBJS) cbctt_ts
	$(CXX) $(CXXFLAGS) -o $@ $(PORTFOLIO_OBJS)

# Compilacion generica de archivos .cpp
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Dependencias entre archivos
Neighborhood.o: Neighborhood.hpp Instance.hpp

DeltaEvaluator.o: DeltaEvaluator.hpp Neighborhood.hpp Evaluator.hpp

TabuSearch.o ts_main.o: TabuSearch.hpp DeltaEvaluator.hpp AdaptiveControl.hpp

AdaptiveControl.o: AdaptiveControl.hpp Neighborhood.hpp

portfolio_main.o: portfolio_main.cpp Instance.hpp Evaluator.hpp

# ============================================================
# PARAMETROS CONFIGURABLES
# ============================================================

INSTANCE ?= comp02
SEED ?= 42
ITERATIONS ?= 100000
TENURE ?= 7
CANDIDATES ?= 100

# ============================================================
# PRUEBAS DEL GREEDY
# ============================================================

# Prueba rapida con la primera instancia encontrada
test: $(TARGET)
	@mkdir -p solutions
	@file=$$(ls Instances/*.ctt 2>/dev/null | head -n 1); \
	if [ -n "$$file" ]; then \
		echo "=== Ejecutando prueba con $$file ==="; \
		./$(TARGET) "$$file"; \
	else \
		echo "Error: No se encontraron archivos .ctt en Instances/"; \
		exit 1; \
	fi

# Prueba de todas las instancias con Greedy
test-all: $(TARGET)
	@mkdir -p solutions
	@echo "=========================================================================================="
	@printf "%-16s | %-9s | %-12s | %-12s | %-12s | %-8s\n" "Instancia" "Factible" "Viol. Duras" "Costo Z(UD2)" "Tiempo (ms)" "Estado"
	@echo "------------------------------------------------------------------------------------------"
	@total=0; feas=0; total_time=0; \
	for f in Instances/*.ctt; do \
		if [ -f "$$f" ]; then \
			total=$$((total + 1)); \
			fname=$$(basename "$$f"); \
			out=$$(./$(TARGET) "$$f" 2>&1); \
			is_feas=$$(echo "$$out" | grep "Factible:" | head -n 1 | awk '{print $$2}'); \
			hard_v=$$(echo "$$out" | grep "Violaciones duras totales:" | head -n 1 | sed -E 's/.*totales: ([0-9]+).*/\1/'); \
			cost_z=$$(echo "$$out" | grep "COSTO TOTAL Z" | head -n 1 | awk '{print $$NF}'); \
			t_ms=$$(echo "$$out" | grep "Tiempo de Construccion:" | head -n 1 | awk '{print $$4}'); \
			if [ -z "$$t_ms" ]; then t_ms="0.0"; fi; \
			if [ "$$is_feas" = "SI" ]; then \
				feas=$$((feas + 1)); \
				status="OK"; \
			else \
				status="NO FACT."; \
			fi; \
			printf "%-16s | %-9s | %-12s | %-12s | %-12s | %-8s\n" "$$fname" "$$is_feas" "$$hard_v" "$$cost_z" "$$t_ms" "$$status"; \
		fi; \
	done; \
	echo "=========================================================================================="; \
	echo "Resumen: $$feas de $$total instancias fueron 100% FACTIBLES con la heuristica voraz."

# ============================================================
# TESTS UNITARIOS
# ============================================================

# Neighborhood
tests/test_neighborhood: tests/test_neighborhood.cpp Instance.o Evaluator.o GreedyInitializer.o Neighborhood.o Neighborhood.hpp Instance.hpp Evaluator.hpp GreedyInitializer.hpp
	$(CXX) $(CXXFLAGS) -I. -o $@ tests/test_neighborhood.cpp Instance.o Evaluator.o GreedyInitializer.o Neighborhood.o

test-neighborhood: tests/test_neighborhood
	./tests/test_neighborhood

# DeltaEvaluator
tests/test_delta_evaluator: tests/test_delta_evaluator.cpp DeltaEvaluator.o Instance.o Evaluator.o GreedyInitializer.o Neighborhood.o DeltaEvaluator.hpp
	$(CXX) $(CXXFLAGS) -I. -o $@ tests/test_delta_evaluator.cpp DeltaEvaluator.o Instance.o Evaluator.o GreedyInitializer.o Neighborhood.o

test-delta: tests/test_delta_evaluator
	./tests/test_delta_evaluator

# Tabu Search
tests/test_tabu_search: tests/test_tabu_search.cpp TabuSearch.o DeltaEvaluator.o Instance.o Evaluator.o GreedyInitializer.o Neighborhood.o AdaptiveControl.o TabuSearch.hpp DeltaEvaluator.hpp
	$(CXX) $(CXXFLAGS) -I. -o $@ tests/test_tabu_search.cpp TabuSearch.o DeltaEvaluator.o Instance.o Evaluator.o GreedyInitializer.o Neighborhood.o AdaptiveControl.o

test-ts: tests/test_tabu_search
	./tests/test_tabu_search

# AdaptiveControl
tests/test_adaptive: tests/test_adaptive.cpp TabuSearch.o DeltaEvaluator.o Instance.o Evaluator.o GreedyInitializer.o Neighborhood.o AdaptiveControl.o TabuSearch.hpp AdaptiveControl.hpp
	$(CXX) $(CXXFLAGS) -I. -o $@ tests/test_adaptive.cpp TabuSearch.o DeltaEvaluator.o Instance.o Evaluator.o GreedyInitializer.o Neighborhood.o AdaptiveControl.o

test-adaptive: tests/test_adaptive
	./tests/test_adaptive

# Ejecutar todos los tests unitarios
test-unit: test-neighborhood test-delta test-ts test-adaptive
	@echo "=========================================="
	@echo "Todos los tests unitarios finalizados."
	@echo "=========================================="

# ============================================================
# EXPERIMENTOS - CUATRO VARIANTES TABU SEARCH
# ============================================================

# Ejecutar TS-A, TS-B, TS-C y TS-D en todas las instancias
test-all-ts: cbctt_ts
	@set -eu; \
	tmpdir=$$(mktemp -d); \
	trap 'rm -rf "$$tmpdir"' EXIT HUP INT TERM; \
	printf "%-13s %-8s %-10s %-10s %-10s\n" \
		"Instancia" "Variante" "Factible" "Greedy" "Mejor TS"; \
	echo "------------------------------------------------------------"; \
	total=0; \
	for file in Instances/*.ctt; do \
		[ -f "$$file" ] || continue; \
		for spec in "A fixed uniform" "B dynamic uniform" \
		            "C fixed adaptive" "D dynamic adaptive"; do \
			set -- $$spec; \
			variant=$$1; \
			tenure=$$2; \
			operator=$$3; \
			if ! out=$$(./cbctt_ts "$$file" \
				--seed $(SEED) \
				--iterations $(ITERATIONS) \
				--tenure $(TENURE) \
				--candidates $(CANDIDATES) \
				--tenure-mode "$$tenure" \
				--operator-mode "$$operator" \
				--output-dir "$$tmpdir" 2>&1); then \
				printf "ERROR: %s TS-%s\n%s\n" "$$file" "$$variant" "$$out"; \
				exit 1; \
			fi; \
			feasible=$$(printf '%s\n' "$$out" | awk -F': ' '/^Final feasible: /{print $$2; exit}'); \
			greedy=$$(printf '%s\n' "$$out" | awk -F': ' '/^Greedy: /{print $$2; exit}'); \
			best=$$(printf '%s\n' "$$out" | awk -F': ' '/^Best TS: /{print $$2; exit}'); \
			if [ "$$feasible" != "YES" ] || \
			   [ -z "$$greedy" ] || [ -z "$$best" ] || \
			   [ "$$best" -gt "$$greedy" ]; then \
				printf "VALIDACION FALLIDA: %s TS-%s\n" "$$file" "$$variant"; \
				exit 1; \
			fi; \
			printf "%-13s %-8s %-10s %-10s %-10s\n" \
				"$$(basename "$$file" .ctt)" "TS-$$variant" \
				"$$feasible" "$$greedy" "$$best"; \
			total=$$((total + 1)); \
		done; \
	done; \
	echo "------------------------------------------------------------"; \
	echo "Ejecuciones validadas: $$total"

# ============================================================
# PORTAFOLIO PARALELO
# ============================================================

# Ejecutar el portafolio sobre una instancia.
#
# Ejemplo:
# make solve-portfolio INSTANCE=comp02 SEED=42 ITERATIONS=100000
#
# Lanza cuatro variantes en paralelo y selecciona
# automaticamente la mejor solucion factible.

solve-portfolio: cbctt_portfolio
	./cbctt_portfolio Instances/$(INSTANCE).ctt \
		--seed $(SEED) \
		--iterations $(ITERATIONS) \
		--candidates $(CANDIDATES)

# Prueba rapida del coordinador paralelo.
# Usa 200 iteraciones para verificar el funcionamiento.
# Los resultados temporales se eliminan al finalizar.

test-portfolio: cbctt_portfolio
	@d=$$(mktemp -d); \
	trap 'rm -rf "$$d"' EXIT; \
	./cbctt_portfolio Instances/comp01.ctt \
		--seed 42 \
		--iterations 200 \
		--output-dir "$$d"

# ============================================================
# LIMPIEZA
# ============================================================

clean:
	rm -f $(OBJS) $(TS_OBJS) $(PORTFOLIO_OBJS) \
		$(TARGET) cbctt_ts cbctt_portfolio \
		tests/test_neighborhood \
		tests/test_delta_evaluator \
		tests/test_tabu_search \
		tests/test_adaptive

# ============================================================
# OBJETIVOS PHONY
# ============================================================

.PHONY: all test test-all \
	test-neighborhood test-delta test-ts test-adaptive test-unit \
	test-all-ts \
	test-portfolio solve-portfolio \
	clean
