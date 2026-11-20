// Parallel portfolio for CB-CTT. Linux/WSL (POSIX fork/exec/waitpid).
// Each child runs the existing sequential cbctt_ts, without shared mutable state.
#include "Instance.hpp"
#include "Evaluator.hpp"

#include <array>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace fs = std::filesystem;
namespace {

struct Variant {
    std::string name, tenure, op;
};
const std::array<Variant, 4> variants{{
    {"TS-A", "fixed", "uniform"},
    {"TS-B", "dynamic", "uniform"},
    {"TS-C", "fixed", "adaptive"},
    {"TS-D", "dynamic", "adaptive"}
}};

struct Child {
    Variant variant;
    pid_t pid = -1;
    fs::path directory, log;
    std::string solution;
    int greedy = -1, best = -1;
    double solverSeconds = 0.0;
};

struct TemporaryDirectory {
    fs::path path;
    TemporaryDirectory() {
        auto pattern = (fs::temp_directory_path() / "cbctt_portfolio_XXXXXX").string();
        std::vector<char> writable(pattern.begin(), pattern.end());
        writable.push_back('\0');
        if (!mkdtemp(writable.data()))
            throw std::runtime_error("Cannot create temporary directory: " + std::string(std::strerror(errno)));
        path = writable.data();
    }
    ~TemporaryDirectory() {
        std::error_code ec;
        if (!path.empty()) fs::remove_all(path, ec);
    }
    TemporaryDirectory(const TemporaryDirectory&) = delete;
    TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;
};

void usage() {
    std::cout << "Usage: ./cbctt_portfolio instance.ctt [TS numeric options] "
                 "[--output-dir DIR] [--sequential]\n"
                 "Example: ./cbctt_portfolio Instances/comp02.ctt --seed 42 "
                 "--iterations 100000 --candidates 100\n"
                 "The coordinator runs TS-A/B/C/D, verifies all .sol files with Evaluator, "
                 "and exports the best feasible result.\n"
                 "--sequential is provided only to compare runtime and reproducibility.\n";
}

bool isNumericOption(const std::string& option) {
    static const std::vector<std::string> options{
        "--seed", "--iterations", "--tenure", "--candidates", "--attempts",
        "--tenure-min", "--tenure-max", "--stagnation-window",
        "--feasibility-samples", "--adaptation-interval", "--restart-stagnation",
        "--restart-moves", "--log-interval"
    };
    for (const auto& accepted : options) if (option == accepted) return true;
    return option == "--p-min";
}

unsigned long long numeric(const std::string& s, const std::string& label) {
    if (s.empty() || s.find_first_not_of("0123456789") != std::string::npos)
        throw std::invalid_argument("Expected unsigned integer for " + label);
    std::size_t used = 0;
    auto n = std::stoull(s, &used);
    if (used != s.size() || n > std::numeric_limits<std::uint32_t>::max())
        throw std::invalid_argument("Value out of range for " + label);
    return n;
}

std::map<std::string, std::string> loadReport(const fs::path& report) {
    std::ifstream file(report);
    if (!file) throw std::runtime_error("Missing report: " + report.string());
    std::map<std::string, std::string> fields;
    std::string line;
    while (std::getline(file, line)) {
        auto pos = line.find(": ");
        if (pos != std::string::npos) fields[line.substr(0, pos)] = line.substr(pos + 2);
    }
    return fields;
}

std::string required(const std::map<std::string, std::string>& fields, const std::string& field) {
    const auto it = fields.find(field);
    if (it == fields.end()) throw std::runtime_error("Missing report field: " + field);
    return it->second;
}

int parseCost(const std::string& value, const std::string& name) {
    std::size_t used = 0;
    int n = std::stoi(value, &used);
    if (used != value.size() || n < 0) throw std::runtime_error("Invalid " + name);
    return n;
}

// Deliberately re-read exported assignments and independently validate UD2.
// This also catches a missing, corrupted, incomplete, or duplicate .sol file.
int evaluateSolution(const Instance& inst, const fs::path& path) {
    std::ifstream file(path);
    if (!file) throw std::runtime_error("Cannot read solution: " + path.string());
    std::vector<std::vector<int>> schedule(inst.total_periods,
                                            std::vector<int>(inst.num_rooms, -1));
    std::string line;
    std::size_t lineNumber = 0;
    while (std::getline(file, line)) {
        ++lineNumber;
        if (line.empty()) continue;
        std::istringstream input(line);
        std::string course, room, extra;
        int day = -1, slot = -1;
        if (!(input >> course >> room >> day >> slot) || (input >> extra))
            throw std::runtime_error("Malformed .sol line " + std::to_string(lineNumber));
        auto c = inst.course_map.find(course), r = inst.room_map.find(room);
        if (c == inst.course_map.end() || r == inst.room_map.end() ||
            day < 0 || day >= inst.days || slot < 0 || slot >= inst.periods_per_day)
            throw std::runtime_error("Unknown course/room or invalid day/slot in .sol line " +
                                     std::to_string(lineNumber));
        const int p = inst.getPeriod(day, slot);
        if (schedule[p][r->second] != -1)
            throw std::runtime_error("Duplicate room/period in .sol line " + std::to_string(lineNumber));
        schedule[p][r->second] = c->second;
    }
    if (file.bad()) throw std::runtime_error("I/O failure while reading " + path.string());
    auto eval = Evaluator::evaluateMatrix(inst, schedule);
    if (!eval.is_feasible())
        throw std::runtime_error("Exported timetable is infeasible (hard=" +
                                 std::to_string(eval.total_hard_violations()) + ")");
    return eval.total_soft_cost();
}

pid_t launch(const fs::path& solver, const std::string& instance,
             const std::vector<std::string>& options, Child& child) {
    fs::create_directories(child.directory);
    child.log = child.directory / "stdout.log";
    std::vector<std::string> args{solver.string(), instance};
    args.insert(args.end(), options.begin(), options.end());
    args.insert(args.end(), {"--tenure-mode", child.variant.tenure,
                              "--operator-mode", child.variant.op,
                              "--output-dir", child.directory.string()});
    pid_t pid = fork();
    if (pid < 0) throw std::runtime_error("fork() failed: " + std::string(std::strerror(errno)));
    if (pid == 0) {
        int fd = open(child.log.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
        if (fd < 0 || dup2(fd, STDOUT_FILENO) < 0 || dup2(fd, STDERR_FILENO) < 0)
            _exit(127);
        close(fd);
        std::vector<char*> raw;
        for (auto& arg : args) raw.push_back(const_cast<char*>(arg.c_str()));
        raw.push_back(nullptr);
        execv(solver.c_str(), raw.data());
        _exit(127);
    }
    child.pid = pid;
    return pid;
}

void awaitChild(Child& child) {
    if (child.pid < 0) throw std::logic_error("No child process started");
    int status = 0;
    pid_t done;
    do { done = waitpid(child.pid, &status, 0); } while (done < 0 && errno == EINTR);
    child.pid = -1;
    if (done < 0 || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        std::ifstream file(child.log);
        std::ostringstream output;
        output << file.rdbuf();
        throw std::runtime_error(child.variant.name + " failed. Output:\n" + output.str());
    }
}

void inspectResult(const Instance& inst, Child& child,
                   unsigned long long seed, const std::string& expectedInstance) {
    std::ifstream log(child.log);
    if (!log) throw std::runtime_error("Missing solver output for " + child.variant.name);
    std::string line, solution;
    while (std::getline(log, line)) {
        const std::string prefix = "Best solution: ";
        if (line.compare(0, prefix.size(), prefix) == 0) solution = line.substr(prefix.size());
    }
    if (solution.empty()) throw std::runtime_error("Missing .sol path for " + child.variant.name);
    const fs::path sol = fs::canonical(solution);
    if (sol.parent_path() != fs::canonical(child.directory) || sol.extension() != ".sol")
        throw std::runtime_error("Unexpected .sol path for " + child.variant.name);
    fs::path report = sol;
    report.replace_extension(".txt");
    auto fields = loadReport(report);
    if (required(fields, "Final feasible") != "YES" ||
        required(fields, "Instance") != expectedInstance ||
        required(fields, "Tenure mode") != child.variant.tenure ||
        required(fields, "Operator mode") != child.variant.op ||
        required(fields, "Seed") != std::to_string(seed))
        throw std::runtime_error("Mismatched report for " + child.variant.name);
    child.greedy = parseCost(required(fields, "Greedy"), "Greedy");
    child.best = parseCost(required(fields, "Best TS"), "Best TS");
    std::size_t used = 0;
    child.solverSeconds = std::stod(required(fields, "Seconds (including Greedy)"), &used);
    if (child.greedy < child.best)
        throw std::runtime_error("Best TS is worse than Greedy for " + child.variant.name);
    int recalculated = evaluateSolution(inst, sol);
    if (recalculated != child.best)
        throw std::runtime_error("UD2 independent cost differs for " + child.variant.name);
    child.solution = sol.string();
}

std::string nameForRun(const std::string& instanceFile, unsigned long long seed,
                       unsigned long long iterations) {
    // Timestamp and PID avoid overwriting past experiments.
    auto timeMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    return instanceFile + "_seed" + std::to_string(seed) + "_i" +
           std::to_string(iterations) + "_" + std::to_string(timeMs) +
           "_pid" + std::to_string(getpid());
}

} // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::string(argv[1]) == "--help") { usage(); return 0; }
        if (argc < 2) { usage(); return 2; }

