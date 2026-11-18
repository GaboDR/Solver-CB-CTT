#ifndef DELTA_EVALUATOR_HPP
#define DELTA_EVALUATOR_HPP
#include "Neighborhood.hpp"
#include <tuple>

// Weighted UD2 components; a Cost can also represent signed deltas.
struct Cost {
    int capacity = 0, days = 0, compactness = 0, stability = 0;
    int total() const { return capacity + days + compactness + stability; }
    bool operator==(const Cost& b) const {
        return std::tie(capacity, days, compactness, stability) ==
               std::tie(b.capacity, b.days, b.compactness, b.stability);
    }
};

class DeltaEvaluator {
public:
    struct State {
        std::vector<std::vector<int>> courseDay, courseRoom, curriculumPeriod;
        Cost cost;
        bool operator==(const State& b) const {
            return courseDay == b.courseDay && courseRoom == b.courseRoom &&
                   curriculumPeriod == b.curriculumPeriod && cost == b.cost;
        }
    };
    // Owns the matrix to keep it synchronized with its counters. Instance
    // must outlive this object. Rejects infeasible/malformed input.
    DeltaEvaluator(const Instance& inst, const Neighborhood::Schedule& schedule);
    const Neighborhood::Schedule& schedule() const { return schedule_; }
    const State& state() const { return state_; }
    Cost delta(const Move& move) const; // throws for an infeasible move; no mutation
    bool apply(const Move& move);
    bool undo(const Move& move); // immediate or LIFO undo
private:
    struct Event { int course, period, room, sign; };
    const Instance& inst_;
    Neighborhood::Schedule schedule_;
    State state_;
    std::vector<Event> events(const Move& move) const;
};
#endif
