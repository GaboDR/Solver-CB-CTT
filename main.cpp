#include "Instance.hpp"
#include "Evaluator.hpp"
#include "GreedyInitializer.hpp"
#include <fstream>
#include <iostream>
#include <chrono>
#include <iomanip>

bool writeSolution(const std::string& out_filename,
                   const Instance& inst,
                   const std::vector<std::vector<int>>& schedule,
                   const EvalResult* report = nullptr,
                   double elapsed_ms = 0.0) {
    std::ofstream out(out_filename);
    if (!out.is_open()) return false;

    if (report) {
        out << std::fixed << std::setprecision(6) << elapsed_ms / 1000.0 << "\n"
            << report->total_hard_violations() << ", " << report->total_soft_cost() << "\n\n"
            << "# Conteo restricciones duras\n"
            << report->unassigned_or_extra_lectures << ", "
            << report->course_curriculum_teacher_conflicts << ", "
            << report->room_occupancy_violations << ", "
            << report->unavailability_violations << "\n\n"
            << "# Conteo restricciones blandas\n"
            << report->raw_room_capacity << ", "
            << report->raw_min_working_days << ", "
            << report->raw_curriculum_compactness << ", "
            << report->raw_room_stability << "\n\n"
            << "# Lista de asignaciones (una por lección)\n";
    }

    for (int p = 0; p < inst.total_periods; ++p) {
        int d = inst.getDay(p);
        int s = inst.getSlot(p);
        for (int r = 0; r < inst.num_rooms; ++r) {
            int c = schedule[p][r];
            if (c >= 0) {
                out << inst.courses[c].code << " "
                    << inst.rooms[r].code << " "
                    << d << " " << s << "\n";
            }
        }
    }
    return true;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Uso: " << argv[0] << " <ruta_instancia.ctt> [ruta_solucion_opcional.sol]\n";
        return 1;
    }

    Instance inst;
    if (!inst.loadFromFile(argv[1])) return 1;
    inst.printSummary();

    std::vector<std::vector<int>> schedule(inst.total_periods, std::vector<int>(inst.num_rooms, -1));
    double elapsed_ms = 0.0;

    if (argc >= 3) {
        std::cout << "\nCargando solucion desde archivo: " << argv[2] << " ...\n";
        std::ifstream sol_file(argv[2]);
        if (!sol_file.is_open()) {
            std::cerr << "Error: No se pudo abrir el archivo de solucion " << argv[2] << "\n";
            return 1;
        }

        std::string c_code, r_code;
        int d, s;
        while (sol_file >> c_code >> r_code >> d >> s) {
            if (inst.course_map.find(c_code) == inst.course_map.end() ||
                inst.room_map.find(r_code) == inst.room_map.end()) {
                std::cerr << "Error: Codigo de curso o sala invalido en .sol (" 
                          << c_code << ", " << r_code << ")\n";
                return 1;
            }
            int c = inst.course_map.at(c_code);
            int r = inst.room_map.at(r_code);
            int p = inst.getPeriod(d, s);
            schedule[p][r] = c;
        }
    } else {
        std::cout << "\nGenerando solucion inicial mediante Heuristica Voraz Adaptativa Hibrida...\n";
        
        // Medición precisa del tiempo de construcción Greedy
        auto start_time = std::chrono::high_resolution_clock::now();
        bool feasible = GreedyInitializer::constructInitialSolution(inst, schedule);
        auto end_time = std::chrono::high_resolution_clock::now();

        elapsed_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();

        if (feasible) {
            std::cout << "[Exito] Solucion inicial generada cumpliendo todas las restricciones duras.\n";
        } else {
            std::cout << "[Aviso] La heuristica voraz no logro asignar el 100% de las clases de forma factible.\n";
        }
        std::cout << "Tiempo de Construccion: " << elapsed_ms << " ms\n";

        std::string out_filename = "solutions/" + inst.name + ".sol";
        if (writeSolution(out_filename, inst, schedule)) {
            std::cout << "Solucion exportada exitosamente en: " << out_filename << "\n";
        }
    }

    EvalResult res = Evaluator::evaluateMatrix(inst, schedule);
    res.printReport();
    std::string report_filename = "solutions/" + inst.name + ".txt";
    if (writeSolution(report_filename, inst, schedule, &res, elapsed_ms)) {
        std::cout << "Reporte exportado en: " << report_filename << "\n";
    } else {
        std::cerr << "Error: No se pudo guardar el reporte en " << report_filename << "\n";
    }

    return 0;
}