        const std::string instancePath = argv[1];
        const std::string instanceFile = fs::path(instancePath).stem().string();
        std::vector<std::string> common;
        fs::path outputDir = "solutions/portfolio";
        bool sequential = false;
        unsigned long long seed = 42, iterations = 10000;
        for (int i = 2; i < argc; ++i) {
            const std::string option = argv[i];
            if (option == "--sequential") { sequential = true; continue; }
            if (option == "--tenure-mode" || option == "--operator-mode")
                throw std::invalid_argument("Portfolio controls tenure/operator modes internally");
            if (i + 1 >= argc) throw std::invalid_argument("Missing argument after " + option);
            const std::string value = argv[++i];
            if (option == "--output-dir") { outputDir = value; continue; }
            if (!isNumericOption(option)) throw std::invalid_argument("Unknown option: " + option);
            if (option != "--p-min") {
                const auto number = numeric(value, option);
                if (option == "--seed") seed = number;
                if (option == "--iterations") iterations = number;
            }
            common.insert(common.end(), {option, value});
        }
        Instance inst;
        if (!inst.loadFromFile(instancePath)) return 1;
        const fs::path solver = fs::absolute("./cbctt_ts");
        if (!fs::exists(solver))
            throw std::runtime_error("Missing cbctt_ts. Run 'make cbctt_portfolio' first.");
        TemporaryDirectory temporary;
        std::vector<Child> children;
        children.reserve(variants.size());
        for (const auto& variant : variants) {
            Child child;
            child.variant = variant;
            child.directory = temporary.path / variant.name;
            children.push_back(child);
        }
        const auto begin = std::chrono::steady_clock::now();
        // All children start before the first wait in parallel mode.
        for (auto& child : children) {
            launch(solver, instancePath, common, child);
            if (sequential) awaitChild(child);
        }
        if (!sequential) {
            // Wait for every child even when one fails; avoids abandoning processes.
            std::string error;
            for (auto& child : children) {
                try { awaitChild(child); }
                catch (const std::exception& e) { if (error.empty()) error = e.what(); }
            }
            if (!error.empty()) throw std::runtime_error(error);
        }
        int bestIndex = -1;
        int greedy = -1;
        for (std::size_t i = 0; i < children.size(); ++i) {
            inspectResult(inst, children[i], seed, inst.name);
            if (greedy == -1) greedy = children[i].greedy;
            if (children[i].greedy != greedy)
                throw std::runtime_error("Greedy costs do not match across variants");
            // Deterministic tie-break TS-A, TS-B, TS-C, TS-D.
            if (bestIndex == -1 || children[i].best < children[bestIndex].best)
                bestIndex = static_cast<int>(i);
        }
        fs::create_directories(outputDir);
        const auto base = outputDir / nameForRun(instanceFile, seed, iterations);
        fs::path sol = base;
        sol += ".sol";
        fs::path report = base;
        report += ".txt";
        fs::copy_file(children[bestIndex].solution, sol, fs::copy_options::none);
        const double wallSeconds = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - begin).count();
        std::ofstream summary(report);
        if (!summary) { fs::remove(sol); throw std::runtime_error("Cannot create portfolio report"); }
        summary << std::setprecision(10);
        summary << "Instance file: " << instancePath << "\nInstance: " << inst.name << "\nSeed: " << seed
                << "\nIterations: " << iterations
                << "\nExecution: " << (sequential ? "sequential" : "parallel")
                << "\nGreedy: " << greedy << "\nFinal feasible: YES"
                << "\nBest portfolio: " << children[bestIndex].best
                << "\nWinner: " << children[bestIndex].variant.name
                << "\nWall seconds: " << wallSeconds << '\n';
        for (const auto& child : children)
            summary << child.variant.name << " (" << child.variant.tenure << '/'
                    << child.variant.op << "): " << child.best
                    << " | Solver seconds: " << child.solverSeconds << '\n';
        summary << "Parameters passed to all four TS:\n";
        for (std::size_t i = 0; i + 1 < common.size(); i += 2)
            summary << "  " << common[i] << " " << common[i + 1] << '\n';
        summary.close();
        if (!summary) { fs::remove(sol); fs::remove(report); throw std::runtime_error("Cannot write portfolio report"); }

        std::cout << "Instance: " << instanceFile << " (" << inst.name << ")\n"
                  << "Greedy: " << greedy << '\n';
        for (const auto& child : children)
            std::cout << child.variant.name << " (" << child.variant.tenure << '/'
                      << child.variant.op << "): " << child.best << '\n';
        std::cout << "Winner: " << children[bestIndex].variant.name
                  << "\nBest portfolio: " << children[bestIndex].best
                  << "\nFinal feasible: YES\nExecution: "
                  << (sequential ? "sequential" : "parallel")
                  << "\nWall seconds: " << std::fixed << std::setprecision(3) << wallSeconds
                  << "\nBest solution: " << sol.string() << "\nReport: " << report.string() << '\n';
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Portfolio error: " << e.what() << '\n';
        return 1;
    }
}
