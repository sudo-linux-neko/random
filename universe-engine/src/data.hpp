// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vaibhav Agarwal

#pragma once
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <cmath>
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <cstdint>

struct Bar {
    int64_t ts=0; // unix ms or index
    double open=0, high=0, low=0, close=0, volume=0;
};

struct Dataset {
    std::string ticker;
    std::vector<Bar> bars;
    size_t train_end=0; // exclusive index for train
    size_t size() const { return bars.size(); }
};

inline std::vector<std::string> list_csvs(const std::string& dir){
    std::vector<std::string> out;
    for(auto &p: std::filesystem::directory_iterator(dir)){
        auto ext=p.path().extension().string();
        if(ext==".csv" || ext==".parquet" || ext==".pq" || p.path().string().find(".csv.gz")!=std::string::npos) out.push_back(p.path().string());
    }
    std::sort(out.begin(), out.end());
    return out;
}
// forward
inline bool parse_parquet(const std::string& path, Dataset& ds);
inline bool parse_csv_gz(const std::string& path, Dataset& ds);

// Expected CSV: timestamp,open,high,low,close,volume  OR  date,open,high,low,close,volume
// Robust parser, skips header, handles date string
inline bool parse_csv(const std::string& path, Dataset& ds){
    std::ifstream f(path);
    if(!f) return false;
    std::string line;
    std::getline(f,line); // header - check if numeric
    bool hasHeader = false;
    {
        std::stringstream ss(line);
        std::string c; ss>>c;
        // if first char is not digit/- then header
        if(line.find("open")!=std::string::npos || line.find("Open")!=std::string::npos || line.find("timestamp")!=std::string::npos || line.find("date")!=std::string::npos) hasHeader=true;
        else {
            // try parse - if fails, header
            try{ std::stod(c); }catch(...){ hasHeader=true; }
        }
        if(!hasHeader){
            // first line is data, process it
            // fall through
        } else {
            // header consumed
        }
    }
    auto process = [&](const std::string& l)->bool{
        if(l.empty()) return false;
        std::stringstream ss(l);
        std::string cell;
        std::vector<std::string> cols;
        while(std::getline(ss,cell,',')) cols.push_back(cell);
        if(cols.size()<6) return false;
        try{
            Bar b;
            // ts: try int64, if contains '-' parse as date - use index fallback
            // For simplicity, use row index as ts if date parse fails
            // cols[0] may be date string
            try{
                if(cols[0].find('-')!=std::string::npos || cols[0].find('/')!=std::string::npos){
                    b.ts = ds.bars.size(); // sequential
                } else {
                    b.ts = std::stoll(cols[0]);
                }
            }catch(...){ b.ts = ds.bars.size(); }
            b.open = std::stod(cols[1]);
            b.high = std::stod(cols[2]);
            b.low  = std::stod(cols[3]);
            b.close= std::stod(cols[4]);
            b.volume=std::stod(cols[5]);
            if(!std::isfinite(b.open) || !std::isfinite(b.close)) return false;
            ds.bars.push_back(b);
        }catch(...){ return false; }
        return true;
    };
    if(!hasHeader) process(line);
    while(std::getline(f,line)) process(line);
    // ensure sorted by ts (already)
    // remove zero volume? keep
    // fill ts sequential if needed
    ds.ticker = std::filesystem::path(path).stem().string();
    if(ds.bars.size()>0){
        ds.train_end = size_t(ds.bars.size()*0.7);
        if(ds.train_end<50) ds.train_end=ds.bars.size()*0.7;
    }
    return ds.bars.size()>50;
}

inline void extract_arrays(const Dataset& ds, std::vector<double>& o, std::vector<double>& h, std::vector<double>& l, std::vector<double>& c, std::vector<double>& v){
    size_t n=ds.bars.size();
    o.resize(n); h.resize(n); l.resize(n); c.resize(n); v.resize(n);
    for(size_t i=0;i<n;++i){ o[i]=ds.bars[i].open; h[i]=ds.bars[i].high; l[i]=ds.bars[i].low; c[i]=ds.bars[i].close; v[i]=ds.bars[i].volume; }
}

