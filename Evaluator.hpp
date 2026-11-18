// Evaluator.hpp
#ifndef EVALUATOR_HPP
#define EVALUATOR_HPP

#include "Instance.hpp"
#include <vector>
#include <iostream>

struct EvalResult {
    // Violaciones a restricciones duras
    int unassigned_or_extra_lectures = 0; 
    int room_occupancy_violations = 0;    
    int course_curriculum_teacher_conflicts = 0; 
    int unavailability_violations = 0;    

    // Componentes sin ponderar de restricciones blandas
    int raw_room_capacity = 0;            // Suma de excesos 
    int raw_min_working_days = 0;         // Suma de días faltantes 
    int raw_curriculum_compactness = 0;   // Suma de clases aisladas
    int raw_room_stability = 0;           // Suma de salas adicionales 


    int cost_room_capacity = 0;
    int cost_min_working_days = 0;
    int cost_curriculum_compactness = 0;
    int cost_room_stability = 0;

    int total_hard_violations() const {
        return unassigned_or_extra_lectures + room_occupancy_violations +
               course_curriculum_teacher_conflicts + unavailability_violations;
    }

    bool is_feasible() const { return total_hard_violations() == 0; }

    int total_soft_cost() const {
        return cost_room_capacity + cost_min_working_days +
               cost_curriculum_compactness + cost_room_stability;
    }

    void printReport() const;
};

class Evaluator {
public:
    // Pesos oficiales formulación UD2 (ITC-2007)
    static constexpr int W_ROOM_CAP = 1;
    static constexpr int W_MIN_DAYS = 5;
    static constexpr int W_COMPACT  = 2;
    static constexpr int W_STAB     = 1;

    // Evalúa una matriz schedule[periodo][sala] = course_id (-1 si está vacía)
    static EvalResult evaluateMatrix(const Instance& inst,
                                     const std::vector<std::vector<int>>& schedule);
};

#endif