#include "TabuSearch.hpp"
#include "Evaluator.hpp"
#include "GreedyInitializer.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <memory>
#include <iomanip>
#include <sstream>
#include <algorithm>

TabuMemory::TabuMemory(const Instance& inst, std::size_t tenure) : tenure_(tenure),
    until_(inst.num_courses, std::vector<std::vector<std::size_t>>(
        inst.total_periods, std::vector<std::size_t>(inst.num_rooms))) {}

bool TabuMemory::isTabu(const Neighborhood::Schedule& s, const Move& m, std::size_t t) const {
    int a = s[m.p1][m.r1], b = s[m.p2][m.r2];
    return until_[a][m.p2][m.r2] >= t ||
           (m.type == MoveType::SWAP && until_[b][m.p1][m.r1] >= t);
}

void TabuMemory::record(const Neighborhood::Schedule& s, const Move& m, std::size_t t) {
    record(s, m, t, tenure_);
}

void TabuMemory::record(const Neighborhood::Schedule& s, const Move& m, std::size_t t, std::size_t tenure) {
    if (t == 0 || t > std::numeric_limits<std::size_t>::max() - tenure)
        throw std::invalid_argument("Invalid tabu iteration");
    until_[s[m.p1][m.r1]][m.p1][m.r1] = t + tenure;
    if (m.type == MoveType::SWAP) until_[s[m.p2][m.r2]][m.p2][m.r2] = t + tenure;
}

