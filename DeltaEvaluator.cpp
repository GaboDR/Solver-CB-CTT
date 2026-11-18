#include "DeltaEvaluator.hpp"
#include "Evaluator.hpp"
#include <algorithm>
#include <set>
#include <stdexcept>

DeltaEvaluator::DeltaEvaluator(const Instance& inst, const Neighborhood::Schedule& schedule)
    : inst_(inst), schedule_(schedule) {
    if (schedule.size() != static_cast<std::size_t>(inst.total_periods))
        throw std::invalid_argument("Invalid schedule dimensions");
    for (const auto& row : schedule) {
        if (row.size() != static_cast<std::size_t>(inst.num_rooms))
            throw std::invalid_argument("Invalid schedule dimensions");
        for (int c : row) if (c < -1 || c >= inst.num_courses)
            throw std::invalid_argument("Invalid course ID");
    }
    auto full = Evaluator::evaluateMatrix(inst, schedule);
    if (!full.is_feasible()) throw std::invalid_argument("Initial schedule is infeasible");
    state_.cost = {full.cost_room_capacity, full.cost_min_working_days,
                   full.cost_curriculum_compactness, full.cost_room_stability};
    state_.courseDay.assign(inst.num_courses, std::vector<int>(inst.days));
    state_.courseRoom.assign(inst.num_courses, std::vector<int>(inst.num_rooms));
    state_.curriculumPeriod.assign(inst.num_curricula, std::vector<int>(inst.total_periods));
    for (int p = 0; p < inst.total_periods; ++p)
        for (int r = 0; r < inst.num_rooms; ++r) {
            int c = schedule[p][r];
            if (c < 0) continue;
            ++state_.courseDay[c][inst.getDay(p)];
            ++state_.courseRoom[c][r];
            for (int u : inst.courses[c].curricula) ++state_.curriculumPeriod[u][p];
        }
}

std::vector<DeltaEvaluator::Event> DeltaEvaluator::events(const Move& m) const {
    int a = schedule_[m.p1][m.r1], b = schedule_[m.p2][m.r2];
    std::vector<Event> result{{a, m.p1, m.r1, -1}, {a, m.p2, m.r2, 1}};
    if (b >= 0) {
        result.push_back({b, m.p2, m.r2, -1});
        result.push_back({b, m.p1, m.r1, 1});
    }
    return result;
}

Cost DeltaEvaluator::delta(const Move& m) const {
    if (!Neighborhood::isFeasible(inst_, schedule_, m))
        throw std::invalid_argument("Cannot evaluate infeasible move");
    const auto changes = events(m);
    Cost result;
    std::set<int> courses, curricula, periods;
    for (const auto& e : changes) {
        courses.insert(e.course);
        result.capacity += e.sign * std::max(0, inst_.courses[e.course].students - inst_.rooms[e.room].capacity);
        for (int u : inst_.courses[e.course].curricula) curricula.insert(u);
        for (int offset = -1; offset <= 1; ++offset) {
            int p = e.period + offset;
            if (p >= 0 && p < inst_.total_periods && inst_.getDay(p) == inst_.getDay(e.period))
                periods.insert(p);
        }
    }
    // Only the (at most two) affected courses are scanned, never the matrix.
    for (int c : courses) {
        int oldDays = 0, newDays = 0, oldRooms = 0, newRooms = 0;
        for (int d = 0; d < inst_.days; ++d) {
            int n = state_.courseDay[c][d];
            oldDays += n > 0;
            for (const auto& e : changes)
                if (e.course == c && inst_.getDay(e.period) == d) n += e.sign;
            newDays += n > 0;
        }
        for (int r = 0; r < inst_.num_rooms; ++r) {
            int n = state_.courseRoom[c][r];
            oldRooms += n > 0;
            for (const auto& e : changes) if (e.course == c && e.room == r) n += e.sign;
            newRooms += n > 0;
        }
        result.days += Evaluator::W_MIN_DAYS *
            (std::max(0, inst_.courses[c].min_working_days - newDays) -
             std::max(0, inst_.courses[c].min_working_days - oldDays));
        result.stability += std::max(0, newRooms - 1) - std::max(0, oldRooms - 1);
    }
    auto activity = [&](int u, int p, bool after) {
        int n = state_.curriculumPeriod[u][p];
        if (after) for (const auto& e : changes)
            if (e.period == p && inst_.in_curriculum[u][e.course]) n += e.sign;
        return n;
    };
    auto isolated = [&](int u, int p, bool after) {
        int n = activity(u, p, after);
        bool prev = inst_.getSlot(p) > 0 && activity(u, p - 1, after) > 0;
        bool next = inst_.getSlot(p) + 1 < inst_.periods_per_day && activity(u, p + 1, after) > 0;
        return !prev && !next ? n : 0;
    };
    for (int u : curricula) for (int p : periods)
        result.compactness += Evaluator::W_COMPACT * (isolated(u, p, true) - isolated(u, p, false));
    return result;
}

bool DeltaEvaluator::apply(const Move& m) {
    if (!Neighborhood::isFeasible(inst_, schedule_, m)) return false;
    auto change = delta(m);
    auto assignments = events(m);
    if (!Neighborhood::apply(inst_, schedule_, m)) return false;
    for (const auto& e : assignments) {
        state_.courseDay[e.course][inst_.getDay(e.period)] += e.sign;
        state_.courseRoom[e.course][e.room] += e.sign;
        for (int u : inst_.courses[e.course].curricula) state_.curriculumPeriod[u][e.period] += e.sign;
    }
    state_.cost.capacity += change.capacity;
    state_.cost.days += change.days;
    state_.cost.compactness += change.compactness;
    state_.cost.stability += change.stability;
    return true;
}

bool DeltaEvaluator::undo(const Move& m) {
    return apply({m.type, m.p2, m.r2, m.p1, m.r1});
}
