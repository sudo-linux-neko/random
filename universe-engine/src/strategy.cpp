// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vaibhav Agarwal

#include "strategy.hpp"
#include "indicators.hpp"
#include "data.hpp"
#include <sstream>
#include <iomanip>
#include <limits>
#include <cmath>

static const double NaN = std::numeric_limits<double>::quiet_NaN();
inline bool is_nan(double x){return std::isnan(x);}

std::string IndConfig::to_string() const {
    std::ostringstream ss;
    ss<<ind_name(type)<<"("<<p1;
    if(type==IndType::MACD) ss<<","<<p2<<","<<p3;
    else if(type==IndType::SUPERTREND) ss<<","<<d1;
    else if(type==IndType::BB) ss<<","<<d1;
    else if(p2!=14 && (type==IndType::STOCH||type==IndType::VOL_OSC||type==IndType::VOL_RATIO)) ss<<","<<p2;
    ss<<") thr="<<thresh_buy<<"/"<<thresh_sell<<" w="<<weight;
    return ss.str();
}
std::string Strategy::to_string() const {
    std::ostringstream ss;
    ss<<"Strategy[";
    for(size_t i=0;i<inds.size();++i){ if(i) ss<<"+"; ss<<inds[i].to_string(); }
    ss<<"] vote_thr="<<vote_threshold;
    return ss.str();
}
std::string Strategy::to_json() const {
    std::ostringstream ss;
    ss<<"{\"inds\":[";
    for(size_t i=0;i<inds.size();++i){
        auto &c=inds[i];
        if(i) ss<<",";
        ss<<"{\"type\":\""<<ind_name(c.type)<<"\",\"p1\":"<<c.p1<<",\"p2\":"<<c.p2<<",\"p3\":"<<c.p3<<",\"d1\":"<<c.d1<<",\"thr_buy\":"<<c.thresh_buy<<",\"thr_sell\":"<<c.thresh_sell<<",\"weight\":"<<c.weight<<",\"mode\":"<<c.mode<<"}";
    }
    ss<<"],\"vote_thr\":"<<vote_threshold<<",\"allow_short\":"<<(allow_short?"true":"false")<<"}";
    return ss.str();
}

// Helper to produce discrete signal from a raw indicator series vs price/threshold
// We'll implement per-type logic. Returns -1,0,1 per bar (0=neutral/exit)

template<typename Vec> std::vector<int> null_signals(size_t n){ return std::vector<int>(n,0); }

