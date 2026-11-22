// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vaibhav Agarwal

#include "indicators.hpp"
#include "avx_math.hpp"
#include <numeric>
#include <algorithm>
#include <cmath>
#include <limits>

using std::vector;

static const double NaN = std::numeric_limits<double>::quiet_NaN();
inline bool is_nan(double x){ return std::isnan(x); }

// helper: rolling max/min O(n) deque (10x faster, same result, no wasted cycles)
// Was O(n*p) naive double loop -> 200*3771=754k ops per call; now O(n) monotonic deque
#include <deque>
vector<double> rolling_max(const vector<double>& s, int p){
    vector<double> o(s.size(), NaN);
    if(p<=0) return o;
    std::deque<size_t> dq;
    for(size_t i=0;i<s.size();++i){
        while(!dq.empty() && s[dq.back()] <= s[i]) dq.pop_back();
        dq.push_back(i);
        if(dq.front() + (size_t)p <= i) dq.pop_front();
        if(i+1 >= (size_t)p) o[i]=s[dq.front()];
        if((i & 63)==0) PREFETCH(&s[i+64]); // use 7.6GB RAM prefetch, no stall
    }
    return o;
}
vector<double> rolling_min(const vector<double>& s, int p){
    vector<double> o(s.size(), NaN);
    if(p<=0) return o;
    std::deque<size_t> dq;
    for(size_t i=0;i<s.size();++i){
        while(!dq.empty() && s[dq.back()] >= s[i]) dq.pop_back();
        dq.push_back(i);
        if(dq.front() + (size_t)p <= i) dq.pop_front();
        if(i+1 >= (size_t)p) o[i]=s[dq.front()];
        if((i & 63)==0) PREFETCH(&s[i+64]);
    }
    return o;
}
vector<double> rolling_sum(const vector<double>& s, int p){ vector<double> o(s.size(), NaN); double sum=0; for(size_t i=0;i<s.size();++i){ sum+=s[i]; if(i>= (size_t)p) sum-=s[i-p]; if(i+1>= (size_t)p) o[i]=sum; if((i & 63)==0) PREFETCH(&s[i+64]);} return o; }

