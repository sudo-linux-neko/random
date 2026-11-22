// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vaibhav Agarwal

#pragma once
#include <vector>
#include <string>
#include <cmath>
#include <cstdint>
#include <limits>

// All indicators return vector<double> same size as input, NaN = not enough data (no lookahead)
// No future leak: computation uses only [0..i] to produce value at i

namespace ind {

// Trend
std::vector<double> sma(const std::vector<double>& src, int period);
std::vector<double> ema(const std::vector<double>& src, int period);
struct MACDResult { std::vector<double> macd, signal, hist; };
MACDResult macd(const std::vector<double>& src, int fast=12, int slow=26, int signal=9);
struct ADXResult { std::vector<double> adx, plus_di, minus_di; };
ADXResult adx(const std::vector<double>& high, const std::vector<double>& low, const std::vector<double>& close, int period=14);
std::vector<double> psar(const std::vector<double>& high, const std::vector<double>& low, double af_start=0.02, double af_inc=0.02, double af_max=0.2);
struct Ichimoku { std::vector<double> tenkan, kijun, senkou_a, senkou_b, chikou; };
Ichimoku ichimoku(const std::vector<double>& high, const std::vector<double>& low, const std::vector<double>& close,
                  int tenkan=9, int kijun=26, int senkou_b_period=52);
std::vector<double> supertrend(const std::vector<double>& high, const std::vector<double>& low, const std::vector<double>& close, int atr_period=10, double mult=3.0);
struct Alligator { std::vector<double> jaw, teeth, lips; };
Alligator alligator(const std::vector<double>& src, int jaw_p=13, int jaw_s=8, int teeth_p=8, int teeth_s=5, int lips_p=5, int lips_s=3);
std::vector<double> linear_regression_slope(const std::vector<double>& src, int period);
struct Vortex { std::vector<double> vi_plus, vi_minus; };
Vortex vortex(const std::vector<double>& high, const std::vector<double>& low, const std::vector<double>& close, int period=14);

// Momentum
std::vector<double> rsi(const std::vector<double>& close, int period=14);
struct Stoch { std::vector<double> k, d; };
Stoch stochastic(const std::vector<double>& high, const std::vector<double>& low, const std::vector<double>& close, int k_period=14, int d_period=3);
std::vector<double> cci(const std::vector<double>& high, const std::vector<double>& low, const std::vector<double>& close, int period=20);
std::vector<double> roc(const std::vector<double>& src, int period=12);
std::vector<double> williams_r(const std::vector<double>& high, const std::vector<double>& low, const std::vector<double>& close, int period=14);
std::vector<double> ultimate_osc(const std::vector<double>& high, const std::vector<double>& low, const std::vector<double>& close, int p1=7, int p2=14, int p3=28);
std::vector<double> mfi(const std::vector<double>& high, const std::vector<double>& low, const std::vector<double>& close, const std::vector<double>& vol, int period=14);
std::vector<double> awesome_osc(const std::vector<double>& high, const std::vector<double>& low);
std::vector<double> tsi(const std::vector<double>& close, int r=25, int s=13);
std::vector<double> dpo(const std::vector<double>& close, int period=20);

// Volatility
struct BB { std::vector<double> upper, middle, lower; };
BB bollinger(const std::vector<double>& src, int period=20, double mult=2.0);
std::vector<double> atr(const std::vector<double>& high, const std::vector<double>& low, const std::vector<double>& close, int period=14);
struct Keltner { std::vector<double> upper, middle, lower; };
Keltner keltner(const std::vector<double>& high, const std::vector<double>& low, const std::vector<double>& close, int ema_p=20, int atr_p=10, double mult=2.0);
struct Donchian { std::vector<double> upper, lower, middle; };
Donchian donchian(const std::vector<double>& high, const std::vector<double>& low, int period=20);
std::vector<double> stddev(const std::vector<double>& src, int period);
std::vector<double> hist_vol(const std::vector<double>& close, int period=20);
std::vector<double> chaikin_vol(const std::vector<double>& high, const std::vector<double>& low, int ema_p=10);
std::vector<double> ulcer_index(const std::vector<double>& close, int period=14);
struct Raff { std::vector<double> upper, lower, middle; };
Raff raff_channel(const std::vector<double>& close, int period=20, double dev_mult=2.0);
std::vector<double> volatility_ratio(const std::vector<double>& high, const std::vector<double>& low, const std::vector<double>& close, int short_p=10, int long_p=30);

// Volume
std::vector<double> obv(const std::vector<double>& close, const std::vector<double>& vol);
std::vector<double> vwap(const std::vector<double>& high, const std::vector<double>& low, const std::vector<double>& close, const std::vector<double>& vol, int period=20);
std::vector<double> cmf(const std::vector<double>& high, const std::vector<double>& low, const std::vector<double>& close, const std::vector<double>& vol, int period=20);
std::vector<double> volume_osc(const std::vector<double>& vol, int fast=12, int slow=26);
std::vector<double> ad_line(const std::vector<double>& high, const std::vector<double>& low, const std::vector<double>& close, const std::vector<double>& vol);
std::vector<double> ease_of_movement(const std::vector<double>& high, const std::vector<double>& low, const std::vector<double>& vol, int period=14);
std::vector<double> volume_roc(const std::vector<double>& vol, int period=12);
std::vector<double> force_index(const std::vector<double>& close, const std::vector<double>& vol, int period=13);
struct Klinger { std::vector<double> kvo, signal; };
Klinger klinger(const std::vector<double>& high, const std::vector<double>& low, const std::vector<double>& close, const std::vector<double>& vol);
std::vector<double> vpci(const std::vector<double>& close, const std::vector<double>& vol, int short_p=5, int long_p=20);

// Generic signal helper - normalize to discrete -1/0/1 using lookback-only thresholds
// Each indicator has a signal function that uses its raw output

} // namespace ind
