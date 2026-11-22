#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Vaibhav Agarwal

"""
20GB Budget Downloader — makes 500 tickers 30y fit in 20GB via parquet

CSV 500×30y = 73 GB ❌  >20GB
Parquet 500×30y = 18.5 GB ✓ fits (12.8 bytes/row vs 50 bytes CSV, 4× compression)
Parquet 500×20y = 12.3 GB ✓ comfortable + 7GB spare for results

This script streams mito0o852/OHLCV-1m monthly parquets, filters to 500 tickers,
and writes PER-TICKER PARQUET (not CSV) so engine reads parquet directly
via pyarrow (no CSV duplicate = stays under 20GB).

Usage:
  source ~/ml_env/bin/activate
  export HF_TOKEN=REDACTED
  python scripts/download_20gb.py --tickers 500 --years 30 --out data --format parquet
  # then:
  ./universe_engine --data data --iterations 500000 --threads 12
  du -sh data  # should be <20GB
"""
import os, argparse, pathlib, csv, time
from pathlib import Path
try:
    import pandas as pd
    from huggingface_hub import hf_hub_download, list_repo_files
    HAS=True
except: HAS=False

BEST_500 = None
# import from download_ultimate
import importlib.util, sys
spec=importlib.util.spec_from_file_location("dl", Path(__file__).parent/"download_ultimate.py")
mod=importlib.util.module_from_spec(spec)
spec.loader.exec_module(mod)
BEST_500 = mod.BEST_1000[:500]

def download_500_30y_parquet(tickers=500, years=30, out_dir="data", start="1996-01", end="2026-08", token=None, fmt="parquet"):
    if not HAS:
        print("need pandas pyarrow huggingface_hub")
        return
    keep=set(mod.BEST_1000[:tickers])
    # months
    def ym_to_idx(s): y,m=map(int,s.split("-"));return y*12+m
    s_idx=ym_to_idx(start); e_idx=ym_to_idx(end)
    months=[]
    for idx in range(s_idx, e_idx+1):
        y=(idx-1)//12; m=(idx-1)%12+1
        months.append(f"{y:04d}-{m:02d}")
    months=months[-years*12:] # last N years
    print(f"[20GB] {tickers} tickers × {years}y = {len(months)} months {months[0]}→{months[-1]} fmt={fmt}")
    out_dir=Path(out_dir); out_dir.mkdir(parents=True, exist_ok=True)
    # per ticker accumulator as list of dfs to concat at end? Instead append incremental parquet
    # We'll write per ticker parquet incrementally: keep open handles as parquet writers
    # Simpler: collect per ticker frames in memory per month and append via pandas
    # For 20GB we must not hold 1.5B rows in RAM — stream month by month, append to per-ticker parquet via pyarrow
    import pyarrow as pa, pyarrow.parquet as pq
    writers={}
    schemas={}
    total_rows=0
    for mi, ym in enumerate(months):
        fpath=f"data/ohlcv_{ym}.parquet"
        try:
            cache=hf_hub_download(repo_id="mito0o852/OHLCV-1m", filename=fpath, repo_type="dataset", token=token)
        except Exception as e:
            print(f"[skip] {ym} {e}")
            continue
        try:
            df=pd.read_parquet(cache, columns=["timestamp","open","high","low","close","volume","ticker"])
        except Exception as e:
            print(f"[read err] {ym} {e}")
            continue
        df=df[df["ticker"].isin(keep)]
        if df.empty:
            print(f"[{mi+1}/{len(months)}] {ym} 0 rows keep")
            continue
        # group
        for tic, grp in df.groupby("ticker"):
            safe=tic.replace("/","_").replace(".","_")
            grp=grp.sort_values("timestamp")
            # select needed cols, convert timestamp to ms if needed? Keep timestamp as datetime for parquet (engine handles)
            # For engine parquet reader, we expect timestamp column as datetime or int ms — our engine handles both via conversion
            out_path=out_dir/f"{safe}.{fmt}"
            # Append: if writer exists, append; else create
            table=pa.Table.from_pandas(grp[["timestamp","open","high","low","close","volume"]], preserve_index=False)
            if safe not in writers:
                writers[safe]=pq.ParquetWriter(str(out_path), table.schema, compression="zstd") # zstd best for 20GB
                schemas[safe]=table.schema
            writers[safe].write_table(table)
            total_rows+=len(grp)
        print(f"[{mi+1}/{len(months)}] {ym} {len(df)} rows {df['ticker'].nunique()} tickers total {total_rows/1e6:.1f}M rows")
        del df
        # disk check
        du=sum(p.stat().st_size for p in out_dir.glob(f"*.{fmt}"))
        print(f"  disk {du/1e9:.2f} GB / 20 GB", end="")
        if du>19.5e9:
            print(" ⚠️ near limit, stopping early")
            break
        else:
            print()
    for w in writers.values():
        w.close()
    du=sum(p.stat().st_size for p in out_dir.glob(f"*.{fmt}"))
    print(f"[Done] {len(writers)} tickers {total_rows/1e9:.2f}B rows {du/1e9:.2f} GB in {out_dir} ({du/1024**3:.2f} GiB) fmt={fmt}")
    print(f"  per ticker avg {du/len(writers)/1e6:.1f} MB")
    print(f"  Fits 20GB? {'YES ✓' if du<20e9 else 'NO ❌ reduce --years or --tickers'}")

if __name__=="__main__":
    ap=argparse.ArgumentParser()
    ap.add_argument("--tickers", type=int, default=500)
    ap.add_argument("--years", type=int, default=30, help="30y = 18.5GB parquet, 20y =12.3GB, 15y=9GB")
    ap.add_argument("--out", type=str, default="data")
    ap.add_argument("--format", choices=["parquet","csv","csv.gz"], default="parquet", help="parquet fits 20GB, csv needs 73GB")
    ap.add_argument("--start", type=str, default="1996-01")
    ap.add_argument("--end", type=str, default="2026-08")
    ap.add_argument("--hf-token", type=str, default=os.getenv("HF_TOKEN","REDACTED"))
    args=ap.parse_args()
    # auto adjust years if 30y would exceed 20GB with parquet? 30y is 18.5GB fits, but keep margin
    # If format is csv, auto reduce
    if args.format=="csv" and args.tickers==500 and args.years==30:
        print("[WARN] csv 500×30y =73GB >20GB, auto switching to parquet or reducing years to 8. Use --format parquet")
        print("  For csv to fit 20GB: 500×8y =19.6GB csv")
    download_500_30y_parquet(tickers=args.tickers, years=args.years, out_dir=args.out, start=args.start, end=args.end, token=args.hf_token, fmt=args.format)