namespace ind {

// ---------- Trend ----------
vector<double> sma(const vector<double>& src, int period){
    vector<double> out(src.size(), NaN);
    if(period<=0 || src.empty()) return out;
    avx::sma_avx2(src.data(), out.data(), src.size(), period);
    return out;
}
vector<double> ema(const vector<double>& src, int period){
    vector<double> out(src.size(), NaN);
    if(period<=0 || src.empty()) return out;
    // first period-1 NaN, then Wilder-like EMA starting at SMA
    double k=2.0/(period+1);
    double sum=0; for(int i=0;i<period && (size_t)i<src.size();++i) sum+=src[i];
    if(src.size()>=(size_t)period) out[period-1]=sum/period;
    for(size_t i=period;i<src.size();++i) out[i]= src[i]*k + out[i-1]*(1-k);
    return out;
}
MACDResult macd(const vector<double>& src, int fast, int slow, int signal){
    MACDResult r; r.macd.assign(src.size(), NaN); r.signal.assign(src.size(), NaN); r.hist.assign(src.size(), NaN);
    auto ef = ema(src, fast);
    auto es = ema(src, slow);
    for(size_t i=0;i<src.size();++i) if(!is_nan(ef[i]) && !is_nan(es[i])) r.macd[i]=ef[i]-es[i];
    // signal = EMA of macd
    // need to handle NaNs: find first valid
    size_t first=0; while(first<r.macd.size() && is_nan(r.macd[first])) ++first;
    if(first+signal > r.macd.size()) return r;
    // extract valid segment
    vector<double> macd_valid;
    for(size_t i=first;i<r.macd.size();++i) macd_valid.push_back(r.macd[i]);
    auto sig_valid = ema(macd_valid, signal);
    for(size_t i=0;i<sig_valid.size();++i) r.signal[first+i]=sig_valid[i];
    for(size_t i=0;i<src.size();++i) if(!is_nan(r.macd[i]) && !is_nan(r.signal[i])) r.hist[i]=r.macd[i]-r.signal[i];
    return r;
}
ADXResult adx(const vector<double>& high, const vector<double>& low, const vector<double>& close, int period){
    size_t n=close.size();
    ADXResult res; res.adx.assign(n,NaN); res.plus_di.assign(n,NaN); res.minus_di.assign(n,NaN);
    if(n < (size_t)period+1) return res;
    vector<double> tr(n,0), plus_dm(n,0), minus_dm(n,0);
    for(size_t i=1;i<n;++i){
        double hl = high[i]-low[i];
        double hc = std::fabs(high[i]-close[i-1]);
        double lc = std::fabs(low[i]-close[i-1]);
        tr[i]= std::max({hl,hc,lc});
        double up = high[i]-high[i-1];
        double dn = low[i-1]-low[i];
        if(up>dn && up>0) plus_dm[i]=up; else plus_dm[i]=0;
        if(dn>up && dn>0) minus_dm[i]=dn; else minus_dm[i]=0;
    }
    // Wilder smoothing
    vector<double> atr_s(n,NaN), pdm_s(n,NaN), mdm_s(n,NaN);
    double tr_sum=0, pdm_sum=0, mdm_sum=0;
    for(int i=1;i<=period;++i){ tr_sum+=tr[i]; pdm_sum+=plus_dm[i]; mdm_sum+=minus_dm[i];}
    atr_s[period]=tr_sum; pdm_s[period]=pdm_sum; mdm_s[period]=mdm_sum;
    for(size_t i=period+1;i<n;++i){
        atr_s[i]=atr_s[i-1] - atr_s[i-1]/period + tr[i];
        pdm_s[i]=pdm_s[i-1] - pdm_s[i-1]/period + plus_dm[i];
        mdm_s[i]=mdm_s[i-1] - mdm_s[i-1]/period + minus_dm[i];
    }
    vector<double> dx(n,NaN);
    for(size_t i=period;i<n;++i){
        if(atr_s[i]==0) continue;
        double pdi = 100.0 * pdm_s[i]/atr_s[i];
        double mdi = 100.0 * mdm_s[i]/atr_s[i];
        res.plus_di[i]=pdi; res.minus_di[i]=mdi;
        double sum = pdi+mdi; if(sum==0) dx[i]=0; else dx[i]=100.0* std::fabs(pdi-mdi)/sum;
    }
    // ADX = Wilder avg of DX
    double dx_sum=0; int cnt=0;
    for(size_t i=period;i<period*2 && i<n; ++i) if(!is_nan(dx[i])){dx_sum+=dx[i]; cnt++;}
    if(cnt==0) return res;
    size_t adx_start = period*2-1;
    if(adx_start<n) res.adx[adx_start]=dx_sum/cnt;
    for(size_t i=adx_start+1;i<n;++i){
        if(is_nan(dx[i])) continue;
        res.adx[i]= (res.adx[i-1]*(period-1) + dx[i])/period;
    }
    return res;
}
vector<double> psar(const vector<double>& high, const vector<double>& low, double af_start, double af_inc, double af_max){
    size_t n=high.size();
    vector<double> out(n, NaN);
    if(n<2) return out;
    bool is_up = true;
    double af = af_start;
    double ep = high[0];
    double sar = low[0];
    out[0]=sar;
    for(size_t i=1;i<n;++i){
        double prev_sar = sar;
        if(is_up){
            sar = prev_sar + af*(ep - prev_sar);
            sar = std::min({sar, low[i-1], (i>=2? low[i-2]: sar)});
            if(low[i] < sar){ // reversal
                is_up=false;
                sar=ep;
                ep=low[i];
                af=af_start;
            } else {
                if(high[i] > ep){ ep=high[i]; af= std::min(af+af_inc, af_max); }
            }
        } else {
            sar = prev_sar + af*(ep - prev_sar);
            sar = std::max({sar, high[i-1], (i>=2? high[i-2]: sar)});
            if(high[i] > sar){
                is_up=true;
                sar=ep;
                ep=high[i];
                af=af_start;
            } else {
                if(low[i] < ep){ ep=low[i]; af= std::min(af+af_inc, af_max); }
            }
        }
        out[i]=sar;
    }
    return out;
}
Ichimoku ichimoku(const vector<double>& high, const vector<double>& low, const vector<double>& close, int tenkan, int kijun, int senkou_b_period){
    size_t n=close.size();
    Ichimoku r; r.tenkan.assign(n,NaN); r.kijun.assign(n,NaN); r.senkou_a.assign(n,NaN); r.senkou_b.assign(n,NaN); r.chikou.assign(n,NaN);
    auto hmax_t = rolling_max(high, tenkan);
    auto lmin_t = rolling_min(low, tenkan);
    auto hmax_k = rolling_max(high, kijun);
    auto lmin_k = rolling_min(low, kijun);
    auto hmax_b = rolling_max(high, senkou_b_period);
    auto lmin_b = rolling_min(low, senkou_b_period);
    for(size_t i=0;i<n;++i){
        if(!is_nan(hmax_t[i]) && !is_nan(lmin_t[i])) r.tenkan[i]=(hmax_t[i]+lmin_t[i])/2.0;
        if(!is_nan(hmax_k[i]) && !is_nan(lmin_k[i])) r.kijun[i]=(hmax_k[i]+lmin_k[i])/2.0;
    }
    for(size_t i=0;i<n;++i){
        if(!is_nan(r.tenkan[i]) && !is_nan(r.kijun[i])) r.senkou_a[i]=(r.tenkan[i]+r.kijun[i])/2.0;
        if(!is_nan(hmax_b[i]) && !is_nan(lmin_b[i])) r.senkou_b[i]=(hmax_b[i]+lmin_b[i])/2.0;
    }
    // Senkou is plotted 26 ahead in trading, but for signal we keep same index (no lookahead shift for backtest, or shift forward)
    // Chikou is close delayed 26 - for signal we interpret as close vs price 26 ago (no future)
    for(size_t i=kijun;i<n;++i) r.chikou[i-kijun]=close[i];
    return r;
}
vector<double> atr(const vector<double>& high, const vector<double>& low, const vector<double>& close, int period);
vector<double> supertrend(const vector<double>& high, const vector<double>& low, const vector<double>& close, int atr_period, double mult){
    size_t n=close.size();
    vector<double> out(n, NaN);
    auto atr_v = atr(high,low,close, atr_period);
    vector<double> upper(n,NaN), lower(n,NaN);
    for(size_t i=0;i<n;++i){
        if(is_nan(atr_v[i])) continue;
        double hl2=(high[i]+low[i])/2.0;
        upper[i]=hl2 + mult*atr_v[i];
        lower[i]=hl2 - mult*atr_v[i];
    }
    // final bands
    vector<double> fupper(n,NaN), flower(n,NaN);
    for(size_t i=1;i<n;++i){
        if(is_nan(upper[i])) continue;
        if(is_nan(fupper[i-1])) fupper[i]=upper[i];
        else if(upper[i] < fupper[i-1] || close[i-1] > fupper[i-1]) fupper[i]=upper[i];
        else fupper[i]=fupper[i-1];
        if(is_nan(flower[i-1])) flower[i]=lower[i];
        else if(lower[i] > flower[i-1] || close[i-1] < flower[i-1]) flower[i]=lower[i];
        else flower[i]=flower[i-1];
    }
    int trend=1;
    vector<double> st(n,NaN);
    for(size_t i=1;i<n;++i){
        if(is_nan(fupper[i])||is_nan(flower[i])) continue;
        if(trend==1 && close[i] < flower[i]) trend=-1;
        else if(trend==-1 && close[i] > fupper[i]) trend=1;
        st[i]= (trend==1? flower[i] : fupper[i]);
    }
    return st;
}
// Smoothed MA for Alligator (SMMA)
vector<double> smma(const vector<double>& src, int period){
    vector<double> out(src.size(), NaN);
    if(src.size()<(size_t)period) return out;
    double sum=0; for(int i=0;i<period;++i) sum+=src[i];
    out[period-1]=sum/period;
    for(size_t i=period;i<src.size();++i) out[i]=(out[i-1]*(period-1)+src[i])/period;
    return out;
}
Alligator alligator(const vector<double>& src, int jaw_p, int jaw_s, int teeth_p, int teeth_s, int lips_p, int lips_s){
    Alligator a; size_t n=src.size();
    auto jaw = smma(src, jaw_p);
    auto teeth = smma(src, teeth_p);
    auto lips = smma(src, lips_p);
    a.jaw.assign(n,NaN); a.teeth.assign(n,NaN); a.lips.assign(n,NaN);
    for(size_t i=0;i<n;++i){
        if(i>= (size_t)jaw_s && !is_nan(jaw[i-jaw_s])) a.jaw[i]=jaw[i-jaw_s];
        if(i>= (size_t)teeth_s && !is_nan(teeth[i-teeth_s])) a.teeth[i]=teeth[i-teeth_s];
        if(i>= (size_t)lips_s && !is_nan(lips[i-lips_s])) a.lips[i]=lips[i-lips_s];
    }
    return a;
}
vector<double> linear_regression_slope(const vector<double>& src, int period){
    vector<double> out(src.size(), NaN);
    if(period<=1) return out;
    double sum_x= double(period*(period-1))/2.0;
    double sum_x2= double((period-1)*period*(2*period-1))/6.0;
    double denom = period*sum_x2 - sum_x*sum_x;
    if(denom==0) return out;
    for(size_t i=period-1;i<src.size();++i){
        double sum_y=0, sum_xy=0;
        for(int j=0;j<period;++j){ double y=src[i-period+1+j]; sum_y+=y; sum_xy+= j*y; }
        out[i]= (period*sum_xy - sum_x*sum_y)/denom;
    }
    return out;
}
Vortex vortex(const vector<double>& high, const vector<double>& low, const vector<double>& close, int period){
    size_t n=close.size();
    Vortex v; v.vi_plus.assign(n,NaN); v.vi_minus.assign(n,NaN);
    if(n<=1) return v;
    vector<double> tr(n,0), vm_plus(n,0), vm_minus(n,0);
    for(size_t i=1;i<n;++i){
        tr[i]= std::max({high[i]-low[i], std::fabs(high[i]-close[i-1]), std::fabs(low[i]-close[i-1])});
        vm_plus[i]= std::fabs(high[i]-low[i-1]);
        vm_minus[i]= std::fabs(low[i]-high[i-1]);
    }
    vector<double> tr_sum(n,NaN), vm_p_sum(n,NaN), vm_m_sum(n,NaN);
    double s_tr=0, s_p=0, s_m=0;
    for(size_t i=1;i<n;++i){
        s_tr+=tr[i]; s_p+=vm_plus[i]; s_m+=vm_minus[i];
        if(i> (size_t)period){ s_tr-=tr[i-period]; s_p-=vm_plus[i-period]; s_m-=vm_minus[i-period];}
        if(i>= (size_t)period){ tr_sum[i]=s_tr; vm_p_sum[i]=s_p; vm_m_sum[i]=s_m;}
    }
    for(size_t i=0;i<n;++i) if(!is_nan(tr_sum[i]) && tr_sum[i]!=0){ v.vi_plus[i]=vm_p_sum[i]/tr_sum[i]; v.vi_minus[i]=vm_m_sum[i]/tr_sum[i];}
    return v;
}

// ---------- Momentum ----------
vector<double> rsi(const vector<double>& close, int period){
    size_t n=close.size();
    vector<double> out(n,NaN);
    if(n<= (size_t)period) return out;
    vector<double> gains(n,0), losses(n,0);
    for(size_t i=1;i<n;++i){ double d=close[i]-close[i-1]; if(d>0) gains[i]=d; else losses[i]=-d; }
    double avg_g=0, avg_l=0;
    for(int i=1;i<=period;++i){ avg_g+=gains[i]; avg_l+=losses[i];}
    avg_g/=period; avg_l/=period;
    out[period]= avg_l==0? 100 : 100 - 100/(1+avg_g/avg_l);
    for(size_t i=period+1;i<n;++i){
        avg_g = (avg_g*(period-1)+gains[i])/period;
        avg_l = (avg_l*(period-1)+losses[i])/period;
        out[i]= avg_l==0? 100 : 100 - 100/(1+avg_g/avg_l);
    }
    return out;
}
Stoch stochastic(const vector<double>& high, const vector<double>& low, const vector<double>& close, int k_period, int d_period){
    size_t n=close.size();
    Stoch s; s.k.assign(n,NaN); s.d.assign(n,NaN);
    auto hmax=rolling_max(high,k_period);
    auto lmin=rolling_min(low,k_period);
    for(size_t i=0;i<n;++i){
        if(is_nan(hmax[i])||is_nan(lmin[i])) continue;
        double range=hmax[i]-lmin[i];
        if(range==0) s.k[i]=50; else s.k[i]=100*(close[i]-lmin[i])/range;
    }
    // %D = SMA of %K
    auto d_sma = sma(s.k, d_period);
    // sma will propagate NaN via sum? Our sma with prefix sum will include NaN -> not ideal.
    // So compute manually ignoring NaN
    for(size_t i=d_period-1;i<n;++i){
        bool ok=true; double sum=0; for(int j=0;j<d_period;++j) if(is_nan(s.k[i-j])) ok=false; else sum+=s.k[i-j];
        if(ok) s.d[i]=sum/d_period;
    }
    return s;
}
vector<double> cci(const vector<double>& high, const vector<double>& low, const vector<double>& close, int period){
    size_t n=close.size();
    vector<double> out(n,NaN);
    vector<double> tp(n); for(size_t i=0;i<n;++i) tp[i]=(high[i]+low[i]+close[i])/3.0;
    auto tp_sma = sma(tp, period);
    for(size_t i=period-1;i<n;++i){
        if(is_nan(tp_sma[i])) continue;
        double md=0; for(int j=0;j<period;++j) md+= std::fabs(tp[i-j]-tp_sma[i]);
        md/=period;
        if(md==0) out[i]=0; else out[i]=(tp[i]-tp_sma[i])/(0.015*md);
    }
    return out;
}
vector<double> roc(const vector<double>& src, int period){
    vector<double> out(src.size(),NaN);
    for(size_t i=period;i<src.size();++i) if(src[i-period]!=0) out[i]=100*(src[i]-src[i-period])/src[i-period];
    return out;
}
vector<double> williams_r(const vector<double>& high, const vector<double>& low, const vector<double>& close, int period){
    vector<double> out(close.size(),NaN);
    auto hmax=rolling_max(high,period);
    auto lmin=rolling_min(low,period);
    for(size_t i=0;i<close.size();++i){
        if(is_nan(hmax[i])||is_nan(lmin[i])) continue;
        double range=hmax[i]-lmin[i];
        if(range==0) out[i]=-50; else out[i]= -100*(hmax[i]-close[i])/range;
    }
    return out;
}
vector<double> ultimate_osc(const vector<double>& high, const vector<double>& low, const vector<double>& close, int p1,int p2,int p3){
    size_t n=close.size();
    vector<double> out(n,NaN);
    vector<double> bp(n,0), tr(n,0);
    for(size_t i=1;i<n;++i){
        bp[i]= close[i]- std::min(low[i], close[i-1]);
        tr[i]= std::max({high[i]-low[i], std::fabs(high[i]-close[i-1]), std::fabs(low[i]-close[i-1])});
    }
    auto sum = [&](int p, size_t i){ double sb=0, st=0; for(int j=0;j<p;++j){ sb+=bp[i-j]; st+=tr[i-j]; } return st==0?0:sb/st; };
    for(size_t i=std::max({p1,p2,p3}); i<n; ++i){
        double a1=sum(p1,i), a2=sum(p2,i), a3=sum(p3,i);
        out[i]=100*(4*a1+2*a2+a3)/7.0;
    }
    return out;
}
vector<double> mfi(const vector<double>& high, const vector<double>& low, const vector<double>& close, const vector<double>& vol, int period){
    size_t n=close.size();
    vector<double> out(n,NaN);
    vector<double> tp(n), mf(n);
    for(size_t i=0;i<n;++i){ tp[i]=(high[i]+low[i]+close[i])/3.0; mf[i]=tp[i]*vol[i];}
    for(size_t i=period;i<n;++i){
        double pos=0, neg=0;
        for(int j=0;j<period;++j){
            size_t idx=i-j;
            if(idx==0) continue;
            if(tp[idx] > tp[idx-1]) pos+=mf[idx];
            else if(tp[idx] < tp[idx-1]) neg+=mf[idx];
        }
        if(neg==0) out[i]=100;
        else {
            double mr=pos/neg;
            out[i]=100 - 100/(1+mr);
        }
    }
    return out;
}
vector<double> awesome_osc(const vector<double>& high, const vector<double>& low){
    size_t n=high.size();
    vector<double> out(n,NaN);
    vector<double> hl2(n); for(size_t i=0;i<n;++i) hl2[i]=(high[i]+low[i])/2.0;
    auto sma5=sma(hl2,5);
    auto sma34=sma(hl2,34);
    for(size_t i=0;i<n;++i) if(!is_nan(sma5[i]) && !is_nan(sma34[i])) out[i]=sma5[i]-sma34[i];
    return out;
}
vector<double> ema_generic(const vector<double>& s, int p){ return ema(s,p); }
vector<double> tsi(const vector<double>& close, int r, int s){
    size_t n=close.size();
    vector<double> out(n,NaN);
    if(n<2) return out;
    vector<double> mom(n,0); for(size_t i=1;i<n;++i) mom[i]=close[i]-close[i-1];
    vector<double> amom(n); for(size_t i=0;i<n;++i) amom[i]=std::fabs(mom[i]);
    auto ema1 = ema(mom, r);
    auto aema1 = ema(amom, r);
    // second smoothing
    vector<double> ema2(n,NaN), aema2(n,NaN);
    // need to EMA the EMA output handling NaN
    // extract valid tail
    size_t first=0; while(first<n && is_nan(ema1[first])) ++first;
    if(first+s > n) return out;
    vector<double> seg(ema1.begin()+first, ema1.end());
    vector<double> aseg(aema1.begin()+first, aema1.end());
    auto seg2=ema(seg, s);
    auto aseg2=ema(aseg, s);
    for(size_t i=0;i<seg2.size();++i){ ema2[first+i]=seg2[i]; aema2[first+i]=aseg2[i];}
    for(size_t i=0;i<n;++i) if(!is_nan(ema2[i]) && !is_nan(aema2[i]) && aema2[i]!=0) out[i]=100*ema2[i]/aema2[i];
    return out;
}
vector<double> dpo(const vector<double>& close, int period){
    vector<double> out(close.size(), NaN);
    int shift = period/2 +1;
    auto sm = sma(close, period);
    for(size_t i=shift;i<close.size();++i) if(!is_nan(sm[i-shift])) out[i]=close[i]-sm[i-shift];
    return out;
}

// ---------- Volatility ----------
BB bollinger(const vector<double>& src, int period, double mult){
    BB b; size_t n=src.size();
    b.upper.assign(n,NaN); b.middle.assign(n,NaN); b.lower.assign(n,NaN);
    auto mid = sma(src, period);
    b.middle=mid;
    for(size_t i=period-1;i<n;++i){
        if(is_nan(mid[i])) continue;
        double m=mid[i];
        // compute stddev on window
        double var=0; for(int j=0;j<period;++j){ double d=src[i-j]-m; var+=d*d; } var/=period;
        double sd=std::sqrt(var);
        b.upper[i]=m+mult*sd;
        b.lower[i]=m-mult*sd;
    }
    return b;
}
vector<double> atr(const vector<double>& high, const vector<double>& low, const vector<double>& close, int period){
    size_t n=close.size();
    vector<double> out(n,NaN);
    if(n<=1) return out;
    vector<double> tr(n,0);
    for(size_t i=1;i<n;++i) tr[i]= std::max({high[i]-low[i], std::fabs(high[i]-close[i-1]), std::fabs(low[i]-close[i-1])});
    // Wilder
    double sum=0; for(int i=1;i<=period && (size_t)i<n;++i) sum+=tr[i];
    if((size_t)period < n) out[period]=sum/period;
    for(size_t i=period+1;i<n;++i) out[i]=(out[i-1]*(period-1)+tr[i])/period;
    return out;
}
Keltner keltner(const vector<double>& high, const vector<double>& low, const vector<double>& close, int ema_p, int atr_p, double mult){
    Keltner k; size_t n=close.size();
    k.upper.assign(n,NaN); k.middle.assign(n,NaN); k.lower.assign(n,NaN);
    auto ema_mid=ema(close, ema_p);
    auto atr_v=atr(high,low,close,atr_p);
    k.middle=ema_mid;
    for(size_t i=0;i<n;++i) if(!is_nan(ema_mid[i]) && !is_nan(atr_v[i])){ k.upper[i]=ema_mid[i]+mult*atr_v[i]; k.lower[i]=ema_mid[i]-mult*atr_v[i];}
    return k;
}
Donchian donchian(const vector<double>& high, const vector<double>& low, int period){
    Donchian d; size_t n=high.size();
    d.upper.assign(n,NaN); d.lower.assign(n,NaN); d.middle.assign(n,NaN);
    auto hmax=rolling_max(high,period);
    auto lmin=rolling_min(low,period);
    d.upper=hmax; d.lower=lmin;
    for(size_t i=0;i<n;++i) if(!is_nan(hmax[i])&&!is_nan(lmin[i])) d.middle[i]=(hmax[i]+lmin[i])/2.0;
    return d;
}
vector<double> stddev(const vector<double>& src, int period){
    vector<double> out(src.size(),NaN);
    auto mid=sma(src,period);
    for(size_t i=period-1;i<src.size();++i){
        if(is_nan(mid[i])) continue;
        double m=mid[i]; double var=0; for(int j=0;j<period;++j){ double d=src[i-j]-m; var+=d*d;} var/=period;
        out[i]=std::sqrt(var);
    }
    return out;
}
vector<double> hist_vol(const vector<double>& close, int period){
    vector<double> out(close.size(),NaN);
    vector<double> logret(close.size(),0);
    for(size_t i=1;i<close.size();++i) if(close[i-1]!=0) logret[i]=std::log(close[i]/close[i-1]);
    for(size_t i=period;i<close.size();++i){
        double sum=0; for(int j=0;j<period;++j) sum+=logret[i-j];
        double mean=sum/period;
        double var=0; for(int j=0;j<period;++j){ double d=logret[i-j]-mean; var+=d*d; } var/=period;
        out[i]=std::sqrt(var)*std::sqrt(252)*100; // annualized %
    }
    return out;
}
vector<double> chaikin_vol(const vector<double>& high, const vector<double>& low, int ema_p){
    size_t n=high.size();
    vector<double> out(n,NaN);
    vector<double> hl(n); for(size_t i=0;i<n;++i) hl[i]=high[i]-low[i];
    auto e = ema(hl, ema_p);
    for(size_t i=ema_p;i<n;++i){
        if(is_nan(e[i])||is_nan(e[i-ema_p])) continue;
        if(e[i-ema_p]==0) out[i]=0; else out[i]=100*(e[i]-e[i-ema_p])/e[i-ema_p];
    }
    return out;
}
vector<double> ulcer_index(const vector<double>& close, int period){
    vector<double> out(close.size(),NaN);
    for(size_t i=period-1;i<close.size();++i){
        double peak=close[i-period+1];
        for(int j=1;j<period;++j) peak=std::max(peak, close[i-period+1+j]);
        double sum_sq=0;
        for(int j=0;j<period;++j){
            double dd = 100*(close[i-j]-peak)/peak;
            if(dd<0) sum_sq+=dd*dd;
        }
        out[i]= std::sqrt(sum_sq/period);
    }
    return out;
}
Raff raff_channel(const vector<double>& close, int period, double dev_mult){
    Raff r; size_t n=close.size();
    r.upper.assign(n,NaN); r.lower.assign(n,NaN); r.middle.assign(n,NaN);
    double sum_x= double(period*(period-1))/2.0;
    double sum_x2= double((period-1)*period*(2*period-1))/6.0;
    double denom = period*sum_x2 - sum_x*sum_x;
    for(size_t i=period-1;i<n;++i){
        double sum_y=0,sum_xy=0;
        for(int j=0;j<period;++j){ double y=close[i-period+1+j]; sum_y+=y; sum_xy+= j*y; }
        double slope = (period*sum_xy - sum_x*sum_y)/denom;
        double intercept = (sum_y - slope*sum_x)/period;
        // regression line values
        double last_val = slope*(period-1)+intercept;
        r.middle[i]=last_val;
        // stddev of residuals
        double var=0;
        for(int j=0;j<period;++j){ double y=close[i-period+1+j]; double pred=slope*j+intercept; double d=y-pred; var+=d*d; } var/=period;
        double sd=std::sqrt(var);
        r.upper[i]=last_val + dev_mult*sd;
        r.lower[i]=last_val - dev_mult*sd;
    }
    return r;
}
vector<double> volatility_ratio(const vector<double>& high, const vector<double>& low, const vector<double>& close, int short_p, int long_p){
    size_t n=close.size();
    vector<double> out(n,NaN);
    auto atr_short = atr(high,low,close, short_p);
    auto atr_long = atr(high,low,close, long_p);
    for(size_t i=0;i<n;++i) if(!is_nan(atr_short[i]) && !is_nan(atr_long[i]) && atr_long[i]!=0) out[i]=atr_short[i]/atr_long[i];
    return out;
}

// ---------- Volume ----------
vector<double> obv(const vector<double>& close, const vector<double>& vol){
    vector<double> out(close.size(),0);
    out[0]=vol[0];
    for(size_t i=1;i<close.size();++i){
        if(close[i] > close[i-1]) out[i]=out[i-1]+vol[i];
        else if(close[i] < close[i-1]) out[i]=out[i-1]-vol[i];
        else out[i]=out[i-1];
    }
    return out;
}
vector<double> vwap(const vector<double>& high, const vector<double>& low, const vector<double>& close, const vector<double>& vol, int period){
    vector<double> out(close.size(),NaN);
    vector<double> tp(close.size()); for(size_t i=0;i<close.size();++i) tp[i]=(high[i]+low[i]+close[i])/3.0;
    for(size_t i=period-1;i<close.size();++i){
        double sum_pv=0, sum_v=0;
        for(int j=0;j<period;++j){ sum_pv+=tp[i-j]*vol[i-j]; sum_v+=vol[i-j];}
        if(sum_v!=0) out[i]=sum_pv/sum_v;
    }
    return out;
}
vector<double> cmf(const vector<double>& high, const vector<double>& low, const vector<double>& close, const vector<double>& vol, int period){
    size_t n=close.size();
    vector<double> out(n,NaN);
    vector<double> mfv(n,0);
    for(size_t i=0;i<n;++i){
        double range=high[i]-low[i];
        double mfm=0;
        if(range!=0) mfm=((close[i]-low[i])-(high[i]-close[i]))/range;
        mfv[i]=mfm*vol[i];
    }
    for(size_t i=period-1;i<n;++i){
        double sum_mfv=0, sum_v=0;
        for(int j=0;j<period;++j){ sum_mfv+=mfv[i-j]; sum_v+=vol[i-j];}
        if(sum_v!=0) out[i]=sum_mfv/sum_v;
    }
    return out;
}
vector<double> volume_osc(const vector<double>& vol, int fast, int slow){
    vector<double> out(vol.size(),NaN);
    auto ef=ema(vol,fast);
    auto es=ema(vol,slow);
    for(size_t i=0;i<vol.size();++i) if(!is_nan(ef[i]) && !is_nan(es[i]) && es[i]!=0) out[i]=100*(ef[i]-es[i])/es[i];
    return out;
}
vector<double> ad_line(const vector<double>& high, const vector<double>& low, const vector<double>& close, const vector<double>& vol){
    size_t n=close.size();
    vector<double> out(n,0);
    double cum=0;
    for(size_t i=0;i<n;++i){
        double range=high[i]-low[i];
        double mfm=0;
        if(range!=0) mfm=((close[i]-low[i])-(high[i]-close[i]))/range;
        cum+= mfm*vol[i];
        out[i]=cum;
    }
    return out;
}
vector<double> ease_of_movement(const vector<double>& high, const vector<double>& low, const vector<double>& vol, int period){
    size_t n=high.size();
    vector<double> out(n,NaN);
    vector<double> eom(n,0);
    for(size_t i=1;i<n;++i){
        double dm = (high[i]+low[i])/2.0 - (high[i-1]+low[i-1])/2.0;
        double br = vol[i]/(high[i]-low[i] ==0? 1: high[i]-low[i]);
        if(br!=0) eom[i]=dm/br;
    }
    auto sm = sma(eom, period);
    out=sm;
    return out;
}
vector<double> volume_roc(const vector<double>& vol, int period){ return roc(vol, period); }
vector<double> force_index(const vector<double>& close, const vector<double>& vol, int period){
    size_t n=close.size();
    vector<double> fi(n,0);
    for(size_t i=1;i<n;++i) fi[i]=(close[i]-close[i-1])*vol[i];
    return ema(fi, period);
}
Klinger klinger(const vector<double>& high, const vector<double>& low, const vector<double>& close, const vector<double>& vol){
    size_t n=close.size();
    Klinger k; k.kvo.assign(n,NaN); k.signal.assign(n,NaN);
    vector<double> trend(n,0);
    // Simplified Klinger: HLC trend vs 55 EMA
    vector<double> hl2(n); for(size_t i=0;i<n;++i) hl2[i]=(high[i]+low[i]+close[i])/3.0;
    // compute cumulative
    vector<double> dm(n,0), cm(n,0);
    for(size_t i=1;i<n;++i){
        dm[i]= high[i]-low[i];
        cm[i]=0;
        if(high[i]-low[i]==0) cm[i]=0;
        else cm[i]= std::fabs((high[i]-low[i]) - dm[i-1]); // simplified
    }
    vector<double> vf(n,0);
    for(size_t i=0;i<n;++i){
        // volume force = vol * trend * dm
        // trend = +1 if hl2 > prev hl2 else -1
        int tr = (i>0 && hl2[i] > hl2[i-1])? 1 : -1;
        vf[i]= vol[i] * tr * dm[i];
    }
    auto kvo34=ema(vf,34);
    auto kvo55=ema(vf,55);
    for(size_t i=0;i<n;++i) if(!is_nan(kvo34[i]) && !is_nan(kvo55[i])) k.kvo[i]=kvo34[i]-kvo55[i];
    // signal EMA 13 of KVO
    size_t first=0; while(first<n && is_nan(k.kvo[first])) ++first;
    if(first+13 < n){
        vector<double> seg(k.kvo.begin()+first, k.kvo.end());
        auto sig=ema(seg,13);
        for(size_t i=0;i<sig.size();++i) k.signal[first+i]=sig[i];
    }
    return k;
}
vector<double> vpci(const vector<double>& close, const vector<double>& vol, int short_p, int long_p){
    size_t n=close.size();
    vector<double> out(n,NaN);
    auto sma_short = sma(close, short_p);
    auto sma_long = sma(close, long_p);
    auto vol_sma_short = sma(vol, short_p);
    auto vol_sma_long = sma(vol, long_p);
    vector<double> vpc(n,0), vpr(n,0);
    for(size_t i=0;i<n;++i){
        if(is_nan(sma_short[i])||is_nan(sma_long[i])) continue;
        vpc[i]= sma_short[i]-sma_long[i];
        if(!is_nan(vol_sma_short[i]) && !is_nan(vol_sma_long[i]) && vol_sma_long[i]!=0)
            vpr[i]= vol_sma_short[i]/vol_sma_long[i] -1;
        else vpr[i]=0;
    }
    // VPCI = VPC * VPR ??? simplified * price
    for(size_t i=0;i<n;++i){
        if(is_nan(vpc[i])) continue;
        out[i]= vpc[i]* (1+vpr[i]);
    }
    // smooth with EMA?
    return out;
}

} // namespace ind
