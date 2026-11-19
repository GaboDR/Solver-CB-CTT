#include "TabuSearch.hpp"
#include "Evaluator.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

namespace {
void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
void units() {
    check(AdaptiveControl::tenure(5,20,200,0) == 5, "Minimum tenure");
    check(AdaptiveControl::tenure(5,20,200,100) == 12, "Floor tenure");
    for (std::size_t e=0, last=5; e<1000; ++e) {
        auto t=AdaptiveControl::tenure(5,20,200,e);
        check(t>=last && t>=5 && t<=20, "Tenure monotonic/bounds"); last=t;
        if(e>=200) check(t==20,"Tenure saturation");
    }
    check(AdaptiveControl::moveProbability(0,0,.1)==.5 &&
          AdaptiveControl::moveProbability(.7,.7,.1)==.5,"Equal/zero rates");
    for (int a=0; a<=10; ++a) for(int b=0; b<=10; ++b) {
        double p=AdaptiveControl::moveProbability(a/10.,b/10.,.1);
        check(p>=.1 && p<=.9 && std::abs(p+(1-p)-1)<1e-15,"Probability bounds");
        if(a>b) check(p>.5,"MOVE preference");
        if(b>a) check(p<.5,"SWAP preference");
    }
    Instance inst;
    inst.num_courses=2; inst.num_rooms=2; inst.total_periods=2;
    inst.conflict={{true,false},{false,true}}; inst.unavailable={{false,true},{false,true}};
    Neighborhood::Schedule s{{0,1},{-1,-1}};
    std::mt19937 rng(42), repeat(42);
    auto a=AdaptiveControl::estimate(inst,s,100,rng);
    auto b=AdaptiveControl::estimate(inst,s,100,repeat);
    check(a.moveSampled==100 && a.swapSampled==100 && a.qMove()==0 && a.qSwap()==1,"Structural sample rates");
    check(a.moveFeasible==b.moveFeasible && a.swapFeasible==b.swapFeasible && rng==repeat,"Estimate reproducibility");
    check(s==Neighborhood::Schedule{{0,1},{-1,-1}},"Sampling changed schedule");
    auto empty = AdaptiveControl::estimate(inst, {{-1,-1},{-1,-1}}, 100, rng);
    check(empty.moveSampled==0 && empty.swapSampled==0,"Empty structural pools");
    TabuMemory memory(inst,7);
    Move move{MoveType::MOVE,0,0,1,0};
    memory.record(s,move,1,5);
    auto changed=s; std::swap(changed[0][0],changed[1][0]);
    Move inverse{MoveType::MOVE,1,0,0,0};
    memory.record(changed,{MoveType::MOVE,0,1,1,1},2,20);
    check(memory.isTabu(changed,inverse,6) && !memory.isTabu(changed,inverse,7),"Retroactive expiration change");
    check(TabuMemory::admissible(true,9,10) && !TabuMemory::admissible(true,10,10),"Aspiration");
}
Instance flat() {
    Instance inst;
    inst.num_courses=inst.num_rooms=inst.days=inst.num_teachers=1;
    inst.periods_per_day=inst.total_periods=2;
    inst.courses.push_back({0,"C","T",0,1,1,1,{}});
    inst.rooms.push_back({0,"R",1}); inst.conflict={{true}};
    inst.unavailable={{false,false}};
    return inst;
}
void recovery() {
    auto inst=flat(); TSConfig c;
    c.iterations=20; c.tenure=0; c.restartStagnation=4; c.restartMoves=3; c.logInterval=1;
    std::size_t callbacks=0;
    auto result=TabuSearch::run(inst,c,[&](const DeltaEvaluator& state,std::size_t t) {
        check(Evaluator::evaluateMatrix(inst,state.schedule()).is_feasible(),"Recovery infeasible");
        check(state.state()==DeltaEvaluator(inst,state.schedule()).state(),"Recovery counters");
        if(callbacks==5) check(t==4 && state.schedule()==Neighborhood::Schedule{{0},{-1}},"Best not restored before perturbation");
        ++callbacks;
    });
    check(result.recoveries==4 && result.perturbations==12 && result.bestCost==0,"Recovery timing/best preservation");
    check(result.perturbations==result.perturbationMoves+result.perturbationSwaps,"Perturbation accounting");
    c.restartStagnation=0;
    auto off=TabuSearch::run(inst,c);
    check(off.recoveries==0 && off.perturbations==0,"Recovery disabled");
    c.tenure=7;
    auto blocked=TabuSearch::run(inst,c);
    check(blocked.iterations==1 && blocked.stoppedWithoutCandidate && blocked.alternatives>0,"All-tabu bounded fallback");
    c.tenureMode=TSConfig::TenureMode::Dynamic;
    c.tenureMin=0; c.tenureMax=3; c.stagnationWindow=3;
    c.operatorMode=TSConfig::OperatorMode::Adaptive;
    auto adaptive=TabuSearch::run(inst,c);
    check(adaptive.alternatives>0,"Adaptive empty-operator fallback");
}
void searches() {
    for(const auto& name:{"comp01","comp02","comp05","comp11","comp12"}) {
        Instance inst; check(inst.loadFromFile(std::string("Instances/")+name+".ctt"),"Load");
        for(int variant=0; variant<4; ++variant) {
            TSConfig c; c.iterations=500; c.logInterval=1;
            if(variant&1) c.tenureMode=TSConfig::TenureMode::Dynamic;
            if(variant&2) c.operatorMode=TSConfig::OperatorMode::Adaptive;
            auto a=TabuSearch::run(inst,c,[&](const DeltaEvaluator& state,std::size_t) {
                auto full=Evaluator::evaluateMatrix(inst,state.schedule());
                check(full.is_feasible() && full.total_soft_cost()==state.state().cost.total(),"Accepted state mismatch");
            });
            auto b=TabuSearch::run(inst,c);
            check(a.best==b.best && a.bestCost==b.bestCost && a.currentCost==b.currentCost &&
                  a.evaluated==b.evaluated && a.aspirations==b.aspirations && a.alternatives==b.alternatives,
                  "Adaptive reproducibility");
            check(a.bestCost<=a.initialCost && a.convergence.size()==a.iterations+1,"Convergence/best");
            check(a.adaptations==((variant&2)?(a.iterations+49)/50:0),"Adaptation frequency");
            check(a.moveGenerated+a.swapGenerated==a.evaluated && a.moves+a.swaps==a.iterations,"Metrics accounting");
            auto lastBest=a.initialCost;
            std::size_t lastImprovement=0;
            for(std::size_t i=1;i<a.convergence.size();++i) {
                const auto& p=a.convergence[i]; const auto& previous=a.convergence[i-1];
                if(p.bestCost<lastBest) { lastImprovement=i; lastBest=p.bestCost; }
                check(p.stagnation==i-lastImprovement,"Stagnation reset");
                if(variant&1) check(p.tenure==AdaptiveControl::tenure(5,20,200,p.stagnation),"Recorded tenure formula");
                else check(p.tenure==7,"Fixed tenure changed");
                if((variant&2) && i>1 && (i-1)%50!=0)
                    check(p.pMove==previous.pMove && p.qMove==previous.qMove && p.qSwap==previous.qSwap,"Unexpected probability update");
                check(p.pMove>=.1 && p.pMove<=.9,"Search probability bounds");
            }
            std::cout<<name<<" variant="<<variant<<" best="<<a.bestCost<<" OK\n";
        }
    }
}
void exports() {
    auto inst=flat();
    auto directory=std::filesystem::temp_directory_path()/"cbctt-adaptive-export-test";
    std::set<std::string> names;
    for(int variant=0;variant<4;++variant) {
        TSConfig c; c.iterations=3; c.logInterval=1; c.tenure=0;
        if(variant&1) { c.tenureMode=TSConfig::TenureMode::Dynamic; c.tenureMin=0; }
        if(variant&2) c.operatorMode=TSConfig::OperatorMode::Adaptive;
        auto result=TabuSearch::run(inst,c);
        auto path=TabuSearch::exportBest(inst,result,c,directory.string());
        check(names.insert(path).second,"Variant filename collision");
        std::ifstream csv(path.substr(0,path.size()-4)+".csv");
        std::string line; std::getline(csv,line);
        check(line=="iteration,elapsed_ms,current_cost,best_cost,tabu_tenure,p_move,p_swap,q_move,q_swap,stagnation","CSV header");
        std::size_t rows=0;
        while(std::getline(csv,line)) {
            std::istringstream input(line); std::string cell; int columns=0;
            while(std::getline(input,cell,',')) ++columns;
            check(columns==10,"CSV columns"); ++rows;
        }
        check(rows==result.convergence.size(),"CSV rows");
        for(const auto& suffix:{".sol",".txt",".csv"})
            std::filesystem::remove(path.substr(0,path.size()-4)+suffix);
    }
    std::filesystem::remove(directory);
}
}
int main() {
    try { units(); recovery(); searches(); exports(); std::cout<<"PASS: adaptive controls, recovery, four variants, reproducibility, exports\n"; return 0; }
    catch(const std::exception& e) { std::cerr<<"FAIL: "<<e.what()<<'\n'; return 1; }
}
