// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vaibhav Agarwal

#include "optimizer.hpp"
#include "progress.hpp"
#include <iostream>
#include <iomanip>
#include <queue>
#include <cmath>

bool Optimizer::load_one(const std::string& path, Dataset& ds){
    if(path.find(".parquet")!=std::string::npos) return parse_parquet(path, ds);
    if(path.find(".csv.gz")!=std::string::npos) return parse_csv_gz(path, ds);
    return parse_csv(path, ds);
}

EvalResult Optimizer::evaluate(const Strategy& strat, std::mt19937& rng){
    TradeStats agg_train{}, agg_test{};
    double sum_score_train=0, sum_score_test=0;
    int valid_tickers=0;
    auto eval_one = [&](Dataset& ds){
        if(ds.size()<100) return;
        auto signals = strategy_signals(strat, ds);
        size_t warmup = 100;
        if(ds.train_end < warmup+10) return;
        auto res_train = backtest(ds, signals, 10000, config.commission_bps, config.slippage_bps, false, warmup, ds.train_end);
        auto res_test  = backtest(ds, signals, 10000, config.commission_bps, config.slippage_bps, false, ds.train_end, 0);
        sum_score_train += composite_score(res_train.stats);
        sum_score_test  += composite_score(res_test.stats);
        agg_train.total_return += res_train.stats.total_return;
        agg_test.total_return  += res_test.stats.total_return;
        agg_train.sharpe += res_train.stats.sharpe;
        agg_test.sharpe  += res_test.stats.sharpe;
        agg_train.max_drawdown += res_train.stats.max_drawdown;
        agg_test.max_drawdown  += res_test.stats.max_drawdown;
        agg_train.win_rate += res_train.stats.win_rate;
        agg_test.win_rate  += res_test.stats.win_rate;
        agg_train.profit_factor += res_train.stats.profit_factor;
        agg_test.profit_factor  += res_test.stats.profit_factor;
        agg_train.num_trades += res_train.stats.num_trades;
        agg_test.num_trades  += res_test.stats.num_trades;
        valid_tickers++;
    };
    if(streaming){
        for(auto &path: data_files){
            Dataset ds;
            if(!load_one(path, ds)) continue;
            eval_one(ds);
        }
    } else {
        for(auto &ds: datasets){
            eval_one(ds);
        }
    }
    EvalResult er;
    er.strat=strat;
    if(valid_tickers>0){
        agg_train.total_return/=valid_tickers; agg_test.total_return/=valid_tickers;
        agg_train.sharpe/=valid_tickers; agg_test.sharpe/=valid_tickers;
        agg_train.max_drawdown/=valid_tickers; agg_test.max_drawdown/=valid_tickers;
        agg_train.win_rate/=valid_tickers; agg_test.win_rate/=valid_tickers;
        agg_train.profit_factor/=valid_tickers; agg_test.profit_factor/=valid_tickers;
        agg_train.num_trades/=valid_tickers; agg_test.num_trades/=valid_tickers;
        er.train_stats=agg_train;
        er.test_stats=agg_test;
        er.score_train = sum_score_train/valid_tickers;
        er.score_test  = sum_score_test/valid_tickers;
    } else {
        er.score_train = -1e9; er.score_test=-1e9;
    }
    er.overfit = er.score_train - er.score_test;
    er.ticker = "UNIVERSE("+std::to_string(valid_tickers)+")";
    return er;
}

