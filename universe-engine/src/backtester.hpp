// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vaibhav Agarwal

#pragma once
#include "data.hpp"
#include <vector>
#include <cmath>
#include <limits>
#include <algorithm>

struct TradeStats {
    double total_return=0; // (final equity / initial -1)*100
    double max_drawdown=0; // %
    double sharpe=0;
    double sortino=0;
    double profit_factor=0;
    double win_rate=0;
    double avg_trade=0;
    double num_trades=0;
    double cagr=0;
    double volatility=0;
    double exposure=0; // % time in market
    double final_equity=0;
    double buy_hold_return=0;
};

struct BacktestResult {
    TradeStats stats;
    std::vector<double> equity_curve;
    std::vector<int> positions; // 0 flat, 1 long, -1 short
};

// No lookahead: signal at bar i executes at bar i+1 open (or close)
// fees: commission_bps + slippage_bps applied on each trade notional
inline BacktestResult backtest(
    const Dataset& ds,
    const std::vector<int>& signal, // -1,0,1 sized n, signal[i] generated using data up to i (close[i])
    double initial_capital=10000.0,
    double commission_bps=5.0, // 0.05% per side
    double slippage_bps=5.0,
    bool allow_short=false,
    size_t start_idx=0, // where to start trading (to skip warmup)
    size_t end_idx=0 // 0 means till end
){
    size_t n=ds.bars.size();
    if(end_idx==0 || end_idx>n) end_idx=n;
    if(start_idx>=end_idx) start_idx=0;
    BacktestResult res;
    res.equity_curve.assign(n, std::numeric_limits<double>::quiet_NaN());
    res.positions.assign(n,0);

    // equity simulation
    double equity = initial_capital;
    double cash = initial_capital;
    double shares = 0;
    double entry_price = 0;
    int pos = 0; // current pos
    double peak = initial_capital;
    double max_dd = 0;
    std::vector<double> returns;
    returns.reserve(end_idx-start_idx);
    double gross_profit=0, gross_loss=0;
    int wins=0, losses=0;
    int trades=0;
    double exposure_bars=0;

    double fee_rate = (commission_bps + slippage_bps)/10000.0;

    // buy&hold benchmark
    double bh_start = ds.bars[start_idx].close;
    double bh_end = ds.bars[end_idx-1].close;
    double bh_ret = bh_start!=0 ? (bh_end/bh_start-1)*100 : 0;

    // next-bar execution loop
    // signal at i determines position for i+1...
    int target_pos = 0;
    for(size_t i=start_idx; i+1 < end_idx; ++i){
        // update target based on signal at i
        int sig = signal[i];
        if(sig>0) target_pos = 1;
        else if(sig<0) target_pos = allow_short? -1 : 0;
        else target_pos = 0; // flat if signal 0? alternative: hold previous? Here 0 means exit
        // Alternative: if sig==0 keep previous? But we want mean-reversion to exit on neutral.
        // We'll implement: if sig==0, keep? Let's do: 0 means close position (take profit) - more conservative. User can define signal as hold (1) vs exit (0)
        // To allow hold, signal should stay 1. So 0=exit is correct.

        // execute at next bar open
        double exec_price = ds.bars[i+1].open;
        if(pos != target_pos){
            // close existing
            if(pos !=0){
                double exit_val = shares * exec_price;
                if(pos==-1) {
                    // short: we borrowed shares, need to cover
                    // equity = cash + shares*price ??? Simplify: for short, shares negative
                    // Instead handle separately: for short, cash initially increased on sell, now buy back
                    // We'll use generic: equity = cash + shares*exec_price ??? But shares negative
                }
                // For simplicity, handle long only fully, short approximation
                if(pos==1){
                    double proceeds = shares * exec_price * (1.0 - fee_rate);
                    double cost_basis = shares * entry_price * (1.0 + fee_rate); // not needed for P&L directly, P&L is proceeds - cost? Actually cash already deducted on entry
                    // P&L: proceeds - (shares*entry_price*(1+fee))? But cash was deducted: cash = cash0 - shares*entry*(1+fee)
                    // Upon exit: cash += proceeds
                    cash += proceeds;
                    double pnl = proceeds - shares*entry_price*(1.0+fee_rate);
                    // Actually pnl includes fees both sides: shares*entry*fee + shares*exit*fee
                    if(pnl>0){ gross_profit+=pnl; wins++; } else { gross_loss+= -pnl; losses++; }
                    shares=0;
                } else if(pos==-1){
                    // covering short: shares negative, need to buy
                    double cover_cost = (-shares) * exec_price * (1.0 + fee_rate);
                    cash -= cover_cost;
                    double short_proceeds = (-shares)*entry_price*(1.0 - fee_rate); // received when entered short
                    double pnl = short_proceeds - cover_cost;
                    if(pnl>0){ gross_profit+=pnl; wins++; } else { gross_loss+= -pnl; losses++; }
                    shares=0;
                }
                trades++;
            }
            // open new
            if(target_pos !=0){
                if(target_pos==1){
                    // go long with all cash
                    double price_with_fee = exec_price * (1.0 + fee_rate);
                    shares = cash / price_with_fee;
                    cash = 0;
                    entry_price = exec_price;
                } else if(target_pos==-1){
                    // short with all equity
                    double price_net = exec_price * (1.0 - fee_rate);
                    shares = - (equity / price_net); // negative shares
                    cash += (-shares)*price_net; // receive proceeds
                    entry_price = exec_price;
                    // equity still = cash + shares*price ; with shares negative and cash increased, equity ~ initial
                }
            }
            pos = target_pos;
        }
        // mark equity at close of i+1
        double cur_price = ds.bars[i+1].close;
        if(pos==1) equity = shares * cur_price;
        else if(pos==-1) equity = cash + shares * cur_price; // shares negative
        else equity = cash;
        // if flat, equity = cash
        if(pos==0) equity = cash;
        res.equity_curve[i+1]=equity;
        res.positions[i+1]=pos;
        if(pos!=0) exposure_bars++;
        if(equity>peak) peak=equity;
        double dd = peak>0? (peak-equity)/peak*100 :0;
        if(dd>max_dd) max_dd=dd;
        // returns for sharpe
        if(i+1 > start_idx+1){
            double prev_eq = res.equity_curve[i];
            if(std::isnan(prev_eq)) prev_eq=initial_capital;
            double ret = prev_eq!=0? (equity/prev_eq -1) :0;
            returns.push_back(ret);
        }
    }
    // finalize equity
    if(pos!=0){
        // mark to market at last close, already done
    } else {
        // ensure final equity is cash
    }
    // if equity_curve still NaN at end, fill
    for(size_t i=start_idx;i<end_idx;++i) if(std::isnan(res.equity_curve[i])) res.equity_curve[i]=initial_capital;

    TradeStats st;
    st.final_equity = equity;
    st.total_return = (equity/initial_capital -1)*100;
    st.max_drawdown = max_dd;
    st.num_trades = trades;
    st.buy_hold_return = bh_ret;
    st.exposure = (end_idx-start_idx)>0 ? exposure_bars/double(end_idx-start_idx)*100 :0;
    if(trades>0){
        st.win_rate = double(wins)/trades*100;
        st.profit_factor = gross_loss>0? gross_profit/gross_loss : (gross_profit>0? 999:0);
        st.avg_trade = trades>0? (gross_profit - gross_loss)/trades :0;
    }
    // Sharpe / Sortino
    if(returns.size()>2){
        double mean=0; for(double r: returns) mean+=r; mean/=returns.size();
        double var=0; for(double r: returns) var+=(r-mean)*(r-mean);
        var/=returns.size();
        double std = std::sqrt(var);
        double downside_var=0; int down_n=0;
        for(double r: returns) if(r<0){ downside_var+=r*r; down_n++; }
        downside_var = down_n>0? downside_var/down_n : 0;
        double downside = std::sqrt(downside_var);
        double rf=0; // assume 0
        // annualized sharpe assuming daily bars ~252, if 1min need different -> estimate 252*390 minutes per year? Simplify 252.
        double ann_factor = std::sqrt(252); // if minute data, this underestimates but comparable across strategies
        st.volatility = std * ann_factor *100;
        st.sharpe = std>0? (mean - rf)/std * ann_factor : 0;
        st.sortino = downside>0? (mean - rf)/downside * ann_factor : 0;
        // CAGR
        double years = double(end_idx-start_idx)/252.0; // assume daily; if minute, still same bars count ~390*252 per year -> this overestimates years. Better detect timeframe via bar count?
        // Heuristic: if n > 100000, assume 1min -> years = bars / (252*390)
        if((end_idx-start_idx) > 50000) years = double(end_idx-start_idx)/(252*390);
        if(years>0) st.cagr = (std::pow(equity/initial_capital, 1.0/years)-1)*100;
    }
    res.stats = st;
    return res;
}

// Helper to compute warmup period (max period among indicators) - caller should ensure start_idx >= warmup
