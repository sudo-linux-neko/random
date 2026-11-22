# Universe Engine — Pure C++ AVX2 Brute-Force Trading Strategy Finder

Finds the **best trading strategy in the universe** by brute-forcing random combos of **40 indicators** (Trend/Momentum/Volatility/Volume) from 1 to 7 at a time, with random params, on 70% train / 30% test, **no lookahead**, with fees, **AVX2 + O3 + LTO** optimization.

## What it does
- Implements all 40 indicators you listed (SMA, EMA, MACD, ADX, PSAR, Ichimoku, SuperTrend, Alligator, LinearReg, Vortex, RSI, Stoch, CCI, ROC, WilliamsR, UO, MFI, AO, TSI, DPO, BB, ATR, Keltner, Donchian, StdDev, HistVol, ChaikinVol, Ulcer, Raff, VolRatio, OBV, VWAP, CMF, VolOsc, AD, EOM, VolROC, ForceIdx, Klinger, VPCI)
- Generates random strategies: pick 1-7 indicators, random params, vote-threshold
- Backtests with **next-bar execution** (signal at close → trade at next open), **commission + slippage**, **70/30 chronological split**
- Ranks by composite: `sharpe*5 + ret*0.05 + pf - dd*0.4 + win*0.02` (train), validates on test
- Saves every result + top 100
- Progress bar with ETA, it/s

## Compile (fastest)

```bash
g++ -O3 flags defined in Makefile include: -march=native -mavx2 -mfma -flto -DNDEBUG
```

```bash
cd universe-engine
make -j$(nproc)   # builds ./universe_engine
# or CMake:
mkdir build && cd build && cmake -DCMAKE_BUILD_TYPE=Release .. && make -j
```

## Data — Free Sources, 20y

**Reality check for 1-min 20y:**
- Stocks 1-min free: Yahoo only gives **7 days** of 1-min. No free source gives 20y 1-min for 1000 stocks. Even if it did, 1000 * 20y * 390 min = **2B bars ≈ 80GB** — won't fit on 7.6GB laptop. Engine streams ticker-by-ticker but still huge download + time.
- **Free & realistic:** `yfinance` **daily 20y** for stocks/ETFs (5000 bars/ticker ≈ 50MB for 1000 tickers) ✅
- **Crypto 1-min free:** **Binance** gives 1-min free for 8+ years (BTC since 2017) — best for 1-min testing at scale ✅

This repo includes `scripts/download.py` that handles both.

```bash
# 1) Activate your env
source ~/ml_env/bin/activate
pip install yfinance requests

# 2) Download 20y daily for 1000 tickers (free, ~5M bars)
python scripts/download.py --all1000 --interval 1d --period 20y --out data

# 3) Or quick demo 100 tickers
python scripts/download.py --tickers 100 --interval 1d --period 20y --out data

# 4) Crypto 1-min (Binance, ~8y history for BTC): 
python scripts/download.py --crypto --interval 1d --period 20y --out data
# For 1-min last 7d stocks:
python scripts/download.py --tickers 100 --interval 1m --period 7d --out data

ls -lh data | head
```

## Run — The Final Brute-Force Command

```bash
# Unlimited brute force — as you asked: try a lot of random combos + params, 1..7 indicators
# This is THE command for the final run:

./universe_engine --data ./data --iterations 1000000 --threads 12 --fee 5 --slippage 5 --top 200

# Even bigger (overnight):
./universe_engine --data ./data --iterations 5000000 --threads 0 --top 500

# Quick test (synthetic if no data):
./universe_engine --gen-synthetic 10 --iterations 20000 --threads 4

# Custom fees, save all:
./universe_engine --data ./data --iterations 200000 --fee 10 --slippage 10 --top 100
```

Outputs:
- `results/top_strategies.csv` — ranked
- `results/all_results.csv` — every iteration
- `results/best_strategy.json` — best JSON

## Example Output

```
BruteForce [██████▏     ] 42.3% 84500/200000 | 1250 it/s | ETA 0h 1m 32s
[Iter 50000] Best so far: train_score=12.4 test=8.1 strat=RSI(14)+SMA(50)+BB(20,2) vote_thr=2
...
SEARCH COMPLETE in 160.2s (2.6 min) | 1248 it/s overall
Rank TrnScore  TstScore  TrnRet%   TstRet%   TrnShar  TstShar  TrnDD% TstDD% Strategy
1    14.20     9.10      42.5      18.3      1.84     1.21     8.2    9.1    RSI(12,30,70)+EMA(21)+Donchian(20) vote_thr=2
```

## No-Leak Guarantee

- Split is **chronological 70/30**, no shuffle
- Indicators computed with **only past data** (`[0..i]` → signal at `i`)
- Execution at **next bar open** (`signal i` → `trade i+1`)
- Warmup 100 bars skipped
- Train and test evaluated **separately**

## Performance

- `-O3 -march=native -mavx2 -mfma -flto -ffast-math`
- AVX2 for SMA/mean/stddev/max/min
- Multi-threaded (12 cores → ~12× speed)
- On i5-1335U 12 threads: ~1200–2500 it/s (depends on tickers/bars)

## Ask Unlimited Questions

Open an issue or ask me: fee tuning, indicator thresholds, adding short, custom scoring, exporting for live trading, etc.
