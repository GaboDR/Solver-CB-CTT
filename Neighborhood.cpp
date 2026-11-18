#include "Neighborhood.hpp"
#include <algorithm>
#include <set>
#include <tuple>
#include <utility>

namespace {
bool validCell(const Instance& inst, const Neighborhood::Schedule& schedule,
               int p, int r) {
    return p >= 0 && p < inst.total_periods && r >= 0 && r < inst.num_rooms &&
           schedule.size() == static_cast<std::size_t>(inst.total_periods) &&
           schedule[p].size() == static_cast<std::size_t>(inst.num_rooms);
}

// Both original assignments are absent during this check. If the two new
// assignments share a period, their mutual conflict is checked separately.
bool canInsert(const Instance& inst, const Neighborhood::Schedule& schedule,
               const Move& move, int course, int period) {
    if (inst.unavailable[course][period]) return false;
    for (int r = 0; r < inst.num_rooms; ++r) {
        if ((period == move.p1 && r == move.r1) ||
            (period == move.p2 && r == move.r2)) continue;
        int other = schedule[period][r];
        if (other >= 0 && (course == other || inst.conflict[course][other]))
            return false;
    }
    return true;
}
}

bool Neighborhood::isFeasible(const Instance& inst, const Schedule& schedule,
                              const Move& move) {
    if (move.type != MoveType::MOVE && move.type != MoveType::SWAP) return false;
    if (!validCell(inst, schedule, move.p1, move.r1) ||
        !validCell(inst, schedule, move.p2, move.r2) ||
        (move.p1 == move.p2 && move.r1 == move.r2)) return false;

    int c1 = schedule[move.p1][move.r1];
    int c2 = schedule[move.p2][move.r2];
    if (c1 < 0 || c1 >= inst.num_courses) return false;
    if (move.type == MoveType::MOVE) {
        return c2 == -1 && canInsert(inst, schedule, move, c1, move.p2);
    }
    if (c2 < 0 || c2 >= inst.num_courses || c1 == c2) return false;
    if (move.p1 == move.p2 && inst.conflict[c1][c2]) return false;
    return canInsert(inst, schedule, move, c1, move.p2) &&
           canInsert(inst, schedule, move, c2, move.p1);
}

bool Neighborhood::apply(const Instance& inst, Schedule& schedule, const Move& move) {
    if (!isFeasible(inst, schedule, move)) return false;
    std::swap(schedule[move.p1][move.r1], schedule[move.p2][move.r2]);
    return true;
}

bool Neighborhood::undo(const Instance& inst, Schedule& schedule, const Move& move) {
    Move inverse{move.type, move.p2, move.r2, move.p1, move.r1};
    return apply(inst, schedule, inverse);
}

std::vector<Move> Neighborhood::generateCandidates(
    const Instance& inst, const Schedule& schedule, MoveType type,
    std::size_t max_candidates, std::mt19937& rng, std::size_t max_attempts) {
    std::vector<Move> result;
    if (max_candidates == 0 || max_attempts == 0 ||
        (type != MoveType::MOVE && type != MoveType::SWAP) ||
        schedule.size() != static_cast<std::size_t>(inst.total_periods)) return result;

    using Cell = std::pair<int, int>;
    std::vector<Cell> occupied, empty;
    for (int p = 0; p < inst.total_periods; ++p) {
        if (schedule[p].size() != static_cast<std::size_t>(inst.num_rooms)) return {};
        for (int r = 0; r < inst.num_rooms; ++r) {
            (schedule[p][r] == -1 ? empty : occupied).emplace_back(p, r);
        }
    }
    if (occupied.empty() || (type == MoveType::MOVE && empty.empty()) ||
        (type == MoveType::SWAP && occupied.size() < 2)) return result;

    const auto& targets = type == MoveType::MOVE ? empty : occupied;
    std::uniform_int_distribution<std::size_t> source(0, occupied.size() - 1);
    std::uniform_int_distribution<std::size_t> target(0, targets.size() - 1);
    std::set<std::tuple<int, int, int, int>> seen;
    for (std::size_t attempt = 0;
         attempt < max_attempts && result.size() < max_candidates; ++attempt) {
        Cell a = occupied[source(rng)], b = targets[target(rng)];
        if (type == MoveType::SWAP && b < a) std::swap(a, b);
        Move move{type, a.first, a.second, b.first, b.second};
        auto key = std::make_tuple(move.p1, move.r1, move.p2, move.r2);
        if (seen.find(key) == seen.end() && isFeasible(inst, schedule, move)) {
            seen.insert(key);
            result.push_back(move);
        }
    }
    return result;
}