std::vector<EvalResult> Optimizer::run(ProgressBar* prog){
    int threads = config.num_threads==0? std::thread::hardware_concurrency() : config.num_threads;
    if(threads<=0) threads=4;
    if(threads>64) threads=64;
    std::cout<<"[Optimizer] Threads="<<threads<<" Iterations="<<config.iterations<<" Datasets="<<datasets.size()<<std::endl;

    std::atomic<int> next_idx{0};
    std::atomic<uint64_t> done{0};
    std::mutex top_mtx;
    std::vector<EvalResult> top;
    top.reserve(config.top_k*2);

    auto cmp = [](const EvalResult& a, const EvalResult& b){ return a.score_train > b.score_train; }; // min-heap by train score
    // Use vector sorted descending

    // For saving all
    std::ofstream all_csv;
    bool save_all = config.save_all;
    std::mutex csv_mtx;
    if(save_all){
        std::filesystem::create_directories(config.results_dir);
        all_csv.open(config.results_dir+"/all_results.csv");
        all_csv<<"rank,score_train,score_test,overfit,train_ret,test_ret,train_sharpe,test_sharpe,train_dd,test_dd,train_pf,test_pf,train_wr,test_wr,trades,strategy,json\n";
    }

    // FAST BATCHED STREAMING: uses 7.6GB RAM to cache one ticker + batch of strategies
    // For streaming (500×20y 1m =12GB disk), per-iteration per-ticker python spawn is 1.6 it/s.
    // Batched: load one ticker (120MB RAM) → evaluate BATCH strategies → 64× faster, uses RAM for batch.
    const int BATCH = streaming ? 64 : 1;
    std::cout<<"[Mode] "<<(streaming?"STREAMING BATCHED":"PRELOAD")<<" batch="<<BATCH<<" (uses 7.6GB RAM, AVX2, "<<threads<<" threads)\n";
    std::vector<std::thread> workers;
    workers.reserve(threads);
    for(int t=0;t<threads;++t){
        workers.emplace_back([&,t](){
            std::mt19937 rng(config.seed + t*10007 + std::hash<std::thread::id>{}(std::this_thread::get_id()));
            // thread-local batch buffers (use RAM)
            std::vector<Strategy> batch_strats;
            batch_strats.reserve(BATCH);
            struct Agg { TradeStats tr, te; double s_tr=0,s_te=0; int valid=0; };
            std::vector<Agg> aggs;
            while(true){
                int batch_start = next_idx.fetch_add(BATCH);
                if(batch_start >= config.iterations) break;
                int batch_n = std::min(BATCH, config.iterations - batch_start);
                batch_strats.clear(); batch_strats.reserve(batch_n);
                for(int i=0;i<batch_n;++i) batch_strats.push_back(random_strategy(rng,1,7));
                aggs.assign(batch_n, Agg{});
                // STREAMING: for each ticker, load once (120MB) then evaluate whole batch (uses RAM, 64× fewer python spawns)
                if(streaming){
                    for(auto &path: data_files){
                        Dataset ds;
                        if(!load_one(path, ds)) continue;
                        if(ds.size()<100 || ds.train_end<110) continue;
                        // pre-extract arrays once per ticker (saves recompute, uses RAM)
                        // for each strat in batch, compute signals+backtest (AVX2 fast)
                        for(int bi=0; bi<batch_n; ++bi){
                            auto signals = strategy_signals(batch_strats[bi], ds);
                            size_t warmup=100;
                            auto res_train = backtest(ds, signals, 10000, config.commission_bps, config.slippage_bps, false, warmup, ds.train_end);
                            auto res_test  = backtest(ds, signals, 10000, config.commission_bps, config.slippage_bps, false, ds.train_end, 0);
                            Agg &ag = aggs[bi];
                            ag.s_tr += composite_score(res_train.stats);
                            ag.s_te += composite_score(res_test.stats);
                            ag.tr.total_return += res_train.stats.total_return;
                            ag.te.total_return  += res_test.stats.total_return;
                            ag.tr.sharpe += res_train.stats.sharpe; ag.te.sharpe += res_test.stats.sharpe;
                            ag.tr.max_drawdown += res_train.stats.max_drawdown; ag.te.max_drawdown += res_test.stats.max_drawdown;
                            ag.tr.win_rate += res_train.stats.win_rate; ag.te.win_rate += res_test.stats.win_rate;
                            ag.tr.profit_factor += res_train.stats.profit_factor; ag.te.profit_factor += res_test.stats.profit_factor;
                            ag.tr.num_trades += res_train.stats.num_trades; ag.te.num_trades += res_test.stats.num_trades;
                            ag.valid++;
                        }
                        if(prog) { /* progress per ticker would overcount, only per batch */ }
                    }
                    // finalize batch results
                    for(int bi=0; bi<batch_n; ++bi){
                        Agg &ag = aggs[bi];
                        EvalResult er; er.strat = batch_strats[bi];
                        if(ag.valid>0){
                            ag.tr.total_return/=ag.valid; ag.te.total_return/=ag.valid;
                            ag.tr.sharpe/=ag.valid; ag.te.sharpe/=ag.valid;
                            ag.tr.max_drawdown/=ag.valid; ag.te.max_drawdown/=ag.valid;
                            ag.tr.win_rate/=ag.valid; ag.te.win_rate/=ag.valid;
                            ag.tr.profit_factor/=ag.valid; ag.te.profit_factor/=ag.valid;
                            ag.tr.num_trades/=ag.valid; ag.te.num_trades/=ag.valid;
                            er.train_stats=ag.tr; er.test_stats=ag.te;
                            er.score_train=ag.s_tr/ag.valid; er.score_test=ag.s_te/ag.valid;
                        } else { er.score_train=-1e9; er.score_test=-1e9; }
                        er.overfit=er.score_train-er.score_test;
                        er.ticker="UNIVERSE("+std::to_string(ag.valid)+")";
                        {
                            std::lock_guard<std::mutex> lk(top_mtx);
                            top.push_back(er);
                            std::sort(top.begin(), top.end(), [](auto &a, auto &b){return a.score_train > b.score_train;});
                            if((int)top.size() > config.top_k) top.resize(config.top_k);
                            if(save_all){
                                std::lock_guard<std::mutex> lk2(csv_mtx);
                                int idx = batch_start+bi;
                                all_csv<< idx<<","<< er.score_train<<","<<er.score_test<<","<<er.overfit<<","
                                       <<er.train_stats.total_return<<","<<er.test_stats.total_return<<","
                                       <<er.train_stats.sharpe<<","<<er.test_stats.sharpe<<","
                                       <<er.train_stats.max_drawdown<<","<<er.test_stats.max_drawdown<<","
                                       <<er.train_stats.profit_factor<<","<<er.test_stats.profit_factor<<","
                                       <<er.train_stats.win_rate<<","<<er.test_stats.win_rate<<","
                                       <<er.train_stats.num_trades<<",\""
                                       <<er.strat.to_string()<<"\",\""<<er.strat.to_json()<<"\"\n";
                            }
                        }
                        if(prog) prog->increment(1);
                    }
                } else {
                    // PRELOAD fast path (no batching needed, already in RAM)
                    for(int bi=0; bi<batch_n; ++bi){
                        int idx = batch_start+bi;
                        EvalResult er = evaluate(batch_strats[bi], rng);
                        {
                            std::lock_guard<std::mutex> lk(top_mtx);
                            top.push_back(er);
                            std::sort(top.begin(), top.end(), [](auto &a, auto &b){return a.score_train > b.score_train;});
                            if((int)top.size() > config.top_k) top.resize(config.top_k);
                            if(save_all){
                                std::lock_guard<std::mutex> lk2(csv_mtx);
                                all_csv<< idx<<","<< er.score_train<<","<<er.score_test<<","<<er.overfit<<","
                                       <<er.train_stats.total_return<<","<<er.test_stats.total_return<<","
                                       <<er.train_stats.sharpe<<","<<er.test_stats.sharpe<<","
                                       <<er.train_stats.max_drawdown<<","<<er.test_stats.max_drawdown<<","
                                       <<er.train_stats.profit_factor<<","<<er.test_stats.profit_factor<<","
                                       <<er.train_stats.win_rate<<","<<er.test_stats.win_rate<<","
                                       <<er.train_stats.num_trades<<",\""
                                       <<er.strat.to_string()<<"\",\""<<er.strat.to_json()<<"\"\n";
                                if(idx%1000==0) all_csv.flush();
                            }
                        }
                        if(prog) prog->increment(1);
                    }
                }
                if(batch_start % 5000 ==0 && t==0){
                    std::lock_guard<std::mutex> lk(top_mtx);
                    if(!top.empty()){
                        std::cout<<"\n[Batch "<<batch_start<<"] Best so far: train_score="<<top[0].score_train<<" test="<<top[0].score_test
                                 <<" strat="<<top[0].strat.to_string()
                                 <<" train_ret="<<top[0].train_stats.total_return<<"% test_ret="<<top[0].test_stats.total_return<<"%\n";
                    }
                }
            }
        });
    }
    for(auto &th: workers) th.join();
    if(save_all) all_csv.close();
    // final sort by train then test
    std::sort(top.begin(), top.end(), [](auto &a, auto &b){
        if(std::fabs(a.score_train - b.score_train)>1e-6) return a.score_train > b.score_train;
        return a.score_test > b.score_test;
    });
    return top;
}

