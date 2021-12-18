// 名前にドットを含む命令の呼び出し用のクラスの定義
// このファイルは自動生成されたファイルなので変更しないでください
private:

class DotImpl_lr {
  friend self_t;
  self_t *parent;
public:

  constexpr inline void w(const IntReg& rd, const IntOffsetReg& rs1) const { parent->lr_w(rd, rs1); }
  DotImpl_lr(self_t *p) : 
    parent(p){}
};

class DotImpl_sc {
  friend self_t;
  self_t *parent;
public:

  constexpr inline void w(const IntReg& rd, const IntReg& rs2, const IntOffsetReg& rs1) const { parent->sc_w(rd, rs2, rs1); }
  DotImpl_sc(self_t *p) : 
    parent(p){}
};

class DotImpl_amoswap {
  friend self_t;
  self_t *parent;
public:

  constexpr inline void w(const IntReg& rd, const IntReg& rs2, const IntOffsetReg& rs1) const { parent->amoswap_w(rd, rs2, rs1); }
  DotImpl_amoswap(self_t *p) : 
    parent(p){}
};

class DotImpl_amoadd {
  friend self_t;
  self_t *parent;
public:

  constexpr inline void w(const IntReg& rd, const IntReg& rs2, const IntOffsetReg& rs1) const { parent->amoadd_w(rd, rs2, rs1); }
  DotImpl_amoadd(self_t *p) : 
    parent(p){}
};

class DotImpl_amoxor {
  friend self_t;
  self_t *parent;
public:

  constexpr inline void w(const IntReg& rd, const IntReg& rs2, const IntOffsetReg& rs1) const { parent->amoxor_w(rd, rs2, rs1); }
  DotImpl_amoxor(self_t *p) : 
    parent(p){}
};

class DotImpl_amoand {
  friend self_t;
  self_t *parent;
public:

  constexpr inline void w(const IntReg& rd, const IntReg& rs2, const IntOffsetReg& rs1) const { parent->amoand_w(rd, rs2, rs1); }
  DotImpl_amoand(self_t *p) : 
    parent(p){}
};

class DotImpl_amoor {
  friend self_t;
  self_t *parent;
public:

  constexpr inline void w(const IntReg& rd, const IntReg& rs2, const IntOffsetReg& rs1) const { parent->amoor_w(rd, rs2, rs1); }
  DotImpl_amoor(self_t *p) : 
    parent(p){}
};

class DotImpl_amomin {
  friend self_t;
  self_t *parent;
public:

  constexpr inline void w(const IntReg& rd, const IntReg& rs2, const IntOffsetReg& rs1) const { parent->amomin_w(rd, rs2, rs1); }
  DotImpl_amomin(self_t *p) : 
    parent(p){}
};

class DotImpl_amomax {
  friend self_t;
  self_t *parent;
public:

  constexpr inline void w(const IntReg& rd, const IntReg& rs2, const IntOffsetReg& rs1) const { parent->amomax_w(rd, rs2, rs1); }
  DotImpl_amomax(self_t *p) : 
    parent(p){}
};

class DotImpl_amominu {
  friend self_t;
  self_t *parent;
public:

  constexpr inline void w(const IntReg& rd, const IntReg& rs2, const IntOffsetReg& rs1) const { parent->amominu_w(rd, rs2, rs1); }
  DotImpl_amominu(self_t *p) : 
    parent(p){}
};

class DotImpl_amomaxu {
  friend self_t;
  self_t *parent;
public:

  constexpr inline void w(const IntReg& rd, const IntReg& rs2, const IntOffsetReg& rs1) const { parent->amomaxu_w(rd, rs2, rs1); }
  DotImpl_amomaxu(self_t *p) : 
    parent(p){}
};

class DotImpl_fmadd {
  friend self_t;
  self_t *parent;
public:

