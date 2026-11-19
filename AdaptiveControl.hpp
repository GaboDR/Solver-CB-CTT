#ifndef ADAPTIVE_CONTROL_HPP
#define ADAPTIVE_CONTROL_HPP
#include "Neighborhood.hpp"

struct FeasibilityEstimate {
    std::size_t moveSampled = 0, moveFeasible = 0, swapSampled = 0, swapFeasible = 0;
    double qMove() const { return moveSampled ? double(moveFeasible) / moveSampled : 0; }
    double qSwap() const { return swapSampled ? double(swapFeasible) / swapSampled : 0; }
};
namespace AdaptiveControl {
// Parameters min/max/window must fit uint32_t; integer floor is exact.
std::size_t tenure(std::size_t minimum, std::size_t maximum, std::size_t window, std::size_t stagnation);
double moveProbability(double qMove, double qSwap, double pMin);
// Structural SWAP proposals are sampled uniformly without rejection by
// grouping occupied cells by course. No full pair enumeration or mutation.
// Same feasible-matrix/loaded-instance precondition as Neighborhood.
FeasibilityEstimate estimate(const Instance& inst, const Neighborhood::Schedule& schedule,
                             std::size_t samples, std::mt19937& rng);
}
#endif
