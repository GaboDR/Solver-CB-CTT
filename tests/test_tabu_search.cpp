#include "TabuSearch.hpp"
#include "Evaluator.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
void memoryTests() {
    Instance inst;
    inst.num_courses = 2; inst.total_periods = 3; inst.num_rooms = 2;
    Neighborhood::Schedule s{{0, -1}, {-1, 1}, {-1, -1}};
    for (auto type : {MoveType::MOVE, MoveType::SWAP}) {
        auto current = s;
        Move m = type == MoveType::MOVE ? Move{type, 0, 0, 2, 0} : Move{type, 0, 0, 1, 1};
        TabuMemory memory(inst, 7);
        check(!memory.isTabu(current, m, 1), "Initially tabu");
        memory.record(current, m, 10);
        std::swap(current[m.p1][m.r1], current[m.p2][m.r2]);
        Move inverse{type, m.p2, m.r2, m.p1, m.r1};
        auto snapshot = current;
        for (std::size_t t = 11; t <= 17; ++t) {
            check(memory.isTabu(current, inverse, t), "Early expiration");
            check(!TabuMemory::admissible(true, 100, 100), "Equality is not aspiration");
            check(!TabuMemory::admissible(true, 101, 100), "Worse aspiration");
            check(TabuMemory::admissible(true, 99, 100), "Strict aspiration denied");
        }
        check(!memory.isTabu(current, inverse, 18), "Late expiration");
        if (type == MoveType::SWAP) {
            // Probe each assignment attribute separately (occupancy is checked
            // by Neighborhood, not by the tabu-attribute lookup).
            check(memory.isTabu(current, {MoveType::MOVE, 1, 1, 0, 0}, 11), "Missing first SWAP attribute");
            check(memory.isTabu(current, {MoveType::MOVE, 0, 0, 1, 1}, 11), "Missing second SWAP attribute");
        }
        check(current == snapshot, "Tabu lookup mutated solution");
        TabuMemory zero(inst, 0);
        zero.record(s, m, 10);
        check(!zero.isTabu(current, inverse, 11), "Zero tenure did not expire");
    }
    check(TabuMemory::admissible(false, 1000, 1), "Non-improving admissible move denied");
}
void exportTest(const Instance& inst, const TSResult& result, const TSConfig& config) {
    auto directory = std::filesystem::temp_directory_path() / "cbctt-ts-export-test";
    auto path = TabuSearch::exportBest(inst, result, config, directory.string());
    std::ifstream input(path);
    Neighborhood::Schedule loaded(inst.total_periods, std::vector<int>(inst.num_rooms, -1));
    std::string c, r;
    int d, slot;
    while (input >> c >> r >> d >> slot) {
        int p = inst.getPeriod(d, slot), room = inst.room_map.at(r);
        check(loaded.at(p).at(room) == -1, "Duplicate exported cell");
        loaded[p][room] = inst.course_map.at(c);
    }
    check(input.eof() && loaded == result.best, "Export is not the best global matrix");
    auto full = Evaluator::evaluateMatrix(inst, loaded);
    check(full.is_feasible() && full.total_soft_cost() == result.bestCost, "Invalid exported best");
    std::filesystem::remove(path);
    std::filesystem::remove(path.substr(0, path.size() - 4) + ".txt");
    std::filesystem::remove(directory);
}
}
int main() {
    try {
        memoryTests();
        for (const auto& reference : std::vector<std::pair<std::string, int>>{
                 {"comp01",187}, {"comp02",337}, {"comp05",756}, {"comp11",57}, {"comp12",770}}) {
            Instance inst;
            check(inst.loadFromFile("Instances/" + reference.first + ".ctt"), "Load failed");
            TSConfig config; config.iterations = 1000;
            std::size_t observed = 0;
            auto observer = [&](const DeltaEvaluator& e, std::size_t) {
                auto full = Evaluator::evaluateMatrix(inst, e.schedule());
                check(full.is_feasible(), "Accepted infeasible schedule / lecture count changed");
                check(full.total_soft_cost() == e.state().cost.total(), "Accepted cost mismatch");
                ++observed;
            };
            auto a = TabuSearch::run(inst, config, observer);
            auto b = TabuSearch::run(inst, config);
            check(a.initialCost == reference.second && a.bestCost <= a.initialCost, "Greedy/best regression");
            check(observed == a.iterations + 1 && a.iterations == a.moves + a.swaps, "Iteration accounting");
            check(a.best == b.best && a.bestCost == b.bestCost && a.currentCost == b.currentCost &&
                  a.iterations == b.iterations && a.bestIteration == b.bestIteration &&
                  a.moves == b.moves && a.swaps == b.swaps && a.evaluated == b.evaluated &&
                  a.tabuRejected == b.tabuRejected && a.aspirations == b.aspirations &&
                  a.worsening == b.worsening && a.stoppedWithoutCandidate == b.stoppedWithoutCandidate,
                  "Seed reproducibility failed");
            check(a.worsening > 0, "Search never accepted worsening moves");
            check(a.evaluated <= config.candidates * (a.iterations + 1), "Candidate budget exceeded");
            exportTest(inst, a, config);
            std::cout << reference.first << ": Greedy=" << a.initialCost << " TS=" << a.bestCost
                      << " iterations=" << a.iterations << " seconds(audited)=" << a.seconds
                      << " worsening=" << a.worsening << " aspirations=" << a.aspirations << '\n';
        }
        Instance singleton;
        singleton.num_courses = singleton.num_rooms = singleton.days = singleton.periods_per_day =
            singleton.total_periods = singleton.num_teachers = 1;
        singleton.courses.push_back({0,"C","T",0,1,1,1,{}});
        singleton.rooms.push_back({0,"R",1});
        singleton.conflict = {{true}}; singleton.unavailable = {{false}};
        TSConfig config;
        auto empty = TabuSearch::run(singleton, config);
        check(empty.stoppedWithoutCandidate && empty.iterations == 0 && empty.bestCost == 0, "Empty neighborhood termination");
        config.iterations = 0;
        auto zero = TabuSearch::run(singleton, config);
        check(zero.iterations == 0 && !zero.stoppedWithoutCandidate, "Zero iteration budget");
        // SWAP has no pair; MOVE succeeds through fallback, then its only
        // reverse is tabu. Both empty and all-prohibited paths must terminate.
        singleton.num_rooms = 2;
        singleton.rooms.push_back({1,"R1",1});
        config.iterations = 100;
        std::mt19937 probe(config.seed);
        check(!std::bernoulli_distribution(0.5)(probe), "Expected initial SWAP for fallback test");
        auto blocked = TabuSearch::run(singleton, config);
        check(blocked.iterations == 1 && blocked.moves == 1 && blocked.swaps == 0 &&
              blocked.stoppedWithoutCandidate && blocked.tabuRejected > 0,
              "Fallback/all-tabu termination failed");
        std::cout << "PASS: tabu attributes, expiration, aspiration, audited TS, reproducibility, export, empty sampling\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
