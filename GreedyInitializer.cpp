#include "GreedyInitializer.hpp"
#include "Evaluator.hpp"
#include <climits>
#include <algorithm>
#include <iostream>

std::vector<int> GreedyInitializer::computeStaticDifficulty(const Instance& inst) {
    std::vector<int> diff(inst.num_courses, 0);
    for (int c = 0; c < inst.num_courses; ++c) {
        int conflict_count = 0;
        for (int c2 = 0; c2 < inst.num_courses; ++c2) {
            if (c != c2 && inst.conflict[c][c2]) {
                conflict_count++;
            }
        }
        int unavail_count = 0;
        for (int p = 0; p < inst.total_periods; ++p) {
            if (inst.unavailable[c][p]) {
                unavail_count++;
            }
        }
        diff[c] = inst.courses[c].lectures * (conflict_count * 3 + unavail_count);
    }
    return diff;
}

bool GreedyInitializer::isPeriodFeasible(const Instance& inst,
                                         const std::vector<std::vector<int>>& schedule,
                                         int c, int p) {
    if (inst.unavailable[c][p]) return false;

    for (int r = 0; r < inst.num_rooms; ++r) {
        int other_c = schedule[p][r];
        if (other_c >= 0 && inst.conflict[c][other_c]) {
            return false;
        }
    }
    return true;
}

int GreedyInitializer::countFeasiblePeriods(const Instance& inst,
                                            const std::vector<std::vector<int>>& schedule,
                                            const std::vector<int>& free_rooms_per_period,
                                            int c) {
    int feasible_count = 0;
    for (int p = 0; p < inst.total_periods; ++p) {
        if (free_rooms_per_period[p] > 0 && isPeriodFeasible(inst, schedule, c, p)) {
            feasible_count++;
        }
    }
    return feasible_count;
}

double GreedyInitializer::evaluatePlacementScore(const Instance& inst,
                                                 const std::vector<std::vector<int>>& schedule,
                                                 const std::vector<std::vector<int>>& course_day_count,
                                                 const std::vector<std::vector<bool>>& course_room_used,
                                                 int c, int p, int r) {
    double score = 0.0;
    int students = inst.courses[c].students;
    int capacity = inst.rooms[r].capacity;

    // 1. Capacidad de Sala (Peso 1) + Best-Fit
    if (students > capacity) {
        score += Evaluator::W_ROOM_CAP * (students - capacity) * 8.0;
    } else {
        score += ((double)(capacity - students) / 50.0);
    }

    // 2. Mínimo de Días de Clase (Peso 5)
    int d = inst.getDay(p);
    int current_distinct_days = 0;
    for (int day = 0; day < inst.days; ++day) {
        if (course_day_count[c][day] > 0) current_distinct_days++;
    }
    if (course_day_count[c][d] == 0 && current_distinct_days < inst.courses[c].min_working_days) {
        score -= Evaluator::W_MIN_DAYS * 1.5;
    }

    // 3. Estabilidad de Aula (Peso 1)
    bool already_used_this_room = course_room_used[c][r];
    bool uses_any_room = false;
    for (int room_idx = 0; room_idx < inst.num_rooms; ++room_idx) {
        if (course_room_used[c][room_idx]) {
            uses_any_room = true;
            break;
        }
    }
    if (uses_any_room && !already_used_this_room) {
        score += Evaluator::W_STAB * 1.0;
    }

    // 4. Compacidad Curricular (Peso 2)
    int s = inst.getSlot(p);
    for (int u : inst.courses[c].curricula) {
        bool adj_prev = false, adj_next = false;
        if (s > 0) {
            int prev_p = inst.getPeriod(d, s - 1);
            for (int ro = 0; ro < inst.num_rooms; ++ro) {
                int oc = schedule[prev_p][ro];
                if (oc >= 0 && inst.in_curriculum[u][oc]) { adj_prev = true; break; }
            }
        }
        if (s + 1 < inst.periods_per_day) {
            int next_p = inst.getPeriod(d, s + 1);
            for (int ro = 0; ro < inst.num_rooms; ++ro) {
                int oc = schedule[next_p][ro];
                if (oc >= 0 && inst.in_curriculum[u][oc]) { adj_next = true; break; }
            }
        }
        if (adj_prev || adj_next) {
            score -= Evaluator::W_COMPACT * 1.0;
        }
    }

    return score;
}

// Look-Ahead (Lü & Hao, 2010): Exactamente la formulación que resolvió comp05,
// escalada por el factor alpha.
double GreedyInitializer::computeLookAheadPenalty(const Instance& inst,
                                                  const std::vector<std::vector<int>>& schedule,
                                                  const std::vector<int>& /*free_rooms_per_period*/,
                                                  const std::vector<int>& pending_lectures,
                                                  const std::vector<int>& feasible_periods,
                                                  int c, int p, double alpha) {
    if (alpha <= 0.0) return 0.0;

    double penalty = 0.0;

    for (int oc = 0; oc < inst.num_courses; ++oc) {
        if (oc == c || pending_lectures[oc] == 0) continue;

        if (inst.conflict[c][oc] && !inst.unavailable[oc][p]) {
            bool oc_can_use_p = true;
            for (int r = 0; r < inst.num_rooms; ++r) {
                int assigned_c = schedule[p][r];
                if (assigned_c >= 0 && inst.conflict[oc][assigned_c]) {
                    oc_can_use_p = false;
                    break;
                }
            }

            if (oc_can_use_p) {
                int slack = feasible_periods[oc] - pending_lectures[oc];
                if (slack <= 0) {
                    penalty += 50.0 * alpha;
                } else if (slack == 1) {
                    penalty += 15.0 * alpha;
                } else if (slack <= 3) {
                    penalty += 4.0 * alpha;
                } else {
                    penalty += 0.5 * alpha;
                }
            }
        }
    }

    return penalty;
}