std::vector<int> strategy_signals(const Strategy& strat, const Dataset& ds){
    size_t n=ds.bars.size();
    if(n==0) return {};
    std::vector<double> close(n), high(n), low(n), vol(n);
    for(size_t i=0;i<n;++i){ close[i]=ds.bars[i].close; high[i]=ds.bars[i].high; low[i]=ds.bars[i].low; vol[i]=ds.bars[i].volume; }
    // accumulate votes per bar
    std::vector<int> votes(n,0);
    std::vector<int> counts(n,0); // number of indicators valid

    for(auto &cfg: strat.inds){
        std::vector<int> sig(n,0);
        // compute indicator
        switch(cfg.type){
            case IndType::SMA: {
                auto v=ind::sma(close,cfg.p1);
                for(size_t i=0;i<n;++i) if(!is_nan(v[i])) sig[i]= (close[i] > v[i]? 1 : close[i] < v[i]? -1:0);
                break;
            }
            case IndType::EMA: {
                auto v=ind::ema(close,cfg.p1);
                for(size_t i=0;i<n;++i) if(!is_nan(v[i])) sig[i]= (close[i] > v[i]? 1 : -1);
                break;
            }
            case IndType::MACD: {
                auto m=ind::macd(close,cfg.p1,cfg.p2,cfg.p3);
                for(size_t i=0;i<n;++i) if(!is_nan(m.macd[i]) && !is_nan(m.signal[i])) sig[i]= (m.macd[i] > m.signal[i]? 1 : m.macd[i] < m.signal[i]? -1:0);
                break;
            }
            case IndType::ADX: {
                auto a=ind::adx(high,low,close,cfg.p1);
                for(size_t i=0;i<n;++i) if(!is_nan(a.adx[i]) && !is_nan(a.plus_di[i])) {
                    if(a.adx[i] > cfg.thresh_buy && a.plus_di[i] > a.minus_di[i]) sig[i]=1;
                    else if(a.adx[i] > cfg.thresh_sell && a.minus_di[i] > a.plus_di[i]) sig[i]=-1;
                    else sig[i]=0;
                }
                break;
            }
            case IndType::PSAR: {
                auto v=ind::psar(high,low);
                for(size_t i=0;i<n;++i) if(!is_nan(v[i])) sig[i]= (close[i] > v[i]? 1 : -1);
                break;
            }
            case IndType::ICHIMOKU: {
                auto ic=ind::ichimoku(high,low,close,cfg.p1,cfg.p2,cfg.p3);
                for(size_t i=0;i<n;++i) if(!is_nan(ic.tenkan[i]) && !is_nan(ic.kijun[i])) {
                    // price above cloud and tenkan > kijun => bullish
                    bool above_cloud=false, below_cloud=false;
                    if(!is_nan(ic.senkou_a[i]) && !is_nan(ic.senkou_b[i])){
                        double cloud_top= std::max(ic.senkou_a[i], ic.senkou_b[i]);
                        double cloud_bot= std::min(ic.senkou_a[i], ic.senkou_b[i]);
                        above_cloud = close[i] > cloud_top;
                        below_cloud = close[i] < cloud_bot;
                    }
                    if(ic.tenkan[i] > ic.kijun[i] && above_cloud) sig[i]=1;
                    else if(ic.tenkan[i] < ic.kijun[i] && below_cloud) sig[i]=-1;
                    else sig[i]=0;
                }
                break;
            }
            case IndType::SUPERTREND: {
                auto v=ind::supertrend(high,low,close,cfg.p1,cfg.d1);
                for(size_t i=0;i<n;++i) if(!is_nan(v[i])) sig[i]= (close[i] > v[i]? 1 : -1);
                break;
            }
            case IndType::ALLIGATOR: {
                auto a=ind::alligator(close,cfg.p1,8,cfg.p2,5,cfg.p3,3);
                for(size_t i=0;i<n;++i) if(!is_nan(a.jaw[i]) && !is_nan(a.teeth[i]) && !is_nan(a.lips[i])) {
                    if(a.lips[i] > a.teeth[i] && a.teeth[i] > a.jaw[i] && close[i] > a.lips[i]) sig[i]=1;
                    else if(a.lips[i] < a.teeth[i] && a.teeth[i] < a.jaw[i] && close[i] < a.lips[i]) sig[i]=-1;
                    else sig[i]=0;
                }
                break;
            }
            case IndType::LINREG: {
                auto v=ind::linear_regression_slope(close,cfg.p1);
                for(size_t i=0;i<n;++i) if(!is_nan(v[i])) sig[i]= (v[i] > cfg.thresh_buy? 1 : v[i] < -cfg.thresh_buy? -1:0);
                break;
            }
            case IndType::VORTEX: {
                auto v=ind::vortex(high,low,close,cfg.p1);
                for(size_t i=0;i<n;++i) if(!is_nan(v.vi_plus[i])) sig[i]= (v.vi_plus[i] > v.vi_minus[i]? 1 : -1);
                break;
            }
            case IndType::RSI: {
                auto v=ind::rsi(close,cfg.p1);
                for(size_t i=0;i<n;++i) if(!is_nan(v[i])) {
                    if(v[i] < cfg.thresh_buy) sig[i]=1; // oversold -> buy
                    else if(v[i] > cfg.thresh_sell) sig[i]=-1;
                    else sig[i]=0;
                }
                break;
            }
            case IndType::STOCH: {
                auto v=ind::stochastic(high,low,close,cfg.p1,cfg.p2);
                for(size_t i=0;i<n;++i) if(!is_nan(v.k[i])) {
                    if(v.k[i] < cfg.thresh_buy && v.k[i] > v.d[i]) sig[i]=1;
                    else if(v.k[i] > cfg.thresh_sell && v.k[i] < v.d[i]) sig[i]=-1;
                    else if(v.k[i] < cfg.thresh_buy) sig[i]=1;
                    else if(v.k[i] > cfg.thresh_sell) sig[i]=-1;
                    else sig[i]=0;
                }
                break;
            }
            case IndType::CCI: {
                auto v=ind::cci(high,low,close,cfg.p1);
                for(size_t i=0;i<n;++i) if(!is_nan(v[i])) sig[i]= (v[i] < cfg.thresh_buy?1: v[i] > cfg.thresh_sell? -1:0);
                break;
            }
            case IndType::ROC: {
                auto v=ind::roc(close,cfg.p1);
                for(size_t i=0;i<n;++i) if(!is_nan(v[i])) sig[i]= (v[i] > cfg.thresh_sell?1: v[i] < cfg.thresh_buy? -1:0);
                break;
            }
            case IndType::WILLIAMS_R: {
                auto v=ind::williams_r(high,low,close,cfg.p1);
                for(size_t i=0;i<n;++i) if(!is_nan(v[i])) sig[i]= (v[i] < cfg.thresh_buy?1: v[i] > cfg.thresh_sell? -1:0); // -80 oversold buy
                break;
            }
            case IndType::ULTIMATE_OSC: {
                auto v=ind::ultimate_osc(high,low,close);
                for(size_t i=0;i<n;++i) if(!is_nan(v[i])) sig[i]= (v[i] < cfg.thresh_buy?1: v[i] > cfg.thresh_sell? -1:0);
                break;
            }
            case IndType::MFI: {
                auto v=ind::mfi(high,low,close,vol,cfg.p1);
                for(size_t i=0;i<n;++i) if(!is_nan(v[i])) sig[i]= (v[i] < cfg.thresh_buy?1: v[i] > cfg.thresh_sell? -1:0);
                break;
            }
            case IndType::AWESOME_OSC: {
                auto v=ind::awesome_osc(high,low);
                for(size_t i=0;i<n;++i) if(!is_nan(v[i])) sig[i]= (v[i] >0?1: v[i]<0?-1:0);
                break;
            }
            case IndType::TSI: {
                auto v=ind::tsi(close,cfg.p1,cfg.p2);
                for(size_t i=0;i<n;++i) if(!is_nan(v[i])) sig[i]= (v[i] >0?1: v[i]<0?-1:0);
                // optional zero cross with signal line needed but use 0
                break;
            }
            case IndType::DPO: {
                auto v=ind::dpo(close,cfg.p1);
                for(size_t i=0;i<n;++i) if(!is_nan(v[i])) sig[i]= (v[i] >0?1: v[i]<0?-1:0);
                break;
            }
            case IndType::BB: {
                auto b=ind::bollinger(close,cfg.p1,cfg.d1);
                for(size_t i=0;i<n;++i) if(!is_nan(b.upper[i])) {
                    if(close[i] < b.lower[i]) sig[i]=1;
                    else if(close[i] > b.upper[i]) sig[i]=-1;
                    else sig[i]=0;
                }
                break;
            }
            case IndType::ATR: {
                auto v=ind::atr(high,low,close,cfg.p1);
                // ATR itself not directional, use ATR vs close volatility filter: 0 neutral
                // We'll use ATR rising => trend continuation? Instead keep 0.
                for(size_t i=1;i<n;++i) if(!is_nan(v[i]) && !is_nan(v[i-1])) {
                    // if ATR rising and price up -> bullish? Simplified: price momentum
                    double atr_change = v[i]-v[i-1];
                    if(atr_change>0 && close[i]>close[i-1]) sig[i]=1;
                    else if(atr_change>0 && close[i]<close[i-1]) sig[i]=-1;
                    else sig[i]=0;
                }
                break;
            }
            case IndType::KELTNER: {
                auto k=ind::keltner(high,low,close,cfg.p1,cfg.p2,cfg.d1);
                for(size_t i=0;i<n;++i) if(!is_nan(k.upper[i])) {
                    if(close[i] > k.upper[i]) sig[i]=1;
                    else if(close[i] < k.lower[i]) sig[i]=-1;
                    else sig[i]=0;
                }
                break;
            }
            case IndType::DONCHIAN: {
                auto d=ind::donchian(high,low,cfg.p1);
                for(size_t i=0;i<n;++i) if(!is_nan(d.upper[i])) {
                    if(close[i] >= d.upper[i]) sig[i]=1;
                    else if(close[i] <= d.lower[i]) sig[i]=-1;
                    else sig[i]=0;
                }
                break;
            }
            case IndType::STDDEV: {
                auto v=ind::stddev(close,cfg.p1);
                for(size_t i=1;i<n;++i) if(!is_nan(v[i]) && !is_nan(v[i-1])) sig[i]= (v[i] > v[i-1]? 0:0); // neutral, not tradable alone -> keep 0
                // keep 0
                break;
            }
            case IndType::HIST_VOL: {
                auto v=ind::hist_vol(close,cfg.p1);
                for(size_t i=0;i<n;++i) sig[i]=0;
                break;
            }
            case IndType::CHAIKIN_VOL: {
                auto v=ind::chaikin_vol(high,low,cfg.p1);
                for(size_t i=0;i<n;++i) if(!is_nan(v[i])) sig[i]= (v[i] >0?0:0);
                break;
            }
            case IndType::ULCER: {
                auto v=ind::ulcer_index(close,cfg.p1);
                for(size_t i=0;i<n;++i) if(!is_nan(v[i])) sig[i]= (v[i] < cfg.thresh_buy?1: v[i]>cfg.thresh_buy*2?-1:0);
                break;
            }
            case IndType::RAFF: {
                auto r=ind::raff_channel(close,cfg.p1,cfg.d1);
                for(size_t i=0;i<n;++i) if(!is_nan(r.upper[i])) {
                    if(close[i] < r.lower[i]) sig[i]=1;
                    else if(close[i] > r.upper[i]) sig[i]=-1;
                    else sig[i]=0;
                }
                break;
            }
            case IndType::VOL_RATIO: {
                auto v=ind::volatility_ratio(high,low,close,cfg.p1,cfg.p2);
                for(size_t i=0;i<n;++i) if(!is_nan(v[i])) sig[i]= (v[i] >1.2? 1 : v[i] <0.8? -1:0);
                break;
            }
            case IndType::OBV: {
                auto v=ind::obv(close,vol);
                // compare OBV SMA signal: use 20 SMA cross
                auto sma20=ind::sma(v,20);
                for(size_t i=0;i<n;++i) if(!is_nan(sma20[i])) sig[i]= (v[i] > sma20[i]?1: v[i]<sma20[i]?-1:0);
                break;
            }
            case IndType::VWAP: {
                auto v=ind::vwap(high,low,close,vol,cfg.p1);
                for(size_t i=0;i<n;++i) if(!is_nan(v[i])) sig[i]= (close[i] > v[i]?1: close[i]<v[i]?-1:0);
                break;
            }
            case IndType::CMF: {
                auto v=ind::cmf(high,low,close,vol,cfg.p1);
                for(size_t i=0;i<n;++i) if(!is_nan(v[i])) sig[i]= (v[i] > cfg.thresh_buy?1: v[i] < -0.1? -1:0);
                break;
            }
            case IndType::VOL_OSC: {
                auto v=ind::volume_osc(vol,cfg.p1,cfg.p2);
                for(size_t i=0;i<n;++i) if(!is_nan(v[i])) sig[i]= (v[i] >0?1: v[i]<0?-1:0);
                break;
            }
            case IndType::AD_LINE: {
                auto v=ind::ad_line(high,low,close,vol);
                auto sma20=ind::sma(v,20);
                for(size_t i=0;i<n;++i) if(!is_nan(sma20[i])) sig[i]= (v[i] > sma20[i]?1:-1);
                break;
            }
            case IndType::EOM: {
                auto v=ind::ease_of_movement(high,low,vol,cfg.p1);
                for(size_t i=0;i<n;++i) if(!is_nan(v[i])) sig[i]= (v[i] >0?1: v[i]<0?-1:0);
                break;
            }
            case IndType::VOL_ROC: {
                auto v=ind::volume_roc(vol,cfg.p1);
                for(size_t i=0;i<n;++i) if(!is_nan(v[i])) sig[i]= (v[i] >0?1:-1);
                break;
            }
            case IndType::FORCE_IDX: {
                auto v=ind::force_index(close,vol,cfg.p1);
                for(size_t i=0;i<n;++i) if(!is_nan(v[i])) sig[i]= (v[i] >0?1: v[i]<0?-1:0);
                break;
            }
            case IndType::KLINGER: {
                auto k=ind::klinger(high,low,close,vol);
                for(size_t i=0;i<n;++i) if(!is_nan(k.kvo[i]) && !is_nan(k.signal[i])) sig[i]= (k.kvo[i] > k.signal[i]?1: -1);
                break;
            }
            case IndType::VPCI: {
                auto v=ind::vpci(close,vol,cfg.p1,cfg.p2);
                for(size_t i=0;i<n;++i) if(!is_nan(v[i])) sig[i]= (v[i] >0?1: v[i]<0?-1:0);
                break;
            }
            default: break;
        }
        // accumulate weighted vote
        for(size_t i=0;i<n;++i){
            if(sig[i]!=0){
                votes[i] += sig[i]*cfg.weight;
                counts[i]++;
            }
        }
    }
    // final signal via threshold
    std::vector<int> final(n,0);
    for(size_t i=0;i<n;++i){
        if(counts[i]==0){ final[i]=0; continue;}
        if(votes[i] >= strat.vote_threshold) final[i]=1;
        else if(votes[i] <= -strat.vote_threshold) final[i]=-1;
        else final[i]=0;
    }
    return final;
}