  constexpr inline void s(const FpReg& rd, const FpReg& rs1, const FpReg& rs2, const FpReg& rs3, RoundingMode rm = RoundingMode::dyn) const { parent->fmadd_s(rd, rs1, rs2, rs3, rm); }
  constexpr inline void d(const FpReg& rd, const FpReg& rs1, const FpReg& rs2, const FpReg& rs3, RoundingMode rm = RoundingMode::dyn) const { parent->fmadd_d(rd, rs1, rs2, rs3, rm); }
  DotImpl_fmadd(self_t *p) : 
    parent(p){}
};

class DotImpl_fmsub {
  friend self_t;
  self_t *parent;
public:

  constexpr inline void s(const FpReg& rd, const FpReg& rs1, const FpReg& rs2, const FpReg& rs3, RoundingMode rm = RoundingMode::dyn) const { parent->fmsub_s(rd, rs1, rs2, rs3, rm); }
  constexpr inline void d(const FpReg& rd, const FpReg& rs1, const FpReg& rs2, const FpReg& rs3, RoundingMode rm = RoundingMode::dyn) const { parent->fmsub_d(rd, rs1, rs2, rs3, rm); }
  DotImpl_fmsub(self_t *p) : 
    parent(p){}
};

class DotImpl_fnmsub {
  friend self_t;
  self_t *parent;
public:

  constexpr inline void s(const FpReg& rd, const FpReg& rs1, const FpReg& rs2, const FpReg& rs3, RoundingMode rm = RoundingMode::dyn) const { parent->fnmsub_s(rd, rs1, rs2, rs3, rm); }
  constexpr inline void d(const FpReg& rd, const FpReg& rs1, const FpReg& rs2, const FpReg& rs3, RoundingMode rm = RoundingMode::dyn) const { parent->fnmsub_d(rd, rs1, rs2, rs3, rm); }
  DotImpl_fnmsub(self_t *p) : 
    parent(p){}
};

class DotImpl_fnmadd {
  friend self_t;
  self_t *parent;
public:

  constexpr inline void s(const FpReg& rd, const FpReg& rs1, const FpReg& rs2, const FpReg& rs3, RoundingMode rm = RoundingMode::dyn) const { parent->fnmadd_s(rd, rs1, rs2, rs3, rm); }
  constexpr inline void d(const FpReg& rd, const FpReg& rs1, const FpReg& rs2, const FpReg& rs3, RoundingMode rm = RoundingMode::dyn) const { parent->fnmadd_d(rd, rs1, rs2, rs3, rm); }
  DotImpl_fnmadd(self_t *p) : 
    parent(p){}
};

class DotImpl_fadd {
  friend self_t;
  self_t *parent;
public:

  constexpr inline void s(const FpReg& rd, const FpReg& rs1, const FpReg& rs2, RoundingMode rm = RoundingMode::dyn) const { parent->fadd_s(rd, rs1, rs2, rm); }
  constexpr inline void d(const FpReg& rd, const FpReg& rs1, const FpReg& rs2, RoundingMode rm = RoundingMode::dyn) const { parent->fadd_d(rd, rs1, rs2, rm); }
  DotImpl_fadd(self_t *p) : 
    parent(p){}
};

class DotImpl_fsub {
  friend self_t;
  self_t *parent;
public:

  constexpr inline void s(const FpReg& rd, const FpReg& rs1, const FpReg& rs2, RoundingMode rm = RoundingMode::dyn) const { parent->fsub_s(rd, rs1, rs2, rm); }
  constexpr inline void d(const FpReg& rd, const FpReg& rs1, const FpReg& rs2, RoundingMode rm = RoundingMode::dyn) const { parent->fsub_d(rd, rs1, rs2, rm); }
  DotImpl_fsub(self_t *p) : 
    parent(p){}
};

class DotImpl_fmul {
  friend self_t;
  self_t *parent;
public:

  constexpr inline void s(const FpReg& rd, const FpReg& rs1, const FpReg& rs2, RoundingMode rm = RoundingMode::dyn) const { parent->fmul_s(rd, rs1, rs2, rm); }
  constexpr inline void d(const FpReg& rd, const FpReg& rs1, const FpReg& rs2, RoundingMode rm = RoundingMode::dyn) const { parent->fmul_d(rd, rs1, rs2, rm); }
  DotImpl_fmul(self_t *p) : 
    parent(p){}
};

