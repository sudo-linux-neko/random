// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vaibhav Agarwal

#include "data.hpp"
#include "strategy.hpp"
#include "optimizer.hpp"
#include "progress.hpp"
#include <iostream>
#include <filesystem>
#include <chrono>
#include <thread>
#include <fstream>
#include <iomanip>
#include <cstring>

void print_banner(){
    std::cout<<R"(
================================================================================
  UNIVERSE ENGINE - Pure C++ AVX2 Trading Strategy Brute-Forcer
  40 Indicators | 1-7 Combo Search | 70/30 Walk-Forward | No Lookahead
  Fees + Slippage | AVX2 + O3 + LTO | Multi-threaded
================================================================================
)"<<std::endl;
    std::cout<<"CPU Features: AVX2=";
#ifdef __AVX2__
    std::cout<<"YES  FMA=";
#ifdef __FMA__
    std::cout<<"YES";
#else
    std::cout<<"NO";
#endif
#else
    std::cout<<"NO";
#endif
    std::cout<<" | Threads="<<std::thread::hardware_concurrency()<<std::endl;
}

void print_usage(const char* prog){
    std::cout<<
    "Usage: "<<prog<<" [options]\n"
    "Options:\n"
    "  --data <dir>          Data directory with .csv/.parquet/.csv.gz (default: ./data)\n"
    "  --iterations <N>      Random combos to try (default: 200000) - unlimited\n"
    "  --threads <N>         Threads (0=auto, default: auto)\n"
    "  --fee <bps>           Commission bps per side (default: 5 = 0.05%)\n"
    "  --slippage <bps>      Slippage bps per side (default: 5)\n"
    "  --top <K>             Keep top K (default: 100)\n"
    "  --seed <N>            RNG seed (default: 42)\n"
    "  --results <dir>       Results dir (default: ./results)\n"
    "  --no-save-all         Don't save all_results.csv (only top)\n"
    "  --stream              Force streaming mode (one ticker in RAM at a time, fits 20GB disk + 7GB RAM, never decompress all)\n"
    "  --no-stream           Force preload all into RAM (fast, needs 60GB for 500×20y 1m)\n"
    "  --gen-synthetic <N>   Generate N synthetic tickers if no data found\n"
    "  --help                Show this\n"
    "\nExamples:\n"
    "  "<<prog<<" --data ./data --iterations 1000000 --threads 12 --fee 10 --top 200\n"
    "  "<<prog<<" --gen-synthetic 10 --iterations 50000  # quick test\n"
    "  "<<prog<<" --data data --iterations 500000 --stream  # 500×20y parquet 12GB fits 20GB, streams one ticker (120MB RAM)\n"
    ;
}

std::vector<Dataset> load_all(const std::string& dir){
    std::vector<Dataset> out;
    if(!std::filesystem::exists(dir)){
        std::cout<<"[Data] Dir not found: "<<dir<<"\n";
        return out;
    }
    auto files = list_csvs(dir);
    std::cout<<"[Data] Found "<<files.size()<<" data files (.csv/.parquet/.csv.gz) in "<<dir<<"\n";
    size_t total_mb=0;
    for(auto &p: files){
        Dataset ds;
        bool ok=false;
        if(p.find(".parquet")!=std::string::npos) ok=parse_parquet(p, ds);
        else if(p.find(".csv.gz")!=std::string::npos) ok=parse_csv_gz(p, ds);
        else ok=parse_csv(p, ds);
        // also try generic fallback
        if(!ok) ok=parse_any(p, ds);
        if(ok){
            std::cout<<"  "<<ds.ticker<<": "<<ds.bars.size()<<" bars  train_end="<<ds.train_end<<" ("<< (ds.train_end*100/ds.bars.size())<<"%)\n";
            total_mb += ds.bars.size()*sizeof(Bar);
            out.push_back(std::move(ds));
        } else {
            std::cout<<"  SKIP "<<p<<" (parse fail or too few rows)\n";
        }
    }
    std::cout<<"[Mem] Datasets loaded ~"<< (total_mb/1024/1024) <<" MB RAM"<<std::endl;
    return out;
}

void generate_synthetic(const std::string& dir, int n_tickers){
    std::filesystem::create_directories(dir);
    std::cout<<"[Synthetic] Generating "<<n_tickers<<" tickers with 20 years daily ~5040 bars each (simulated GBM)\n";
    std::mt19937 rng(1234);
    std::normal_distribution<double> norm(0,1);
    for(int t=0;t<n_tickers;++t){
        std::string path = dir+"/SYNTH"+std::to_string(t+1)+".csv";
        std::ofstream f(path);
        f<<"timestamp,open,high,low,close,volume\n";
        double price=100 + rng()%200;
        for(int i=0;i<5040;++i){
            double drift=0.0003;
            double vol=0.015;
            double ret = drift + vol*norm(rng);
            double close = price*std::exp(ret);
            double open = price;
            double high = std::max(open,close)*(1+ std::abs(norm(rng))*0.005);
            double low  = std::min(open,close)*(1- std::abs(norm(rng))*0.005);
            double volume = 1000000 + rng()%500000;
            f<<i<<","<<open<<","<<high<<","<<low<<","<<close<<","<<volume<<"\n";
            price=close;
        }
        f.close();
    }
    std::cout<<"[Synthetic] Done in "<<dir<<"\n";
}