// Parquet reader via Python pyarrow (no C++ Arrow link needed, streams via popen, fits 20GB budget)
// Uses ~/ml_env/bin/python if available, else python3. Keeps data as parquet on disk (18GB) not CSV (73GB)
inline bool parse_parquet(const std::string& path, Dataset& ds){
    // Use python to emit CSV to stdout: timestamp,open,high,low,close,volume
    // Handles both partitioned (ticker column) and single-ticker parquet
    std::string py = std::filesystem::exists("/home/neko/ml_env/bin/python") ? "/home/neko/ml_env/bin/python" : "python3";
    // Escape path for shell
    std::string cmd = py + " -c \""
        "import pyarrow.parquet as pq, sys; "
        "pf=pq.ParquetFile('" + path + "'); "
        "cols=[c for c in ['timestamp','open','high','low','close','volume','ticker'] if c in pf.metadata.schema.names]; "
        "df=pf.read(columns=cols).to_pandas(); "
        "import pandas as pd; "
        "df=df.sort_values('timestamp'); "
        // if timestamp is datetime, convert to ms
        "df['ts']=(pd.to_datetime(df['timestamp']).astype('int64')//1000000).astype('int64') if 'timestamp' in df.columns else range(len(df)); "
        "out=df[['ts','open','high','low','close','volume']]; "
        "out.to_csv(sys.stdout, header=False, index=False)\" 2>/dev/null";
    FILE* pipe = popen(cmd.c_str(), "r");
    if(!pipe) return false;
    char buf[8192];
    std::string out;
    // Instead of reading all, parse line by line via fgets
    ds.ticker = std::filesystem::path(path).stem().string();
    // If file is partitioned per ticker, ticker column may be present; use first ticker name
    bool first=true;
    while(fgets(buf, sizeof(buf), pipe)){
        std::string line(buf);
        if(line.empty() || line=="\n") continue;
        // line is ts,open,high,low,close,volume
        std::stringstream ss(line);
        std::string cell; std::vector<std::string> cols;
        while(std::getline(ss,cell,',')) cols.push_back(cell);
        if(cols.size()<6) continue;
        try{
            Bar b;
            b.ts = std::stoll(cols[0]);
            b.open = std::stod(cols[1]);
            b.high = std::stod(cols[2]);
            b.low  = std::stod(cols[3]);
            b.close= std::stod(cols[4]);
            b.volume=std::stod(cols[5]);
            if(!std::isfinite(b.open) || !std::isfinite(b.close)) continue;
            ds.bars.push_back(b);
        }catch(...){ continue; }
    }
    int rc = pclose(pipe);
    if(ds.bars.size()>50){
        ds.train_end = size_t(ds.bars.size()*0.7);
        return true;
    }
    return false;
}

inline bool parse_csv_gz(const std::string& path, Dataset& ds){
    std::string cmd = "gzip -dc '" + path + "' 2>/dev/null";
    FILE* pipe = popen(cmd.c_str(), "r");
    if(!pipe) return false;
    char buf[8192];
    bool hasHeader=false; bool first=true;
    auto process = [&](const std::string& l)->bool{
        if(l.empty()) return false;
        std::stringstream ss(l);
        std::string cell; std::vector<std::string> cols;
        while(std::getline(ss,cell,',')) cols.push_back(cell);
        if(cols.size()<6) return false;
        try{
            Bar b;
            try{
                if(cols[0].find('-')!=std::string::npos || cols[0].find('/')!=std::string::npos) b.ts = ds.bars.size();
                else b.ts = std::stoll(cols[0]);
            }catch(...){ b.ts = ds.bars.size(); }
            b.open = std::stod(cols[1]); b.high = std::stod(cols[2]); b.low = std::stod(cols[3]); b.close= std::stod(cols[4]); b.volume=std::stod(cols[5]);
            if(!std::isfinite(b.open) || !std::isfinite(b.close)) return false;
            ds.bars.push_back(b);
        }catch(...){ return false; }
        return true;
    };
    bool headerChecked=false;
    while(fgets(buf, sizeof(buf), pipe)){
        std::string line(buf);
        // strip newline
        if(!line.empty() && line.back()=='\n') line.pop_back();
        if(!headerChecked){
            headerChecked=true;
            if(line.find("open")!=std::string::npos || line.find("timestamp")!=std::string::npos) continue; // skip header
        }
        process(line);
    }
    pclose(pipe);
    ds.ticker = std::filesystem::path(path).stem().string();
    // strip .csv.gz
    if(ds.ticker.rfind(".csv")!=std::string::npos) ds.ticker = ds.ticker.substr(0, ds.ticker.find(".csv"));
    if(ds.bars.size()>50){ ds.train_end = size_t(ds.bars.size()*0.7); return true; }
    return false;
}

// Unified dispatcher
inline bool parse_any(const std::string& path, Dataset& ds){
    if(path.find(".parquet")!=std::string::npos || path.find(".pq")!=std::string::npos) return parse_parquet(path, ds);
    if(path.find(".csv.gz")!=std::string::npos) return parse_csv_gz(path, ds);
    return parse_csv(path, ds);
}
