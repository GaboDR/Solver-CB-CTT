#ifndef NEIGHBORHOOD_HPP
#define NEIGHBORHOOD_HPP

#include "Instance.hpp"
#include <cstddef>
#include <random>
#include <vector>

enum class MoveType { MOVE, SWAP };

struct Move {
    MoveType type;
    int p1, r1;
    int p2, r2;
};

class Neighborhood {
public:
    using Schedule = std::vector<std::vector<int>>;

    // Precondition: a loaded Instance and a feasible P x R matrix, with -1
    // for empty cells. These local checks preserve feasibility; they do not
    // diagnose pre-existing violations elsewhere in the matrix.
    static bool isFeasible(const Instance& inst, const Schedule& schedule,
                           const Move& move);
    // Rejected operations leave the matrix untouched.
    static bool apply(const Instance& inst, Schedule& schedule, const Move& move);
    // Undo immediately after a successful apply (or undo a sequence in
    // reverse order). Checks the inverse move,
    // not the original MOVE occupancy preconditions. No course IDs are cached.
    static bool undo(const Instance& inst, Schedule& schedule, const Move& move);

    // Bounded random sampling, without enumerating all cell pairs. The RNG is
    // owned/seeded by the caller. SWAP pairs are unordered for deduplication.
    // May return fewer than max_candidates, including zero, even if feasible
    // neighbors exist: exhausting max_attempts is NOT proof of their absence.
    // A future search can try the other operator or sample again.
    static std::vector<Move> generateCandidates(
        const Instance& inst, const Schedule& schedule, MoveType type,
        std::size_t max_candidates, std::mt19937& rng,
        std::size_t max_attempts = 10000);
};

#endif
