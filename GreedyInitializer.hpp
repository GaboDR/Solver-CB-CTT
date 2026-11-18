#ifndef GREEDY_INITIALIZER_HPP
#define GREEDY_INITIALIZER_HPP

#include "Instance.hpp"
#include <vector>

class GreedyInitializer {
public:
    // Construye la solución inicial en la matriz schedule[P][R]
    // Retorna true si logró asignar todas las clases de todos los cursos
    static bool constructInitialSolution(const Instance& inst,
                                         std::vector<std::vector<int>>& schedule);

private:
    // Puntuación estática de rigidez/dificultad de cada curso (Geiger, 2008)
    static std::vector<int> computeStaticDifficulty(const Instance& inst);

    // Comprueba si un curso c puede impartirse en el periodo p sin violar H3 ni H4
    static bool isPeriodFeasible(const Instance& inst,
                                 const std::vector<std::vector<int>>& schedule,
                                 int c, int p);

    // Cuenta cuántos periodos con salas libres le quedan disponibles al curso c (MRV)
    static int countFeasiblePeriods(const Instance& inst,
                                    const std::vector<std::vector<int>>& schedule,
                                    const std::vector<int>& free_rooms_per_period,
                                    int c);

    // Función de evaluación greedy para elegir la mejor celda (p, r)
    static double evaluatePlacementScore(const Instance& inst,
                                         const std::vector<std::vector<int>>& schedule,
                                         const std::vector<std::vector<int>>& course_day_count,
                                         const std::vector<std::vector<bool>>& course_room_used,
                                         int c, int p, int r);

    // Look-Ahead de preservación de dominio (Lü & Hao, 2010)
    static double computeLookAheadPenalty(const Instance& inst,
                                          const std::vector<std::vector<int>>& schedule,
                                          const std::vector<int>& free_rooms_per_period,
                                          const std::vector<int>& pending_lectures,
                                          const std::vector<int>& feasible_periods,
                                          int c, int p, double alpha);

    // Ejecuta una pasada constructiva pura con un peso alpha
    static int runGreedyPass(const Instance& inst,
                             const std::vector<int>& static_diff,
                             double alpha,
                             std::vector<std::vector<int>>& schedule);
};

#endif