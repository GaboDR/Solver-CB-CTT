#include "DeltaEvaluator.hpp"
#include "Evaluator.hpp"
#include "GreedyInitializer.hpp"
#include <iostream>
#include <stdexcept>

namespace {
void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
Cost fullCost(const Instance& inst, const Neighborhood::Schedule& s) {
    auto e = Evaluator::evaluateMatrix(inst, s);
    check(e.is_feasible(), "Hard violation");
    return {e.cost_room_capacity, e.cost_min_working_days, e.cost_curriculum_compactness, e.cost_room_stability};
}
Cost add(Cost a, Cost b) {
    return {a.capacity + b.capacity, a.days + b.days, a.compactness + b.compactness, a.stability + b.stability};
}
void verify(const Instance& inst, DeltaEvaluator& evaluator, const Move& move) {
    const auto original = evaluator.schedule();
    const auto counters = evaluator.state();
    auto before = fullCost(inst, original);
    auto delta = evaluator.delta(move);
    check(evaluator.state() == counters && evaluator.schedule() == original, "Candidate mutated state");
    check(evaluator.apply(move), "Apply failed");
    check(fullCost(inst, evaluator.schedule()) == add(before, delta), "Component delta mismatch");
    check(evaluator.state() == DeltaEvaluator(inst, evaluator.schedule()).state(), "Counters disagree with rebuild");
    check(evaluator.undo(move), "Undo failed");
    check(evaluator.schedule() == original && evaluator.state() == counters, "Undo not exact");
}
}
int main() {
    try {
        std::size_t tested = 0;
        for (const auto& name : {"comp01", "comp02", "comp05", "comp11", "comp12"}) {
            Instance inst;
            check(inst.loadFromFile(std::string("Instances/") + name + ".ctt"), "Load failed");
            Neighborhood::Schedule initial;
            check(GreedyInitializer::constructInitialSolution(inst, initial), "Greedy failed");
            DeltaEvaluator evaluator(inst, initial);
            std::mt19937 rng(20261118);
            for (auto type : {MoveType::MOVE, MoveType::SWAP}) {
                std::size_t n = 0;
                for (int batch = 0; batch < 10; ++batch) {
                    auto candidates = Neighborhood::generateCandidates(inst, evaluator.schedule(), type, 100, rng, 20000);
                    for (const auto& move : candidates) { verify(inst, evaluator, move); ++n; }
                }
                check(n == 1000, "Insufficient sampled tests");
                tested += n;
                int samePeriod = 0;
                for (int p = 0; p < inst.total_periods && samePeriod < 20; ++p)
                    for (int r1 = 0; r1 < inst.num_rooms && samePeriod < 20; ++r1)
                        for (int r2 = 0; r2 < inst.num_rooms && samePeriod < 20; ++r2) {
                            Move move{type, p, r1, p, r2};
                            if (Neighborhood::isFeasible(inst, evaluator.schedule(), move)) {
                                verify(inst, evaluator, move); ++samePeriod; ++tested;
                            }
                        }
                check(samePeriod > 0, "No same-period tests");
            }
            const auto originalState = evaluator.state();
            std::vector<Move> history;
            for (int i = 0; i < 1000; ++i) {
                auto moves = Neighborhood::generateCandidates(inst, evaluator.schedule(),
                    i % 2 ? MoveType::MOVE : MoveType::SWAP, 1, rng, 20000);
                check(!moves.empty(), "Sequence sampling exhausted");
                verify(inst, evaluator, moves[0]); ++tested;
                check(evaluator.apply(moves[0]), "Sequence apply failed");
                history.push_back(moves[0]);
            }
            for (auto it = history.rbegin(); it != history.rend(); ++it) check(evaluator.undo(*it), "LIFO failed");
            check(evaluator.schedule() == initial && evaluator.state() == originalState, "Sequence restoration failed");
            auto badState = evaluator.state();
            check(!evaluator.apply({MoveType::MOVE, -1, 0, 0, 0}), "Invalid apply accepted");
            bool threw = false;
            try { evaluator.delta({MoveType::MOVE, -1, 0, 0, 0}); }
            catch (const std::invalid_argument&) { threw = true; }
            check(threw && evaluator.state() == badState && evaluator.schedule() == initial, "Invalid candidate side effect");
            std::cout << name << ": exact components, counters and sequence restoration OK\n";
        }
        std::cout << "PASS: " << tested << " incremental checks\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
