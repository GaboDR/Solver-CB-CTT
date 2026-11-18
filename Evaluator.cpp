// Evaluator.cpp
#include "Evaluator.hpp"
#include <cmath>
#include <algorithm>

EvalResult Evaluator::evaluateMatrix(const Instance& inst,
                                     const std::vector<std::vector<int>>& schedule) {
    EvalResult res;

    // Estructuras auxiliares de conteo
    std::vector<int> assigned_lectures(inst.num_courses, 0);
    // course_day_count[c][d]: cantidad de clases del curso c en el día d
    std::vector<std::vector<int>> course_day_count(inst.num_courses, std::vector<int>(inst.days, 0));
    // course_room_used[c][r]: true si el curso c usa la sala r al menos una vez (variable y_{c,r})
    std::vector<std::vector<bool>> course_room_used(inst.num_courses, std::vector<bool>(inst.num_rooms, false));
    // curr_period_active[u][p]: cantidad de clases del currículo u en el periodo p (variable q_{u,p})
    std::vector<std::vector<int>> curr_period_active(inst.num_curricula, std::vector<int>(inst.total_periods, 0));

    // 1. Recorrido por cada Periodo y Sala
    for (int p = 0; p < inst.total_periods; ++p) {
        int d = inst.getDay(p);
        std::vector<int> active_courses_in_period;

        for (int r = 0; r < inst.num_rooms; ++r) {
            int c = schedule[p][r];
            if (c < 0) continue; // Sala vacía en este periodo

            assigned_lectures[c]++;
            course_day_count[c][d]++;
            course_room_used[c][r] = true;

            // DURA Indisponibilidad de horario
            if (inst.unavailable[c][p]) {
                res.unavailability_violations++;
            }

            // BLANDA Room Capacity
            if (inst.courses[c].students > inst.rooms[r].capacity) {
                res.raw_room_capacity += (inst.courses[c].students - inst.rooms[r].capacity);
            }

            // Registrar presencia para conflictos en el mismo periodo p
            for (int prev_c : active_courses_in_period) {
                if (inst.conflict[c][prev_c]) {
                    res.course_curriculum_teacher_conflicts++;
                }
            }
            active_courses_in_period.push_back(c);

            // Marcar actividad en currículos del curso c
            for (int u : inst.courses[c].curricula) {
                curr_period_active[u][p]++;
            }
        }
    }

    // 2. Evaluar por Curso: Clases totales (Dura ), Mínimo de Días (Blanda ) y Estabilidad de Sala (Blanda )
    for (int c = 0; c < inst.num_courses; ++c) {
        // DURA Todas las clases  deben ser impartidas
        if (assigned_lectures[c] != inst.courses[c].lectures) {
            res.unassigned_or_extra_lectures += std::abs(inst.courses[c].lectures - assigned_lectures[c]);
        }

        // BLANDA - MinWorkingDays
        int working_days = 0;
        for (int d = 0; d < inst.days; ++d) {
            if (course_day_count[c][d] > 0) working_days++;
        }
        if (working_days < inst.courses[c].min_working_days) {
            res.raw_min_working_days += (inst.courses[c].min_working_days - working_days);
        }

        // BLANDA  - RoomStability: (salas distintas usadas - 1)
        int distinct_rooms = 0;
        for (int r = 0; r < inst.num_rooms; ++r) {
            if (course_room_used[c][r]) distinct_rooms++;
        }
        if (distinct_rooms > 1) {
            res.raw_room_stability += (distinct_rooms - 1);
        }
    }

    // 3. Evaluar por Currículo: Compacidad del Currículo (Blanda )
    for (int u = 0; u < inst.num_curricula; ++u) {
        for (int d = 0; d < inst.days; ++d) {
            for (int s = 0; s < inst.periods_per_day; ++s) {
                int p = inst.getPeriod(d, s);
                if (curr_period_active[u][p] > 0) {
                    bool has_prev = (s > 0) && (curr_period_active[u][inst.getPeriod(d, s - 1)] > 0);
                    bool has_next = (s + 1 < inst.periods_per_day) && (curr_period_active[u][inst.getPeriod(d, s + 1)] > 0);
                    if (!has_prev && !has_next) {
                        // Si por alguna violación dura hubiera más de 1 clase de u en p, se cuenta por cada clase
                        res.raw_curriculum_compactness += curr_period_active[u][p];
                    }
                }
            }
        }
    }

    // 4. Ponderación UD2
    res.cost_room_capacity          = res.raw_room_capacity * W_ROOM_CAP;
    res.cost_min_working_days       = res.raw_min_working_days * W_MIN_DAYS;
    res.cost_curriculum_compactness = res.raw_curriculum_compactness * W_COMPACT;
    res.cost_room_stability         = res.raw_room_stability * W_STAB;

    return res;
}

void EvalResult::printReport() const {
    std::cout << "=== Reporte Funcion de Evaluacion (UD2) ===\n"
              << "Factible: " << (is_feasible() ? "SI" : "NO")
              << " (Violaciones duras totales: " << total_hard_violations() << ")\n"
              << "  - Clases sin asignar/extra : " << unassigned_or_extra_lectures << "\n"
              << "  - Conflictos (Prof/Curr/C) : " << course_curriculum_teacher_conflicts << "\n"
              << "  - Indisponibilidad horaria : " << unavailability_violations << "\n"
              << "-------------------------------------------\n"
              << "Penalizaciones Blandas:\n"
              << "  - Capacidad de Sala (x1)   : " << raw_room_capacity << " -> " << cost_room_capacity << "\n"
              << "  - Minimo de Dias    (x5)   : " << raw_min_working_days << " -> " << cost_min_working_days << "\n"
              << "  - Compacidad Curr.  (x2)   : " << raw_curriculum_compactness << " -> " << cost_curriculum_compactness << "\n"
              << "  - Estabilidad Sala  (x1)   : " << raw_room_stability << " -> " << cost_room_stability << "\n"
              << "COSTO TOTAL Z (UD2)          : " << total_soft_cost() << "\n";
}