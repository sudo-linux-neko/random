// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vaibhav Agarwal

#pragma once
#include "indicators.hpp"
#include "data.hpp"
#include <vector>
#include <string>
#include <random>
#include <variant>
#include <functional>
#include <cmath>

// Each indicator config randomizes params within sane ranges

enum class IndType {
    SMA, EMA, MACD, ADX, PSAR, ICHIMOKU, SUPERTREND, ALLIGATOR, LINREG, VORTEX,
    RSI, STOCH, CCI, ROC, WILLIAMS_R, ULTIMATE_OSC, MFI, AWESOME_OSC, TSI, DPO,
    BB, ATR, KELTNER, DONCHIAN, STDDEV, HIST_VOL, CHAIKIN_VOL, ULCER, RAFF, VOL_RATIO,
    OBV, VWAP, CMF, VOL_OSC, AD_LINE, EOM, VOL_ROC, FORCE_IDX, KLINGER, VPCI,
    COUNT
};

inline std::string ind_name(IndType t){
    const char* names[]={"SMA","EMA","MACD","ADX","PSAR","ICHIMOKU","SUPERTREND","ALLIGATOR","LINREG","VORTEX",
    "RSI","STOCH","CCI","ROC","WILLIAMS_R","ULTIMATE_OSC","MFI","AWESOME_OSC","TSI","DPO",
    "BB","ATR","KELTNER","DONCHIAN","STDDEV","HIST_VOL","CHAIKIN_VOL","ULCER","RAFF","VOL_RATIO",
    "OBV","VWAP","CMF","VOL_OSC","AD_LINE","EOM","VOL_ROC","FORCE_IDX","KLINGER","VPCI"};
    return names[int(t)];
}

struct IndConfig {
    IndType type;
    // params union-like
    int p1=14, p2=14, p3=14;
    double d1=2.0, d2=0.02;
    // thresholds for signal generation
    double thresh_buy=0, thresh_sell=0;
    // weight for voting
    int weight=1;
    // additional signal mode (0=price vs indicator, 1=indicator vs threshold, 2=cross)
    int mode=0;

    std::string to_string() const;
};

struct Strategy {
    std::vector<IndConfig> inds; // 1..7
    int vote_threshold=1; // sum votes >= threshold -> long, <= -threshold -> short/exit
    bool allow_short=false;

    std::string to_string() const;
    std::string to_json() const;
};

