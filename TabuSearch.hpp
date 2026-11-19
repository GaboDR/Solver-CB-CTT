#ifndef TABU_SEARCH_HPP
#define TABU_SEARCH_HPP
#include "DeltaEvaluator.hpp"
#include "AdaptiveControl.hpp"
#include <cstdint>
#include <functional>
#include <string>

class TabuMemory {
public:
    // Iterations are 1-based; endpoints and source IDs must be valid.
    TabuMemory(const Instance& inst, std::size_t tenure);
    bool isTabu(const Neighborhood::Schedule& schedule, const Move& move, std::size_t iteration) const;
    // Called BEFORE apply, only for a selected/accepted feasible move.
    void record(const Neighborhood::Schedule& schedule, const Move& move, std::size_t iteration);
    void record(const Neighborhood::Schedule& schedule, const Move& move, std::size_t iteration, std::size_t tenure);
    static bool admissible(bool tabu, int candidateCost, int bestCost) {
        return !tabu || candidateCost < bestCost;
    }
private:
    std::size_t tenure_;
    std::vector<std::vector<std::vector<std::size_t>>> until_;
};

struct TSConfig {
    std::uint32_t seed = 42;
    std::size_t iterations = 10000, tenure = 7, candidates = 100, attempts = 10000;
    enum class TenureMode { Fixed, Dynamic } tenureMode = TenureMode::Fixed;
    enum class OperatorMode { Uniform, Adaptive } operatorMode = OperatorMode::Uniform;
    std::size_t tenureMin = 5, tenureMax = 20, stagnationWindow = 200;
    std::size_t feasibilitySamples = 100, adaptationInterval = 50;
    double pMin = 0.10;
    std::size_t restartStagnation = 0, restartMoves = 10, logInterval = 0;
};
struct ConvergencePoint {
    std::size_t iteration, tenure, stagnation;
    double elapsedMs, pMove, qMove, qSwap;
    int currentCost, bestCost;
};
struct TSResult {
    Neighborhood::Schedule best;
    int initialCost = 0, bestCost = 0, currentCost = 0;
    std::size_t iterations = 0, bestIteration = 0, moves = 0, swaps = 0;
    std::size_t evaluated = 0, tabuRejected = 0, aspirations = 0, worsening = 0;
    double seconds = 0;
    bool stoppedWithoutCandidate = false;
    std::size_t improvements = 0, recoveries = 0, perturbations = 0, lastRecovery = 0;
    std::size_t perturbationMoves = 0, perturbationSwaps = 0;
    std::size_t moveSelected = 0, swapSelected = 0, moveGenerated = 0, swapGenerated = 0, alternatives = 0;
    std::size_t tenureUsedMin = 0, tenureUsedMax = 0, tenureRecords = 0, adaptations = 0;
    double tenureSum = 0, pMove = 0.5, qMove = 0, qSwap = 0;
    std::vector<ConvergencePoint> convergence;
};
class TabuSearch {
public:
    // Optional observer receives initial and every accepted state, for auditing.
    using Observer = std::function<void(const DeltaEvaluator&, std::size_t)>;
    static TSResult run(const Instance& inst, const TSConfig& config = {}, Observer observer = {});
    // Exports best global matrix, separate from Greedy. Returns .sol path.
    static std::string exportBest(const Instance& inst, const TSResult& result,
                                  const TSConfig& config, const std::string& directory = "solutions/ts");
};
#endif