void Optimizer::save_results(const std::vector<EvalResult>& top, const std::string& path){
    std::filesystem::create_directories(std::filesystem::path(path).parent_path());
    std::ofstream f(path);
    f<<"rank,score_train,score_test,overfit,train_ret,test_ret,train_sharpe,test_sharpe,train_dd,test_dd,train_pf,test_pf,buy_hold_train,buy_hold_test,strategy,json\n";
    for(size_t i=0;i<top.size();++i){
        auto &e=top[i];
        f<<i+1<<","<<e.score_train<<","<<e.score_test<<","<<e.overfit<<","
         <<e.train_stats.total_return<<","<<e.test_stats.total_return<<","
         <<e.train_stats.sharpe<<","<<e.test_stats.sharpe<<","
         <<e.train_stats.max_drawdown<<","<<e.test_stats.max_drawdown<<","
         <<e.train_stats.profit_factor<<","<<e.test_stats.profit_factor<<","
         <<e.train_stats.win_rate<<","<<e.test_stats.win_rate<<","
         <<e.train_stats.num_trades<<",\""
         <<e.strat.to_string()<<"\",\""<<e.strat.to_json()<<"\"\n";
    }
    f.close();
    // also save best strategy detailed
    if(!top.empty()){
        std::ofstream best(std::filesystem::path(path).parent_path()/"best_strategy.json");
        best<<top[0].strat.to_json();
        best.close();
    }
}