TSResult TabuSearch::run(const Instance& inst, const TSConfig& config, Observer observer) {
    AdaptiveControl::tenure(config.tenureMin, config.tenureMax, config.stagnationWindow, 0);
    AdaptiveControl::moveProbability(0, 0, config.pMin);
    if (config.feasibilitySamples == 0 || config.adaptationInterval == 0)
        throw std::invalid_argument("Samples and adaptation interval must be positive");
    if (config.restartStagnation > 0 && config.restartMoves == 0)
        throw std::invalid_argument("Enabled recovery requires restartMoves > 0");
    auto maximumTenure = config.tenureMode == TSConfig::TenureMode::Fixed ? config.tenure : config.tenureMax;
    if (config.candidates < 2 || config.attempts < 2 ||
        config.iterations >= std::numeric_limits<std::size_t>::max() - maximumTenure)
        throw std::invalid_argument("Need candidates/attempts >= 2 and non-overflowing iteration budget");
    auto start = std::chrono::steady_clock::now();
    Neighborhood::Schedule initial;
    if (!GreedyInitializer::constructInitialSolution(inst, initial))
        throw std::runtime_error("Greedy could not construct a feasible initial solution");
    auto current = std::make_unique<DeltaEvaluator>(inst, initial); // Independent full feasibility check.
    TSResult result;
    result.best = initial;
    result.initialCost = result.bestCost = result.currentCost = current->state().cost.total();
    TabuMemory memory(inst, config.tenure);
    std::mt19937 rng(config.seed);
    std::bernoulli_distribution chooseMove(0.5);
    std::mt19937 estimateRng(config.seed ^ 0x9e3779b9U), restartRng(config.seed ^ 0x85ebca6bU);
    std::size_t lastTenure = config.tenureMode == TSConfig::TenureMode::Fixed ? config.tenure : config.tenureMin;
    auto savePoint = [&](std::size_t t) {
        if (!config.logInterval) return;
        if (!result.convergence.empty() && result.convergence.back().iteration == t) result.convergence.pop_back();
        result.convergence.push_back({t, lastTenure, t - result.bestIteration,
            std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count(),
            result.pMove,result.qMove,result.qSwap,result.currentCost,result.bestCost});
    };
    auto updateBest = [&](std::size_t t) {
        result.currentCost = current->state().cost.total();
        if (result.currentCost < result.bestCost) {
            result.bestCost = result.currentCost; result.best = current->schedule();
            result.bestIteration = t; ++result.improvements;
        }
    };
    savePoint(0);
    if (observer) observer(*current, 0);
    for (std::size_t t = 1; t <= config.iterations; ++t) {
        // Recovery consumes no additional search iterations. Its separately
        // counted perturbations are bounded by restartMoves. Cooldown avoids
        // restarting on every iteration while the global best is unchanged.
        if (config.restartStagnation && t - 1 - std::max(result.bestIteration, result.lastRecovery) >= config.restartStagnation) {
            current = std::make_unique<DeltaEvaluator>(inst, result.best);
            result.currentCost = result.bestCost;
            memory = TabuMemory(inst, config.tenure); // explicitly begin a new trajectory
            ++result.recoveries; result.lastRecovery = t - 1;
            if (observer) observer(*current, t - 1); // restored best, before perturbation
            std::size_t applied = 0;
            for (std::size_t k = 0; k < config.restartMoves; ++k) {
                MoveType type = chooseMove(restartRng) ? MoveType::MOVE : MoveType::SWAP;
                auto moves = Neighborhood::generateCandidates(inst, current->schedule(), type, 1, restartRng, config.attempts / 2);
                if (moves.empty()) moves = Neighborhood::generateCandidates(inst, current->schedule(),
                    type == MoveType::MOVE ? MoveType::SWAP : MoveType::MOVE, 1, restartRng, config.attempts - config.attempts / 2);
                if (moves.empty()) break;
                if (!current->apply(moves.front())) throw std::logic_error("Invalid perturbation");
                ++applied; ++result.perturbations;
                if (moves.front().type == MoveType::MOVE) ++result.perturbationMoves;
                else ++result.perturbationSwaps;
                updateBest(t - 1);
                if (observer) observer(*current, t - 1);
            }
            if (!applied) { result.stoppedWithoutCandidate = true; break; }
        }
        if (config.operatorMode == TSConfig::OperatorMode::Adaptive && (t - 1) % config.adaptationInterval == 0) {
            auto estimate = AdaptiveControl::estimate(inst, current->schedule(), config.feasibilitySamples, estimateRng);
            result.qMove = estimate.qMove(); result.qSwap = estimate.qSwap();
            result.pMove = AdaptiveControl::moveProbability(result.qMove, result.qSwap, config.pMin);
            ++result.adaptations;
        }
        MoveType first = (config.operatorMode == TSConfig::OperatorMode::Uniform ? chooseMove(rng) :
                         std::bernoulli_distribution(result.pMove)(rng)) ? MoveType::MOVE : MoveType::SWAP;
        if (first == MoveType::MOVE) ++result.moveSelected; else ++result.swapSelected;
        bool found = false, selectedTabu = false;
        Move selected{};
        int selectedCost = 0;
        std::size_t remainingCandidates = config.candidates;
        // Half the budget is reserved for the alternative operator. No
        // unbounded recovery and no silent relaxation of tabu restrictions.
        for (int batch = 0; batch < 2 && !found; ++batch) {
            if (batch) ++result.alternatives;
            MoveType type = batch == 0 ? first :
                (first == MoveType::MOVE ? MoveType::SWAP : MoveType::MOVE);
            auto limit = batch == 0 ? config.candidates / 2 : remainingCandidates;
            auto attempts = batch == 0 ? config.attempts / 2 : config.attempts - config.attempts / 2;
            auto candidates = Neighborhood::generateCandidates(inst, current->schedule(), type, limit, rng, attempts);
            remainingCandidates -= candidates.size();
            if (type == MoveType::MOVE) result.moveGenerated += candidates.size();
            else result.swapGenerated += candidates.size();
            for (const auto& move : candidates) {
                int cost = result.currentCost + current->delta(move).total();
                ++result.evaluated;
                bool tabu = memory.isTabu(current->schedule(), move, t);
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
        lastTenure = config.tenureMode == TSConfig::TenureMode::Fixed ? config.tenure :
            AdaptiveControl::tenure(config.tenureMin, config.tenureMax, config.stagnationWindow,
                                    selectedCost < result.bestCost ? 0 : t - result.bestIteration);
        memory.record(current->schedule(), selected, t, lastTenure);
        if (!result.tenureRecords) result.tenureUsedMin = result.tenureUsedMax = lastTenure;
        result.tenureUsedMin = std::min(result.tenureUsedMin, lastTenure);
        result.tenureUsedMax = std::max(result.tenureUsedMax, lastTenure);
        result.tenureSum += lastTenure; ++result.tenureRecords;
        if (!current->apply(selected)) throw std::logic_error("Selected candidate became infeasible");
        if (selectedCost > result.currentCost) ++result.worsening;
        result.currentCost = selectedCost;
        result.iterations = t;
        if (selected.type == MoveType::MOVE) ++result.moves; else ++result.swaps;
        if (selectedTabu) ++result.aspirations;
        if (selectedCost < result.bestCost) {
            ++result.improvements;
            result.bestCost = selectedCost;
            result.best = current->schedule();
            result.bestIteration = t;
        }
        if (observer) observer(*current, t);
        if (config.logInterval && t % config.logInterval == 0) savePoint(t);
    }
    savePoint(result.iterations);
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
    std::ostringstream id;
    id << std::setprecision(17) << inst.name << "_seed" << config.seed << '_'
       << (config.tenureMode == TSConfig::TenureMode::Fixed ? "fixed" : "dynamic") << '_'
       << (config.operatorMode == TSConfig::OperatorMode::Uniform ? "uniform" : "adaptive")
       << "_i" << config.iterations << "_t" << config.tenure << "_c" << config.candidates << "_a" << config.attempts
       << "_lo" << config.tenureMin << "_hi" << config.tenureMax << "_w" << config.stagnationWindow
       << "_s" << config.feasibilitySamples << "_u" << config.adaptationInterval << "_p" << config.pMin
       << "_r" << config.restartStagnation << "_k" << config.restartMoves << "_l" << config.logInterval;
    auto base = std::filesystem::path(directory) / id.str();
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
           << "\nAttempt budget: " << config.attempts
           << "\nTenure mode: " << (config.tenureMode == TSConfig::TenureMode::Fixed ? "fixed" : "dynamic")
           << "\nOperator mode: " << (config.operatorMode == TSConfig::OperatorMode::Uniform ? "uniform" : "adaptive")
           << "\nTenure min parameter: " << config.tenureMin << "\nTenure max parameter: " << config.tenureMax
           << "\nStagnation window: " << config.stagnationWindow << "\nFeasibility samples: " << config.feasibilitySamples
           << "\nAdaptation interval: " << config.adaptationInterval << "\nP min: " << config.pMin
           << "\nRestart stagnation: " << config.restartStagnation << "\nRestart moves: " << config.restartMoves
           << "\nLog interval: " << config.logInterval << "\nImprovements: " << result.improvements
           << "\nRecoveries: " << result.recoveries << "\nPerturbations: " << result.perturbations
           << "\nPerturbation MOVE: " << result.perturbationMoves << "\nPerturbation SWAP: " << result.perturbationSwaps
           << "\nMOVE executed total: " << result.moves + result.perturbationMoves
           << "\nSWAP executed total: " << result.swaps + result.perturbationSwaps
           << "\nTenure minimum used: " << result.tenureUsedMin << "\nTenure maximum used: " << result.tenureUsedMax
           << "\nTenure average: " << (result.tenureRecords ? result.tenureSum / result.tenureRecords : 0)
           << "\nMOVE selected: " << result.moveSelected << "\nSWAP selected: " << result.swapSelected
           << "\nMOVE generated: " << result.moveGenerated << "\nSWAP generated: " << result.swapGenerated
           << "\nAlternatives: " << result.alternatives << "\nAdaptations: " << result.adaptations
           << "\nQ MOVE: " << result.qMove << "\nQ SWAP: " << result.qSwap
           << "\nP MOVE: " << result.pMove << "\nP SWAP: " << 1-result.pMove << '\n';
    if (config.logInterval) {
        std::ofstream csv(base.string() + ".csv");
        csv << "iteration,elapsed_ms,current_cost,best_cost,tabu_tenure,p_move,p_swap,q_move,q_swap,stagnation\n";
        for (const auto& point : result.convergence)
            csv << point.iteration << ',' << point.elapsedMs << ',' << point.currentCost << ',' << point.bestCost << ','
                << point.tenure << ',' << point.pMove << ',' << 1-point.pMove << ',' << point.qMove << ',' << point.qSwap << ',' << point.stagnation << '\n';
        csv.close(); if (!csv) throw std::runtime_error("Failed writing convergence CSV");
    }
    sol.close(); report.close();
    if (!sol || !report) throw std::runtime_error("Failed writing TS output");
    return base.string() + ".sol";
}
