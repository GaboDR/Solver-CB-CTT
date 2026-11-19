#include "TabuSearch.hpp"
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

int main(int argc, char** argv) {
    try {
        if (argc < 2) throw std::invalid_argument(
            "Usage: cbctt_ts instance.ctt [--seed N --iterations N --tenure N --candidates N --attempts N]");
        TSConfig config;
        std::string outputDirectory = "solutions/ts";
        for (int i = 2; i < argc; i += 2) {
            std::string option = argv[i];
            if (i + 1 >= argc) throw std::invalid_argument("Missing value for " + option);
            std::string value = argv[i + 1];
            if (option == "--output-dir") { outputDirectory = value; continue; }
            if (option == "--tenure-mode") {
                if (value == "fixed") config.tenureMode = TSConfig::TenureMode::Fixed;
                else if (value == "dynamic") config.tenureMode = TSConfig::TenureMode::Dynamic;
                else throw std::invalid_argument("Expected fixed|dynamic");
                continue;
            }
            if (option == "--operator-mode") {
                if (value == "uniform") config.operatorMode = TSConfig::OperatorMode::Uniform;
                else if (value == "adaptive") config.operatorMode = TSConfig::OperatorMode::Adaptive;
                else throw std::invalid_argument("Expected uniform|adaptive");
                continue;
            }
            if (option == "--p-min") {
                std::size_t used = 0;
                config.pMin = std::stod(value, &used);
                if (used != value.size()) throw std::invalid_argument("Invalid p-min");
                continue;
            }
            if (value.empty() || value.find_first_not_of("0123456789") != std::string::npos)
                throw std::invalid_argument("Expected nonnegative integer for " + option);
            auto n = std::stoull(value);
            if (n > std::numeric_limits<std::uint32_t>::max()) throw std::invalid_argument("Value too large");
            if (option == "--seed") config.seed = static_cast<std::uint32_t>(n);
            else if (option == "--iterations") config.iterations = n;
            else if (option == "--tenure") config.tenure = n;
            else if (option == "--candidates") config.candidates = n;
            else if (option == "--attempts") config.attempts = n;
            else if (option == "--tenure-min") config.tenureMin = n;
            else if (option == "--tenure-max") config.tenureMax = n;
            else if (option == "--stagnation-window") config.stagnationWindow = n;
            else if (option == "--feasibility-samples") config.feasibilitySamples = n;
            else if (option == "--adaptation-interval") config.adaptationInterval = n;
            else if (option == "--restart-stagnation") config.restartStagnation = n;
            else if (option == "--restart-moves") config.restartMoves = n;
            else if (option == "--log-interval") config.logInterval = n;
            else throw std::invalid_argument("Unknown option: " + option);
        }
        Instance inst;
        if (!inst.loadFromFile(argv[1])) return 1;
        auto result = TabuSearch::run(inst, config);
        auto path = TabuSearch::exportBest(inst, result, config, outputDirectory);
        std::ifstream report(path.substr(0, path.size() - 4) + ".txt");
        std::cout << report.rdbuf() << "Best solution: " << path << '\n';
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << '\n';
        return 1;
    }
}
