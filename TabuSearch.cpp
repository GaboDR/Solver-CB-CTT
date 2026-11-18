#include "TabuSearch.hpp"
#include "Evaluator.hpp"
#include "GreedyInitializer.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>

TabuMemory::TabuMemory(const Instance& inst, std::size_t tenure) : tenure_(tenure),
    until_(inst.num_courses, std::vector<std::vector<std::size_t>>(
        inst.total_periods, std::vector<std::size_t>(inst.num_rooms))) {}

bool TabuMemory::isTabu(const Neighborhood::Schedule& s, const Move& m, std::size_t t) const {
    int a = s[m.p1][m.r1], b = s[m.p2][m.r2];
    return until_[a][m.p2][m.r2] >= t ||
           (m.type == MoveType::SWAP && until_[b][m.p1][m.r1] >= t);
}

void TabuMemory::record(const Neighborhood::Schedule& s, const Move& m, std::size_t t) {
    if (t == 0 || t > std::numeric_limits<std::size_t>::max() - tenure_)
        throw std::invalid_argument("Invalid tabu iteration");
    until_[s[m.p1][m.r1]][m.p1][m.r1] = t + tenure_;
    if (m.type == MoveType::SWAP) until_[s[m.p2][m.r2]][m.p2][m.r2] = t + tenure_;
}

TSResult TabuSearch::run(const Instance& inst, const TSConfig& config, Observer observer) {
    if (config.candidates < 2 || config.attempts < 2 ||
        config.iterations >= std::numeric_limits<std::size_t>::max() - config.tenure)
        throw std::invalid_argument("Need candidates/attempts >= 2 and non-overflowing iteration budget");
    auto start = std::chrono::steady_clock::now();
    Neighborhood::Schedule initial;
    if (!GreedyInitializer::constructInitialSolution(inst, initial))
        throw std::runtime_error("Greedy could not construct a feasible initial solution");
    DeltaEvaluator current(inst, initial); // Independent full feasibility check.
    TSResult result;
    result.best = initial;
    result.initialCost = result.bestCost = result.currentCost = current.state().cost.total();
    TabuMemory memory(inst, config.tenure);
    std::mt19937 rng(config.seed);
    std::bernoulli_distribution chooseMove(0.5);
    if (observer) observer(current, 0);
    for (std::size_t t = 1; t <= config.iterations; ++t) {
        MoveType first = chooseMove(rng) ? MoveType::MOVE : MoveType::SWAP;
        bool found = false, selectedTabu = false;
        Move selected{};
        int selectedCost = 0;
        std::size_t remainingCandidates = config.candidates;
        // Half the budget is reserved for the alternative operator. No
        // unbounded recovery and no silent relaxation of tabu restrictions.
        for (int batch = 0; batch < 2 && !found; ++batch) {
            MoveType type = batch == 0 ? first :
                (first == MoveType::MOVE ? MoveType::SWAP : MoveType::MOVE);
            auto limit = batch == 0 ? config.candidates / 2 : remainingCandidates;
            auto attempts = batch == 0 ? config.attempts / 2 : config.attempts - config.attempts / 2;
            auto candidates = Neighborhood::generateCandidates(inst, current.schedule(), type, limit, rng, attempts);
            remainingCandidates -= candidates.size();
            for (const auto& move : candidates) {
                int cost = result.currentCost + current.delta(move).total();
                ++result.evaluated;
                bool tabu = memory.isTabu(current.schedule(), move, t);
                if (!TabuMemory::admissible(tabu, cost, result.bestCost)) {
                    ++result.tabuRejected;
                    continue;
                }
                if (!found || cost < selectedCost) {
                    found = true;
                    selected = move;
                    selectedCost = cost;
                    selectedTabu = tabu;
                }
            }
        }
        if (!found) { result.stoppedWithoutCandidate = true; break; }
        memory.record(current.schedule(), selected, t);
        if (!current.apply(selected)) throw std::logic_error("Selected candidate became infeasible");
        if (selectedCost > result.currentCost) ++result.worsening;
        result.currentCost = selectedCost;
        result.iterations = t;
        if (selected.type == MoveType::MOVE) ++result.moves; else ++result.swaps;
        if (selectedTabu) ++result.aspirations;
        if (selectedCost < result.bestCost) {
            result.bestCost = selectedCost;
            result.best = current.schedule();
            result.bestIteration = t;
        }
        if (observer) observer(current, t);
    }
    auto final = Evaluator::evaluateMatrix(inst, result.best);
    if (!final.is_feasible() || final.total_soft_cost() != result.bestCost)
        throw std::logic_error("Final independent validation failed");
    result.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    return result;
}

std::string TabuSearch::exportBest(const Instance& inst, const TSResult& result,
                                  const TSConfig& config, const std::string& directory) {
    DeltaEvaluator validation(inst, result.best);
    if (validation.state().cost.total() != result.bestCost)
        throw std::invalid_argument("Best solution/cost mismatch");
    std::filesystem::create_directories(directory);
    auto base = std::filesystem::path(directory) / (inst.name + "_seed" + std::to_string(config.seed));
    std::ofstream sol(base.string() + ".sol"), report(base.string() + ".txt");
    if (!sol || !report) throw std::runtime_error("Cannot open TS output files");
    for (int p = 0; p < inst.total_periods; ++p) for (int r = 0; r < inst.num_rooms; ++r) {
        int c = result.best[p][r];
        if (c >= 0) sol << inst.courses[c].code << ' ' << inst.rooms[r].code << ' '
                        << inst.getDay(p) << ' ' << inst.getSlot(p) << '\n';
    }
    report << "Instance: " << inst.name << "\nInitial feasible: YES\nFinal feasible: YES"
           << "\nGreedy: " << result.initialCost << "\nBest TS: " << result.bestCost
           << "\nLast visited cost: " << result.currentCost << "\nSeconds (including Greedy): " << result.seconds
           << "\nIterations: " << result.iterations << "\nBest iteration: " << result.bestIteration
           << "\nMOVE: " << result.moves << "\nSWAP: " << result.swaps
           << "\nEvaluated: " << result.evaluated << "\nTabu rejected: " << result.tabuRejected
           << "\nAspirations: " << result.aspirations << "\nWorsening: " << result.worsening
           << "\nSampling exhausted: " << result.stoppedWithoutCandidate
           << "\nSeed: " << config.seed << "\nTenure: " << config.tenure
           << "\nIteration budget: " << config.iterations << "\nCandidate budget: " << config.candidates
           << "\nAttempt budget: " << config.attempts << '\n';
    sol.close(); report.close();
    if (!sol || !report) throw std::runtime_error("Failed writing TS output");
    return base.string() + ".sol";
}
