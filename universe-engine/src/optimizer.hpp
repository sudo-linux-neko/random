// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vaibhav Agarwal

#pragma once
#include "strategy.hpp"
#include "data.hpp"
#include "backtester.hpp"
#include <vector>
#include <random>
#include <mutex>
#include <algorithm>
#include <atomic>
#include <thread>
#include <fstream>
#include <filesystem>
#include <chrono>
#include <condition_variable>

struct EvalResult {
    Strategy strat;
    TradeStats train_stats;
    TradeStats test_stats;
    double score_train=0; // composite
    double score_test=0;
    double overfit =0; // score_train - score_test
    std::string ticker; // if single ticker, else "UNIVERSE"
};

inline double composite_score(const TradeStats& s){
    // Prefer sharpe, total return, profit factor, low drawdown
    // score = sharpe*10 + total_return*0.1 + profit_factor*2 - max_dd*0.5 + win_rate*0.05 + cagr*0.1
    // Penalize high drawdown heavily
    double sc = s.sharpe*5.0 + s.total_return*0.05 + s.profit_factor*1.0 - s.max_drawdown*0.4 + s.win_rate*0.02;
    if(s.num_trades < 5) sc -= 10; // need minimum trades
    if(s.num_trades > 1000) sc -= 2; // overtrading penalty
    if(s.exposure < 5 || s.exposure > 90) sc -= 2;
    return sc;
}

struct OptimizerConfig {
    int iterations = 200000; // per process run
    int top_k = 100;
    int num_threads = 0; // 0=auto
    double commission_bps = 5; // 0.05%
    double slippage_bps = 5;
    unsigned seed = 42;
    std::string results_dir = "results";
    bool save_all = true; // save all results csv
};

class Optimizer {
public:
    // legacy: preload all into RAM (fast, needs ~60GB for 500×20y)
    Optimizer(OptimizerConfig cfg, std::vector<Dataset> data): config(cfg), datasets(std::move(data)), streaming(false) {}
    // streaming: only file paths, loads one ticker at a time (needs ~120MB RAM, fits 7.6GB & 20GB disk)
    Optimizer(OptimizerConfig cfg, std::vector<std::string> files): config(cfg), data_files(std::move(files)), streaming(true) {}

    std::vector<EvalResult> run(class ProgressBar* prog=nullptr);

    void save_results(const std::vector<EvalResult>& top, const std::string& path);
private:
    OptimizerConfig config;
    std::vector<Dataset> datasets; // used if !streaming
    std::vector<std::string> data_files; // used if streaming
    bool streaming=false;
    std::mutex mtx;

    EvalResult evaluate(const Strategy& strat, std::mt19937& rng);
    // helper to load one dataset on demand
    bool load_one(const std::string& path, Dataset& ds);
};