class DotImpl_fdiv {
  friend self_t;
  self_t *parent;
public:

  constexpr inline void s(const FpReg& rd, const FpReg& rs1, const FpReg& rs2, RoundingMode rm = RoundingMode::dyn) const { parent->fdiv_s(rd, rs1, rs2, rm); }
  constexpr inline void d(const FpReg& rd, const FpReg& rs1, const FpReg& rs2, RoundingMode rm = RoundingMode::dyn) const { parent->fdiv_d(rd, rs1, rs2, rm); }
  DotImpl_fdiv(self_t *p) : 
    parent(p){}
};

class DotImpl_fsqrt {
  friend self_t;
  self_t *parent;
public:

  constexpr inline void s(const FpReg& rd, const FpReg& rs1, RoundingMode rm = RoundingMode::dyn) const { parent->fsqrt_s(rd, rs1, rm); }
  constexpr inline void d(const FpReg& rd, const FpReg& rs1, RoundingMode rm = RoundingMode::dyn) const { parent->fsqrt_d(rd, rs1, rm); }
  DotImpl_fsqrt(self_t *p) : 
    parent(p){}
};

class DotImpl_fsgnj {
  friend self_t;
  self_t *parent;
public:

  constexpr inline void s(const FpReg& rd, const FpReg& rs1, const FpReg& rs2) const { parent->fsgnj_s(rd, rs1, rs2); }
  constexpr inline void d(const FpReg& rd, const FpReg& rs1, const FpReg& rs2) const { parent->fsgnj_d(rd, rs1, rs2); }
  DotImpl_fsgnj(self_t *p) : 
    parent(p){}
};

class DotImpl_fsgnjn {
  friend self_t;
  self_t *parent;
public:

  constexpr inline void s(const FpReg& rd, const FpReg& rs1, const FpReg& rs2) const { parent->fsgnjn_s(rd, rs1, rs2); }
  constexpr inline void d(const FpReg& rd, const FpReg& rs1, const FpReg& rs2) const { parent->fsgnjn_d(rd, rs1, rs2); }
  DotImpl_fsgnjn(self_t *p) : 
    parent(p){}
};

class DotImpl_fsgnjx {
  friend self_t;
  self_t *parent;
public:

  constexpr inline void s(const FpReg& rd, const FpReg& rs1, const FpReg& rs2) const { parent->fsgnjx_s(rd, rs1, rs2); }
  constexpr inline void d(const FpReg& rd, const FpReg& rs1, const FpReg& rs2) const { parent->fsgnjx_d(rd, rs1, rs2); }
  DotImpl_fsgnjx(self_t *p) : 
    parent(p){}
};

class DotImpl_fmin {
  friend self_t;
  self_t *parent;
public:

  constexpr inline void s(const FpReg& rd, const FpReg& rs1, const FpReg& rs2) const { parent->fmin_s(rd, rs1, rs2); }
  constexpr inline void d(const FpReg& rd, const FpReg& rs1, const FpReg& rs2) const { parent->fmin_d(rd, rs1, rs2); }
  DotImpl_fmin(self_t *p) : 
    parent(p){}
};

class DotImpl_fmax {
  friend self_t;
  self_t *parent;
public:

  constexpr inline void s(const FpReg& rd, const FpReg& rs1, const FpReg& rs2) const { parent->fmax_s(rd, rs1, rs2); }
  constexpr inline void d(const FpReg& rd, const FpReg& rs1, const FpReg& rs2) const { parent->fmax_d(rd, rs1, rs2); }
  DotImpl_fmax(self_t *p) : 
    parent(p){}
};

class DotImpl_fcvt_w {
  friend self_t;
  self_t *parent;
public:

  constexpr inline void s(const IntReg& rd, const FpReg& rs1, RoundingMode rm = RoundingMode::dyn) const { parent->fcvt_w_s(rd, rs1, rm); }
  constexpr inline void d(const IntReg& rd, const FpReg& rs1, RoundingMode rm = RoundingMode::dyn) const { parent->fcvt_w_d(rd, rs1, rm); }
  DotImpl_fcvt_w(self_t *p) : 
    parent(p){}
};