// Generate random param for an indicator type
inline IndConfig random_config(IndType t, std::mt19937& rng){
    IndConfig c; c.type=t;
    auto randint=[&](int a,int b){ std::uniform_int_distribution<int> d(a,b); return d(rng); };
    auto randd=[&](double a,double b){ std::uniform_real_distribution<double> d(a,b); return d(rng); };
    switch(t){
        case IndType::SMA: c.p1=randint(5,200); c.mode=randint(0,1); break;
        case IndType::EMA: c.p1=randint(5,150); c.mode=randint(0,1); break;
        case IndType::MACD: c.p1=randint(5,15); c.p2=randint(20,35); c.p3=randint(5,12); if(c.p2<=c.p1) c.p2=c.p1+randint(5,15); break;
        case IndType::ADX: c.p1=randint(7,30); c.thresh_buy=randd(15,30); c.thresh_sell=randd(15,30); break;
        case IndType::PSAR: c.d1=randd(0.01,0.05); c.d2=randd(0.01,0.04); break;
        case IndType::ICHIMOKU: c.p1=randint(7,12); c.p2=randint(22,30); c.p3=randint(40,60); break;
        case IndType::SUPERTREND: c.p1=randint(7,21); c.d1=randd(1.0,4.0); break;
        case IndType::ALLIGATOR: c.p1=randint(10,15); c.p2=randint(6,10); c.p3=randint(3,7); break;
        case IndType::LINREG: c.p1=randint(10,100); c.thresh_buy=randd(0,0.5); break;
        case IndType::VORTEX: c.p1=randint(7,30); break;
        case IndType::RSI: c.p1=randint(7,30); c.thresh_buy=randd(20,35); c.thresh_sell=randd(65,80); break;
        case IndType::STOCH: c.p1=randint(10,20); c.p2=randint(2,5); c.thresh_buy=randd(15,30); c.thresh_sell=randd(70,85); break;
        case IndType::CCI: c.p1=randint(10,30); c.thresh_buy=randd(-150,-80); c.thresh_sell=randd(80,150); break;
        case IndType::ROC: c.p1=randint(5,30); c.thresh_buy=randd(-2,0); c.thresh_sell=randd(0,2); break;
        case IndType::WILLIAMS_R: c.p1=randint(7,30); c.thresh_buy=randd(-90,-70); c.thresh_sell=randd(-30,-10); break;
        case IndType::ULTIMATE_OSC: c.p1=7; c.p2=14; c.p3=28; c.thresh_buy=randd(25,35); c.thresh_sell=randd(65,75); break;
        case IndType::MFI: c.p1=randint(10,20); c.thresh_buy=randd(15,30); c.thresh_sell=randd(70,85); break;
        case IndType::AWESOME_OSC: break;
        case IndType::TSI: c.p1=randint(15,30); c.p2=randint(7,15); break;
        case IndType::DPO: c.p1=randint(10,40); break;
        case IndType::BB: c.p1=randint(15,30); c.d1=randd(1.5,3.0); c.mode=randint(0,1); break;
        case IndType::ATR: c.p1=randint(7,30); c.mode=1; break;
        case IndType::KELTNER: c.p1=randint(15,30); c.p2=randint(7,21); c.d1=randd(1.0,3.0); break;
        case IndType::DONCHIAN: c.p1=randint(15,50); break;
        case IndType::STDDEV: c.p1=randint(10,30); break;
        case IndType::HIST_VOL: c.p1=randint(10,30); break;
        case IndType::CHAIKIN_VOL: c.p1=randint(7,20); break;
        case IndType::ULCER: c.p1=randint(7,20); c.thresh_buy=randd(2,8); break;
        case IndType::RAFF: c.p1=randint(20,60); c.d1=randd(1.0,3.0); break;
        case IndType::VOL_RATIO: c.p1=randint(5,15); c.p2=randint(20,40); break;
        case IndType::OBV: c.mode=randint(0,1); break;
        case IndType::VWAP: c.p1=randint(10,30); break;
        case IndType::CMF: c.p1=randint(10,30); c.thresh_buy=randd(-0.1,0.1); break;
        case IndType::VOL_OSC: c.p1=randint(5,15); c.p2=randint(20,35); break;
        case IndType::AD_LINE: break;
        case IndType::EOM: c.p1=randint(10,20); break;
        case IndType::VOL_ROC: c.p1=randint(5,20); break;
        case IndType::FORCE_IDX: c.p1=randint(10,20); break;
        case IndType::KLINGER: break;
        case IndType::VPCI: c.p1=randint(5,15); c.p2=randint(20,30); break;
        default: break;
    }
    c.weight = randint(1,2);
    return c;
}

inline Strategy random_strategy(std::mt19937& rng, int min_inds=1, int max_inds=7){
    Strategy s;
    std::uniform_int_distribution<int> n_dist(min_inds,max_inds);
    int n=n_dist(rng);
    std::uniform_int_distribution<int> type_dist(0, int(IndType::COUNT)-1);
    for(int i=0;i<n;++i){
        IndType t = IndType(type_dist(rng));
        s.inds.push_back(random_config(t,rng));
    }
    s.vote_threshold = std::uniform_int_distribution<int>(1, std::max(1,n))(rng);
    s.allow_short = std::uniform_int_distribution<int>(0,1)(rng)==0 ? false : false; // keep false for now, long-only safer
    return s;
}

// Compute raw indicator and convert to signal vector (-1,0,1)
// Price series needed for price-vs-indicator modes
std::vector<int> strategy_signals(const Strategy& strat, const Dataset& ds);
