// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vaibhav Agarwal

#pragma once
#include <atomic>
#include <chrono>
#include <iostream>
#include <iomanip>
#include <mutex>
#include <thread>

struct ProgressBar {
    std::atomic<uint64_t> done{0};
    uint64_t total=0;
    std::chrono::steady_clock::time_point start;
    std::mutex mtx;
    int barWidth=50;
    std::atomic<bool> stop{false};
    std::thread th;
    std::string prefix;

    ProgressBar(uint64_t tot, std::string pfx="Progress"): total(tot), prefix(pfx){
        start=std::chrono::steady_clock::now();
    }
    void start_display(){
        th = std::thread([this](){
            while(!stop.load()){
                display();
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
            }
            display(); std::cout<<std::endl;
        });
    }
    void display(){
        uint64_t d = done.load();
        double frac = total? double(d)/double(total):0;
        if(frac>1) frac=1;
        int pos = int(barWidth*frac);
        auto now=std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double>(now-start).count();
        double rate = elapsed>0? double(d)/elapsed : 0;
        double eta = rate>0? double(total-d)/rate : 0;
        std::lock_guard<std::mutex> lk(mtx);
        std::cout<<"\r"<<prefix<<" [";
        for(int i=0;i<barWidth;++i) std::cout<<(i<pos? "█": (i==pos? "▏":" "));
        std::cout<<"] "<< std::fixed<<std::setprecision(1)<<frac*100<<"% "
                 <<d<<"/"<<total
                 <<" | "<< std::setprecision(0)<<rate<<" it/s"
                 <<" | ETA "<< int(eta/3600)<<"h "<< int(fmod(eta,3600)/60)<<"m "<< int(fmod(eta,60))<<"s"
                 <<" | "<< std::setprecision(1)<<elapsed<<"s"
                 <<"   " << std::flush;
    }
    void increment(uint64_t n=1){ done.fetch_add(n); }
    void finish(){ stop.store(true); if(th.joinable()) th.join(); }
    ~ProgressBar(){ if(!stop.load()) finish(); }
};