class DotImpl_fcvt_wu {
  friend self_t;
  self_t *parent;
public:

  constexpr inline void s(const IntReg& rd, const FpReg& rs1, RoundingMode rm = RoundingMode::dyn) const { parent->fcvt_wu_s(rd, rs1, rm); }
  constexpr inline void d(const IntReg& rd, const FpReg& rs1, RoundingMode rm = RoundingMode::dyn) const { parent->fcvt_wu_d(rd, rs1, rm); }
  DotImpl_fcvt_wu(self_t *p) : 
    parent(p){}
};

class DotImpl_fcvt_s {
  friend self_t;
  self_t *parent;
public:

  constexpr inline void w(const FpReg& rd, const IntReg& rs1, RoundingMode rm = RoundingMode::dyn) const { parent->fcvt_s_w(rd, rs1, rm); }
  constexpr inline void wu(const FpReg& rd, const IntReg& rs1, RoundingMode rm = RoundingMode::dyn) const { parent->fcvt_s_wu(rd, rs1, rm); }
  constexpr inline void d(const FpReg& rd, const FpReg& rs1, RoundingMode rm = RoundingMode::dyn) const { parent->fcvt_s_d(rd, rs1, rm); }
  DotImpl_fcvt_s(self_t *p) : 
    parent(p){}
};

class DotImpl_fcvt_d {
  friend self_t;
  self_t *parent;
public:

  constexpr inline void s(const FpReg& rd, const FpReg& rs1) const { parent->fcvt_d_s(rd, rs1); }
  constexpr inline void w(const FpReg& rd, const IntReg& rs1) const { parent->fcvt_d_w(rd, rs1); }
  constexpr inline void wu(const FpReg& rd, const IntReg& rs1) const { parent->fcvt_d_wu(rd, rs1); }
  DotImpl_fcvt_d(self_t *p) : 
    parent(p){}
};

class DotImpl_fcvt {
  friend self_t;
  self_t *parent;
public:
  DotImpl_fcvt_w w;
  DotImpl_fcvt_wu wu;
  DotImpl_fcvt_s s;
  DotImpl_fcvt_d d;


  DotImpl_fcvt(self_t *p) : 
    parent(p), w(p), wu(p), s(p), d(p){}
};

class DotImpl_fmv_x {
  friend self_t;
  self_t *parent;
public:

  constexpr inline void w(const IntReg& rd, const FpReg& rs1) const { parent->fmv_x_w(rd, rs1); }
  DotImpl_fmv_x(self_t *p) : 
    parent(p){}
};

class DotImpl_fmv_w {
  friend self_t;
  self_t *parent;
public:

  constexpr inline void x(const FpReg& rd, const IntReg& rs1) const { parent->fmv_w_x(rd, rs1); }
  DotImpl_fmv_w(self_t *p) : 
    parent(p){}
};

class DotImpl_fmv {
  friend self_t;
  self_t *parent;
public:
  DotImpl_fmv_x x;
  DotImpl_fmv_w w;

  constexpr inline void s(const FpReg& rd, const FpReg& rs) const { parent->fmv_s(rd, rs); }
  constexpr inline void d(const FpReg& rd, const FpReg& rs) const { parent->fmv_d(rd, rs); }
  DotImpl_fmv(self_t *p) : 
    parent(p), x(p), w(p){}
};

class DotImpl_feq {
  friend self_t;
  self_t *parent;
public:

  constexpr inline void s(const IntReg& rd, const FpReg& rs1, const FpReg& rs2) const { parent->feq_s(rd, rs1, rs2); }
  constexpr inline void d(const IntReg& rd, const FpReg& rs1, const FpReg& rs2) const { parent->feq_d(rd, rs1, rs2); }
  DotImpl_feq(self_t *p) : 
    parent(p){}
};

class DotImpl_flt {
  friend self_t;
  self_t *parent;
public:

  constexpr inline void s(const IntReg& rd, const FpReg& rs1, const FpReg& rs2) const { parent->flt_s(rd, rs1, rs2); }
  constexpr inline void d(const IntReg& rd, const FpReg& rs1, const FpReg& rs2) const { parent->flt_d(rd, rs1, rs2); }
  DotImpl_flt(self_t *p) : 
    parent(p){}
};

class DotImpl_fle {
  friend self_t;
  self_t *parent;
public:

  constexpr inline void s(const IntReg& rd, const FpReg& rs1, const FpReg& rs2) const { parent->fle_s(rd, rs1, rs2); }
  constexpr inline void d(const IntReg& rd, const FpReg& rs1, const FpReg& rs2) const { parent->fle_d(rd, rs1, rs2); }
  DotImpl_fle(self_t *p) : 
    parent(p){}
};

class DotImpl_fclass {
  friend self_t;
  self_t *parent;
public:

  constexpr inline void s(const IntReg& rd, const FpReg& rs1) const { parent->fclass_s(rd, rs1); }
  constexpr inline void d(const IntReg& rd, const FpReg& rs1) const { parent->fclass_d(rd, rs1); }
  DotImpl_fclass(self_t *p) : 
    parent(p){}
};

class DotImpl_fabs {
  friend self_t;
  self_t *parent;
public:

  constexpr inline void s(const FpReg& rd, const FpReg& rs) const { parent->fabs_s(rd, rs); }
  constexpr inline void d(const FpReg& rd, const FpReg& rs) const { parent->fabs_d(rd, rs); }
  DotImpl_fabs(self_t *p) : 
    parent(p){}
};

class DotImpl_fneg {
  friend self_t;
  self_t *parent;
public:

  constexpr inline void s(const FpReg& rd, const FpReg& rs) const { parent->fneg_s(rd, rs); }
  constexpr inline void d(const FpReg& rd, const FpReg& rs) const { parent->fneg_d(rd, rs); }
  DotImpl_fneg(self_t *p) : 
    parent(p){}
};

public:
  DotImpl_lr lr;
  DotImpl_sc sc;
  DotImpl_amoswap amoswap;
  DotImpl_amoadd amoadd;
  DotImpl_amoxor amoxor;
  DotImpl_amoand amoand;
  DotImpl_amoor amoor;
  DotImpl_amomin amomin;
  DotImpl_amomax amomax;
  DotImpl_amominu amominu;
  DotImpl_amomaxu amomaxu;
  DotImpl_fmadd fmadd;
  DotImpl_fmsub fmsub;
  DotImpl_fnmsub fnmsub;
  DotImpl_fnmadd fnmadd;
  DotImpl_fadd fadd;
  DotImpl_fsub fsub;
  DotImpl_fmul fmul;
  DotImpl_fdiv fdiv;
  DotImpl_fsqrt fsqrt;
  DotImpl_fsgnj fsgnj;
  DotImpl_fsgnjn fsgnjn;
  DotImpl_fsgnjx fsgnjx;
  DotImpl_fmin fmin;
  DotImpl_fmax fmax;
  DotImpl_fcvt fcvt;
  DotImpl_fmv fmv;
  DotImpl_feq feq;
  DotImpl_flt flt;
  DotImpl_fle fle;
  DotImpl_fclass fclass;
  DotImpl_fabs fabs;
  DotImpl_fneg fneg;

CodeGenerator(std::size_t size = 4096) :
    Registers(), st(size), lr(this), sc(this), amoswap(this), amoadd(this), amoxor(this), amoand(this), amoor(this), amomin(this), amomax(this), amominu(this), amomaxu(this), fmadd(this), fmsub(this), fnmsub(this), fnmadd(this), fadd(this), fsub(this), fmul(this), fdiv(this), fsqrt(this), fsgnj(this), fsgnjn(this), fsgnjx(this), fmin(this), fmax(this), fcvt(this), fmv(this), feq(this), flt(this), fle(this), fclass(this), fabs(this), fneg(this){}