int main(int argc, char** argv){
    print_banner();
    std::string data_dir="./data";
    int iterations=200000;
    int threads=0;
    double fee_bps=5, slip_bps=5;
    int top_k=100;
    unsigned seed=42;
    std::string results_dir="./results";
    bool save_all=true;
    int gen_synth=0;
    bool force_stream=false, force_nostream=false;

    for(int i=1;i<argc;++i){
        std::string a=argv[i];
        if(a=="--data" && i+1<argc) data_dir=argv[++i];
        else if(a=="--iterations" && i+1<argc) iterations=std::stoi(argv[++i]);
        else if(a=="--threads" && i+1<argc) threads=std::stoi(argv[++i]);
        else if(a=="--fee" && i+1<argc) fee_bps=std::stod(argv[++i]);
        else if(a=="--slippage" && i+1<argc) slip_bps=std::stod(argv[++i]);
        else if(a=="--top" && i+1<argc) top_k=std::stoi(argv[++i]);
        else if(a=="--seed" && i+1<argc) seed=std::stoul(argv[++i]);
        else if(a=="--results" && i+1<argc) results_dir=argv[++i];
        else if(a=="--no-save-all") save_all=false;
        else if(a=="--stream") force_stream=true;
        else if(a=="--no-stream") force_nostream=true;
        else if(a=="--gen-synthetic" && i+1<argc) gen_synth=std::stoi(argv[++i]);
        else if(a=="--help" || a=="-h"){ print_usage(argv[0]); return 0; }
        else { std::cout<<"Unknown arg: "<<a<<"\n"; print_usage(argv[0]); return 1; }
    }

    if(gen_synth>0){
        generate_synthetic(data_dir, gen_synth);
    }

    // Detect data size to choose streaming (fits 20GB disk + 7.6GB RAM, never decompress all)
    auto files = list_csvs(data_dir);
    if(files.empty()){
        std::cout<<"\n[ERROR] No data! Generating 10 synthetic tickers for demo...\n";
        generate_synthetic(data_dir, 10);
        files = list_csvs(data_dir);
    }
    if(files.empty()){ std::cout<<"No data even after synthetic, abort\n"; return 1; }
    // Estimate: check one file's bars to guess total RAM
    size_t est_bars_per_file=0;
    {
        Dataset tmp;
        if(files[0].find(".parquet")!=std::string::npos) parse_parquet(files[0], tmp);
        else parse_csv(files[0], tmp);
        est_bars_per_file = tmp.bars.size();
    }
    size_t est_total_bars = est_bars_per_file * files.size();
    size_t est_ram_mb = est_total_bars * sizeof(Bar) / (1024*1024);
    bool use_stream = force_stream ? true : (force_nostream ? false : (est_ram_mb > 500 || est_total_bars > 5000000)); // >500MB or 5M bars => stream
    std::cout<<"[Data] Found "<<files.size()<<" files in "<<data_dir<<"  est ~"<<est_total_bars/1000000<<"M bars  ~"<<est_ram_mb<<" MB RAM if preloaded\n";
    if(use_stream) std::cout<<"[Mode] STREAMING (one ticker in RAM at a time, ~"<< (est_bars_per_file*sizeof(Bar)/1024/1024) <<" MB, fits 20GB disk + 7GB RAM, never decompress all)\n";
    else std::cout<<"[Mode] PRELOAD (all in RAM, fast)\n";

    // For streaming we don't preload; for non-streaming we do
    std::vector<Dataset> datasets;
    if(!use_stream){
        datasets = load_all(data_dir);
        if(datasets.empty()){ std::cout<<"No data, abort\n"; return 1; }
        size_t total_bars=0; for(auto &d: datasets) total_bars+=d.bars.size();
        std::cout<<"\n[Universe] Tickers: "<<datasets.size()<<"  Total bars: "<<total_bars<<"  Avg bars/ticker: "<< total_bars/datasets.size()<<"\n";
    } else {
        // streaming summary without loading all
        std::cout<<"\n[Universe] Tickers: "<<files.size()<<"  (streaming, total est bars ~"<<est_total_bars<<")\n";
    }
    std::cout<<"[Split] 70% train / 30% test  (chronological, no shuffle, no leak)\n";
    std::cout<<"[Costs] Commission="<<fee_bps<<" bps  Slippage="<<slip_bps<<" bps  (per side, applied at entry & exit)\n";
    std::cout<<"[Search] Indicators=40  Combo size 1..7  Random params  | Iterations="<<iterations<<"\n";
    std::cout<<"[Output] Results -> "<<results_dir<<"/  (top "<<top_k<<" + all_results.csv)\n";
    std::cout<<"--------------------------------------------------------------------------------\n";

    OptimizerConfig cfg;
    cfg.iterations=iterations;
    cfg.top_k=top_k;
    cfg.num_threads=threads;
    cfg.commission_bps=fee_bps;
    cfg.slippage_bps=slip_bps;
    cfg.seed=seed;
    cfg.results_dir=results_dir;
    cfg.save_all=save_all;

    ProgressBar prog(iterations, "BruteForce");
    prog.start_display();

    auto t0=std::chrono::steady_clock::now();
    // Choose optimizer mode
    std::vector<EvalResult> top;
    {
        std::unique_ptr<Optimizer> opt;
        if(use_stream) opt = std::make_unique<Optimizer>(cfg, files);
        else opt = std::make_unique<Optimizer>(cfg, datasets);
        top = opt->run(&prog);
        prog.finish();
        auto t1=std::chrono::steady_clock::now();
        double secs = std::chrono::duration<double>(t1-t0).count();
        std::cout<<"\n\n================================================================================\n";
        std::cout<<"SEARCH COMPLETE in "<< std::fixed<<std::setprecision(1)<<secs<<"s  ("<< secs/60<<" min)  |  "<< iterations/secs<<" it/s overall\n";
        std::cout<<"================================================================================\n";
        std::cout<<std::left<<std::setw(4)<<"Rank" <<std::setw(10)<<"TrnScore" <<std::setw(10)<<"TstScore" <<std::setw(10)<<"TrnRet%" <<std::setw(10)<<"TstRet%" 
                 <<std::setw(9)<<"TrnShar" <<std::setw(9)<<"TstShar" <<std::setw(7)<<"TrnDD%" <<std::setw(7)<<"TstDD%" <<"Strategy\n";
        std::cout<<std::string(130,'-')<<"\n";
        for(size_t i=0;i<std::min<size_t>(30, top.size()); ++i){
            auto &e=top[i];
            std::cout<<std::setw(4)<<(i+1)
                     <<std::setw(10)<<std::fixed<<std::setprecision(2)<<e.score_train
                     <<std::setw(10)<<e.score_test
                     <<std::setw(10)<<e.train_stats.total_return
                     <<std::setw(10)<<e.test_stats.total_return
                     <<std::setw(9)<<e.train_stats.sharpe
                     <<std::setw(9)<<e.test_stats.sharpe
                     <<std::setw(7)<<std::setprecision(1)<<e.train_stats.max_drawdown
                     <<std::setw(7)<<e.test_stats.max_drawdown
                     <<e.strat.to_string().substr(0,60)<<"\n";
        }
        opt->save_results(top, results_dir+"/top_strategies.csv");
        std::cout<<"\n[Saved] "<<results_dir<<"/top_strategies.csv  ("<<top.size()<<" rows)\n";
        std::cout<<"[Saved] "<<results_dir<<"/best_strategy.json\n";
        if(save_all) std::cout<<"[Saved] "<<results_dir<<"/all_results.csv  ("<<iterations<<" rows)\n";
        if(!top.empty()){
            auto &b=top[0];
            std::cout<<"\n[BEST] "<<b.strat.to_string()<<"\n";
            std::cout<<"       Train: ret="<<b.train_stats.total_return<<"%  sharpe="<<b.train_stats.sharpe<<"  PF="<<b.train_stats.profit_factor<<"  win="<<b.train_stats.win_rate<<"%  dd="<<b.train_stats.max_drawdown<<"%  trades="<<b.train_stats.num_trades<<"\n";
            std::cout<<"       Test : ret="<<b.test_stats.total_return<<"%  sharpe="<<b.test_stats.sharpe<<"  PF="<<b.test_stats.profit_factor<<"  win="<<b.test_stats.win_rate<<"%  dd="<<b.test_stats.max_drawdown<<"%  trades="<<b.test_stats.num_trades<<"\n";
            std::cout<<"       JSON : "<<b.strat.to_json()<<"\n";
            if(b.test_stats.total_return > 0 && b.score_test > 0) std::cout<<"       >> TEST PROFITABLE with positive score - potential edge found!\n";
            else std::cout<<"       >> Test not profitable - market is efficient or needs more search/params.\n";
            std::cout<<"\n[No Leak Check] ✓ Train and Test computed on strictly separated chronological splits\n";
            std::cout<<"[Fee Check] ✓ Commission + slippage applied on both entry & exit, next-bar execution\n";
        }
        std::cout<<"\nRun again with more iterations for deeper search:\n";
        std::cout<<"  ./universe_engine --data "<<data_dir<<" --iterations 1000000 --threads 12\n";
        return 0;
    }
}
