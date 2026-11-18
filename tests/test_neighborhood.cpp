#include "Neighborhood.hpp"
#include "Evaluator.hpp"
#include "GreedyInitializer.hpp"
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <tuple>

namespace {
using Schedule = Neighborhood::Schedule;
std::size_t verified = 0;

void check(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

std::vector<int> counts(const Instance& inst, const Schedule& schedule) {
    std::vector<int> result(inst.num_courses, 0);
    for (const auto& row : schedule)
        for (int c : row) if (c >= 0) ++result.at(c);
    return result;
}

void verifyAccepted(const Instance& inst, Schedule& schedule, const Move& move) {
    const auto original = schedule;
    const auto before = counts(inst, schedule);
    check(Evaluator::evaluateMatrix(inst, schedule).is_feasible(), "Input must be feasible");
    check(Neighborhood::isFeasible(inst, schedule, move), "Expected feasible move");
    check(schedule == original, "Validation mutated schedule");
    check(Neighborhood::apply(inst, schedule, move), "Apply rejected feasible move");
    auto expected = original;
    std::swap(expected[move.p1][move.r1], expected[move.p2][move.r2]);
    check(schedule == expected, "Apply changed unexpected cells");
    check(Evaluator::evaluateMatrix(inst, schedule).total_hard_violations() == 0,
          "Hard violation after apply");
    check(counts(inst, schedule) == before, "Lecture counts changed");
    check(Neighborhood::undo(inst, schedule, move), "Undo failed");
    check(schedule == original, "Undo did not restore exact matrix");
    ++verified;
}

void verifyRejected(const Instance& inst, Schedule& schedule, const Move& move) {
    const auto original = schedule;
    check(Evaluator::evaluateMatrix(inst, schedule).is_feasible(), "Rejected-case input infeasible");
    check(!Neighborhood::isFeasible(inst, schedule, move), "Invalid move accepted");
    check(!Neighborhood::apply(inst, schedule, move), "Invalid apply accepted");
    check(schedule == original, "Rejected move mutated schedule");
}

Instance fixture() {
    Instance inst;
    inst.num_courses = 6;
    inst.num_rooms = 3;
    inst.days = 2;
    inst.periods_per_day = 2;
    inst.total_periods = 4;
    inst.num_teachers = 5;
    inst.num_curricula = 1;
    for (int c = 0; c < 6; ++c)
        inst.courses.push_back({c, "C" + std::to_string(c),
                                "T" + std::to_string(c == 1 ? 0 : c),
                                c == 1 ? 0 : c, c == 0 ? 2 : 1, 1, 20, {}});
    for (int r = 0; r < 3; ++r)
        inst.rooms.push_back({r, "R" + std::to_string(r), 10 + 20 * r});
    inst.curricula.push_back({0, "U", {2, 3}});
    inst.courses[2].curricula = {0};
    inst.courses[3].curricula = {0};
    inst.in_curriculum = {{false, false, true, true, false, false}};
    inst.conflict.assign(6, std::vector<bool>(6, false));
    for (int c = 0; c < 6; ++c) inst.conflict[c][c] = true;
    inst.conflict[0][1] = inst.conflict[1][0] = true;
    inst.conflict[2][3] = inst.conflict[3][2] = true;
    inst.unavailable.assign(6, std::vector<bool>(4, false));
    inst.unavailable[0][2] = true;
    inst.unavailable[1][3] = true;
    inst.unavailable[2][1] = true;
    return inst;
}

void deterministicTests() {
    auto inst = fixture();
    Schedule schedule{{0, 2, -1}, {1, 4, -1}, {3, 5, -1}, {0, -1, -1}};
    check(Evaluator::evaluateMatrix(inst, schedule).is_feasible(), "Fixture infeasible");
    const auto M = MoveType::MOVE;
    const auto S = MoveType::SWAP;
    for (const Move& move : std::vector<Move>{
             {M, 1, 1, 3, 1}, {M, 0, 0, 0, 2},
             {S, 0, 0, 1, 0}, // Shared teacher: both originals must be removed.
             {S, 0, 1, 2, 0}, // Shared curriculum: likewise.
             {S, 0, 0, 0, 1}})
        verifyAccepted(inst, schedule, move);

    for (const Move& move : std::vector<Move>{
             {M, 0, 0, 1, 0}, {M, 0, 2, 3, 1}, {M, 0, 0, 0, 0},
             {M, -1, 0, 0, 2}, {M, 0, -1, 0, 2},
             {M, 4, 0, 0, 2}, {M, 0, 3, 0, 2},
             {M, 0, 0, 4, 0}, {M, 0, 0, 0, 3},
             {M, 0, 0, -1, 0}, {M, 0, 0, 0, -1},
             {M, 0, 0, 2, 2}, // Unavailability.
             {M, 0, 0, 1, 2}, // Teacher.
             {M, 2, 0, 0, 2}, // Curriculum.
             {M, 0, 0, 3, 2}, // Same course.
             {S, 0, 0, 3, 0}, {S, 0, 2, 1, 0}, {S, 0, 0, 1, 2},
             {S, 0, 0, 0, 0}, {S, -1, 0, 1, 0}, {S, 0, 0, 4, 0},
             {S, 0, 3, 1, 0}, {S, 0, 0, 1, -1},
             {S, 3, 0, 1, 1}, // Teacher after swap.
             {S, 0, 0, 2, 1}, // First course unavailable at destination.
             {S, 3, 0, 1, 0}, // Second course unavailable at destination.
             {S, 0, 0, 1, 1},
             {static_cast<MoveType>(99), 0, 0, 1, 2}}) {
        verifyRejected(inst, schedule, move);
    }
    auto available = inst;
    available.unavailable[0][2] = false;
    verifyRejected(available, schedule, {S, 2, 0, 0, 0}); // Curriculum only.
    verifyRejected(inst, schedule, {S, 0, 0, 3, 1}); // Empty target.
    verifyRejected(inst, schedule, {S, 1, 1, 3, 0});
    auto same_course = schedule;
    check(Neighborhood::apply(inst, same_course, {M, 1, 1, 3, 1}), "Fixture relocation");
    verifyRejected(inst, same_course, {S, 0, 0, 3, 1}); // Same course after swap.

    // Exhaustive independent oracle on this small feasible input. H1/H2 are
    // structural; the full evaluator decides H3/H4 after a raw exchange.
    for (auto type : {M, S})
        for (int p1 = 0; p1 < 4; ++p1)
            for (int r1 = 0; r1 < 3; ++r1)
                for (int p2 = 0; p2 < 4; ++p2)
                    for (int r2 = 0; r2 < 3; ++r2) {
                        Move move{type, p1, r1, p2, r2};
                        int c1 = schedule[p1][r1], c2 = schedule[p2][r2];
                        bool structural = (p1 != p2 || r1 != r2) && c1 >= 0 &&
                            (type == M ? c2 == -1 : c2 >= 0 && c1 != c2);
                        auto changed = schedule;
                        std::swap(changed[p1][r1], changed[p2][r2]);
                        bool expected = structural && Evaluator::evaluateMatrix(inst, changed).is_feasible();
                        check(Neighborhood::isFeasible(inst, schedule, move) == expected,
                              "Local validator disagrees with exhaustive oracle");
                        if (expected) verifyAccepted(inst, schedule, move);
                        else verifyRejected(inst, schedule, move);
                    }
}

using Key = std::tuple<int, int, int, int, int>;
Key key(const Move& move) {
    return {static_cast<int>(move.type), move.p1, move.r1, move.p2, move.r2};
}

void generatorEdges() {
    auto inst = fixture();
    Schedule schedule{{0, 2, -1}, {1, 4, -1}, {3, 5, -1}, {0, -1, -1}};
    std::mt19937 rng(123);
    check(Neighborhood::generateCandidates(inst, schedule, MoveType::MOVE, 0, rng).empty(), "Zero count");
    check(Neighborhood::generateCandidates(inst, schedule, MoveType::SWAP, 20, rng, 0).empty(), "Zero attempts");
    check(Neighborhood::generateCandidates(inst, schedule, MoveType::MOVE, 20, rng, 1).size() <= 1,
          "Attempt bound ignored");
    for (auto type : {MoveType::MOVE, MoveType::SWAP}) {
        auto batch = Neighborhood::generateCandidates(inst, schedule, type, 10000, rng, 1000);
        std::set<Key> unique;
        for (const auto& move : batch) {
            check(unique.insert(key(move)).second, "Duplicate candidate");
            if (type == MoveType::SWAP)
                check(std::make_pair(move.p1, move.r1) < std::make_pair(move.p2, move.r2), "Noncanonical SWAP");
            verifyAccepted(inst, schedule, move);
        }
        check(!batch.empty() && batch.size() < 10000, "Oversized request failed");
    }
    // Feasible degenerate instances: no courses, and one course filling all cells.
    Instance empty;
    empty.days = empty.periods_per_day = empty.total_periods = empty.num_rooms = 1;
    for (auto type : {MoveType::MOVE, MoveType::SWAP})
        check(Neighborhood::generateCandidates(empty, {{-1}}, type, 10, rng).empty(), "Empty pool");
    Instance full;
    full.days = full.periods_per_day = full.total_periods = full.num_rooms = full.num_courses = 1;
    full.courses.push_back({0, "C", "T", 0, 1, 1, 1, {}});
    full.rooms.push_back({0, "R", 1});
    full.conflict = {{true}};
    full.unavailable = {{false}};
    check(Evaluator::evaluateMatrix(full, {{0}}).is_feasible(), "Full fixture infeasible");
    for (auto type : {MoveType::MOVE, MoveType::SWAP})
        check(Neighborhood::generateCandidates(full, {{0}}, type, 10, rng).empty(), "Full singleton");
}

void realInstanceTests() {
    const std::vector<std::pair<std::string, int>> references{
        {"comp01", 187}, {"comp02", 337}, {"comp05", 756}, {"comp11", 57}, {"comp12", 770}};
    for (const auto& reference : references) {
        Instance inst;
        check(inst.loadFromFile("Instances/" + reference.first + ".ctt"), "Cannot load instance");
        Schedule schedule;
        check(GreedyInitializer::constructInitialSolution(inst, schedule), "Greedy incomplete");
        auto evaluation = Evaluator::evaluateMatrix(inst, schedule);
        check(evaluation.is_feasible(), "Greedy hard regression");
        check(evaluation.total_soft_cost() == reference.second, "Greedy cost regression");
        const auto original = schedule;
        const auto original_counts = counts(inst, schedule);
        std::mt19937 rng(20261118);
        std::size_t isolated = 0, consecutive = 0;
        for (auto type : {MoveType::MOVE, MoveType::SWAP}) {
            std::size_t operator_count = 0;
            for (int batch = 0; batch < 10; ++batch) {
                auto rng_copy = rng;
                auto moves = Neighborhood::generateCandidates(inst, schedule, type, 100, rng, 20000);
                auto repeat = Neighborhood::generateCandidates(inst, schedule, type, 100, rng_copy, 20000);
                check(moves.size() == repeat.size() && moves.size() <= 100, "Sampling size");
                check(schedule == original, "Generator mutated schedule");
                std::set<Key> unique;
                for (std::size_t i = 0; i < moves.size(); ++i) {
                    check(key(moves[i]) == key(repeat[i]), "Sampling not reproducible");
                    check(unique.insert(key(moves[i])).second, "Duplicate sampled pair");
                    verifyAccepted(inst, schedule, moves[i]);
                    ++operator_count;
                }
            }
            check(operator_count == 1000, "Expected 1000 candidates per operator/instance");
            isolated += operator_count;
        }
        std::vector<Move> history;
        for (int step = 0; step < 1000; ++step) {
            auto type = step % 2 == 0 ? MoveType::MOVE : MoveType::SWAP;
            auto moves = Neighborhood::generateCandidates(inst, schedule, type, 1, rng, 20000);
            check(!moves.empty(), "Sequence sampling exhausted");
            const auto& move = moves.front();
            verifyAccepted(inst, schedule, move);
            check(Neighborhood::apply(inst, schedule, move), "Sequence apply failed");
            history.push_back(move);
            check(Evaluator::evaluateMatrix(inst, schedule).is_feasible(), "Sequence hard violation");
            check(counts(inst, schedule) == original_counts, "Sequence counts changed");
            ++consecutive;
        }
        for (auto it = history.rbegin(); it != history.rend(); ++it)
            check(Neighborhood::undo(inst, schedule, *it), "LIFO undo failed");
        check(schedule == original, "Sequence not exactly restored");
        std::cout << reference.first << ": hard=0, Greedy=" << evaluation.total_soft_cost()
                  << ", isolated=" << isolated << ", consecutive=" << consecutive << '\n';
    }
}
}

int main() {
    try {
        deterministicTests();
        generatorEdges();
        realInstanceTests();
        std::cout << "PASS: " << verified << " apply/evaluate/count/undo checks\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
