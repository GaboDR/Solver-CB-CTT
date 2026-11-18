CXX = g++
CXXFLAGS = -std=c++17 -O3 -Wall -Wextra -pedantic
TARGET = cbctt_solver
SRCS = main.cpp Instance.cpp Evaluator.cpp GreedyInitializer.cpp
OBJS = $(SRCS:.cpp=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(OBJS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Prueba rápida con la primera instancia encontrada en Instances/
test: $(TARGET)
	@mkdir -p solutions
	@file=$$(ls Instances/*.ctt 2>/dev/null | head -n 1); \
	if [ -n "$$file" ]; then \
		echo "=== Ejecutando prueba con $$file ==="; \
		./$(TARGET) "$$file"; \
	else \
		echo "Error: No se encontraron archivos .ctt en la carpeta Instances/"; \
		exit 1; \
	fi

# Prueba y reporte en lote de todas las instancias con medición de tiempo
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

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all test test-all clean