int GreedyInitializer::runGreedyPass(const Instance& inst,
                                     const std::vector<int>& static_diff,
                                     double alpha,
                                     std::vector<std::vector<int>>& schedule) {
    schedule.assign(inst.total_periods, std::vector<int>(inst.num_rooms, -1));
    std::vector<int> free_rooms_per_period(inst.total_periods, inst.num_rooms);
    std::vector<int> pending_lectures(inst.num_courses);
    int total_pending = 0;

    for (int c = 0; c < inst.num_courses; ++c) {
        pending_lectures[c] = inst.courses[c].lectures;
        total_pending += pending_lectures[c];
    }

    std::vector<std::vector<int>> course_day_count(inst.num_courses, std::vector<int>(inst.days, 0));
    std::vector<std::vector<bool>> course_room_used(inst.num_courses, std::vector<bool>(inst.num_rooms, false));

    while (total_pending > 0) {
        int best_c = -1;
        int min_feasible_periods = INT_MAX;
        int max_static_diff = -1;
        int max_pending = -1;

        std::vector<int> feasible_periods(inst.num_courses, 0);
        for (int c = 0; c < inst.num_courses; ++c) {
            if (pending_lectures[c] == 0) continue;
            feasible_periods[c] = countFeasiblePeriods(inst, schedule, free_rooms_per_period, c);
        }

        for (int c = 0; c < inst.num_courses; ++c) {
            if (pending_lectures[c] == 0) continue;

            int feasible_p = feasible_periods[c];
            if (feasible_p > 0) {
                if (feasible_p < min_feasible_periods ||
                   (feasible_p == min_feasible_periods && static_diff[c] > max_static_diff) ||
                   (feasible_p == min_feasible_periods && static_diff[c] == max_static_diff && pending_lectures[c] > max_pending)) {
                    min_feasible_periods = feasible_p;
                    max_static_diff = static_diff[c];
                    max_pending = pending_lectures[c];
                    best_c = c;
                }
            }
        }

        if (best_c == -1) break;

        int best_p = -1;
        int best_r = -1;
        double best_score = 1e18;

        for (int p = 0; p < inst.total_periods; ++p) {
            if (free_rooms_per_period[p] == 0) continue;
            if (!isPeriodFeasible(inst, schedule, best_c, p)) continue;

            double lookahead_cost = computeLookAheadPenalty(inst, schedule, free_rooms_per_period,
                                                            pending_lectures, feasible_periods,
                                                            best_c, p, alpha);

            for (int r = 0; r < inst.num_rooms; ++r) {
                if (schedule[p][r] != -1) continue;

                double soft_score = evaluatePlacementScore(inst, schedule,
                                                           course_day_count,
                                                           course_room_used,
                                                           best_c, p, r);

                double total_score = soft_score + lookahead_cost;
                if (total_score < best_score) {
                    best_score = total_score;
                    best_p = p;
                    best_r = r;
                }
            }
        }

        if (best_p == -1 || best_r == -1) break;

        schedule[best_p][best_r] = best_c;
        pending_lectures[best_c]--;
        total_pending--;
        free_rooms_per_period[best_p]--;
        course_day_count[best_c][inst.getDay(best_p)]++;
        course_room_used[best_c][best_r] = true;
    }

    return total_pending;
}

bool GreedyInitializer::constructInitialSolution(const Instance& inst,
                                                 std::vector<std::vector<int>>& schedule) {
    std::vector<int> static_diff = computeStaticDifficulty(inst);

    // Evaluamos con Look-Ahead completo 
    // y como respaldo sin Look-Ahead
    const std::vector<double> alphas = {1.0, 0.5, 0.25, 0.0};
    int min_unassigned = INT_MAX;
    int best_soft_cost = INT_MAX;
    std::vector<std::vector<int>> best_schedule;

    for (double alpha : alphas) {
        std::vector<std::vector<int>> candidate_schedule;
        int unassigned = runGreedyPass(inst, static_diff, alpha, candidate_schedule);
        int soft_cost = Evaluator::evaluateMatrix(inst, candidate_schedule).total_soft_cost();

        if (unassigned < min_unassigned ||
           (unassigned == min_unassigned && soft_cost < best_soft_cost)) {
            min_unassigned = unassigned;
            best_soft_cost = soft_cost;
            best_schedule = candidate_schedule;
        }
    }

    schedule = best_schedule;
    return (min_unassigned == 0);
}