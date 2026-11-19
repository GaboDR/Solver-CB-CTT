#include "AdaptiveControl.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>

std::size_t AdaptiveControl::tenure(std::size_t lo, std::size_t hi, std::size_t window, std::size_t e) {
    const auto limit = std::numeric_limits<std::uint32_t>::max();
    if (lo > hi || hi > limit || window == 0 || window > limit)
        throw std::invalid_argument("Invalid dynamic tenure bounds/window");
    return lo + static_cast<std::uint64_t>(hi - lo) * std::min(e, window) / window;
}

double AdaptiveControl::moveProbability(double qm, double qs, double p) {
    if (!std::isfinite(qm) || !std::isfinite(qs) || !std::isfinite(p) ||
        qm < 0 || qm > 1 || qs < 0 || qs > 1 || p < 0 || p > 0.5)
        throw std::invalid_argument("Invalid probability/rate");
    if (qm == qs) return 0.5; // includes two zero estimates
    return std::clamp(p + (1 - 2 * p) * qm / (qm + qs + 1e-12), p, 1 - p);
}

FeasibilityEstimate AdaptiveControl::estimate(const Instance& inst, const Neighborhood::Schedule& s,
                                               std::size_t samples, std::mt19937& rng) {
    using Cell = std::pair<int,int>;
    std::vector<Cell> occupied, empty;
    std::vector<std::vector<Cell>> groups(inst.num_courses);
    for (int p = 0; p < inst.total_periods; ++p) for (int r = 0; r < inst.num_rooms; ++r) {
        if (s[p][r] == -1) empty.emplace_back(p,r);
        else { occupied.emplace_back(p,r); groups[s[p][r]].emplace_back(p,r); }
    }
    FeasibilityEstimate result;
    auto draw = [&](const std::vector<Cell>& pool) {
        return pool[std::uniform_int_distribution<std::size_t>(0, pool.size() - 1)(rng)];
    };
    if (!occupied.empty() && !empty.empty()) for (std::size_t i = 0; i < samples; ++i) {
        auto a = draw(occupied), b = draw(empty);
        ++result.moveSampled;
        result.moveFeasible += Neighborhood::isFeasible(inst, s, {MoveType::MOVE,a.first,a.second,b.first,b.second});
    }
    std::vector<Cell> sorted;
    std::vector<std::size_t> begin(inst.num_courses), end(inst.num_courses);
    std::vector<double> weights(inst.num_courses);
    for (int c = 0; c < inst.num_courses; ++c) {
        begin[c] = sorted.size();
        sorted.insert(sorted.end(), groups[c].begin(), groups[c].end());
        end[c] = sorted.size();
        weights[c] = double(groups[c].size()) * (occupied.size() - groups[c].size());
    }
    if (std::count_if(weights.begin(), weights.end(), [](double w){ return w > 0; }) > 0) {
        std::discrete_distribution<int> course(weights.begin(), weights.end());
        for (std::size_t i = 0; i < samples; ++i) {
            int c = course(rng);
            auto a = draw(groups[c]);
            auto j = std::uniform_int_distribution<std::size_t>(0, sorted.size() - groups[c].size() - 1)(rng);
            if (j >= begin[c]) j += end[c] - begin[c];
            auto b = sorted[j];
            ++result.swapSampled;
            result.swapFeasible += Neighborhood::isFeasible(inst, s, {MoveType::SWAP,a.first,a.second,b.first,b.second});
        }
    }
    return result;
}
