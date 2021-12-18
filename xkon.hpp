
#define _CRT_SECURE_NO_WARNINGS

/**
 * 課題リスト
 *
 * * 絶対アドレス指定の call/tail がまともに動かない。
 *   おそらくアドレスの定義が定まってないため。
 *   ホストとRV32のアドレスがごっちゃになって破綻してる。
 *   明確に型を定義して使用するように修正が必要。
 * * 後方のラベルを参照する命令で、かつ圧縮命令が存在する場合
 *   1パス目で相対アドレスの範囲が圧縮命令の範囲外でも圧縮命令を生成すると仮定してしまい、
 *   1パス目で通常命令を生成してラベルのアドレスが変化してしまうケースがある。
 *   2パス目でラベルのアドレス変化を検出して自動で3パス目を走らせることも考えたが、
 *   3パス目でも同様のケースが起こったり、さらには何回パスを追加しても
 *   ラベルのアドレスが収束しないケースが無いことをすぐには証明できない。
 *   そのため、代替案としてラベルが圧縮命令で対応できない範囲に入る可能性がある場合
 *   明示的にfar()で囲むことで圧縮命令の生成を抑止する実装として、
 *   ユーザーに問題の抑制を丸投げした。
 *   -> 命令生成lambdaに命令サイズ変化の可能性フラグを追加して、
 *      2パス目でずれを検出したらリストをさかのぼりつつ必要な個数だけフラグ箇所にfar指定を強制追加してやり直す
 *   -> 2パス目で、ラベル自身でずれを検出して例外を飛ばすorASSERTで殺す
 *
 * 引数にラベルを指定可能で、圧縮命令になる可能性のある命令において、
 * 命令より後方（アドレスが大きい）ラベルを引数に指定する場合、
 * ラベルのアドレスとの距離が圧縮命令で表現可能な範囲を超えてしまう可能性がある場合は、
 * ラベルをfar()で囲んで指定してください。
 * この指定を誤ると、ラベルのアドレス決定処理でアドレスにずれが生じ、
 * 正常に動作しない命令列を生成してしまいます。
 * ※ラベルのアドレス決定処理では圧縮命令の生成に常に成功するが、
 *   命令生成時は相対アドレスが遠い場合、アドレス決定時と異なり
 *   通常命令が生成されれ、結果としてアドレスがずれる。
 *
 * * unsupportedの呼び出しがちゃんと入っているか？
 *   ->確認して入ってないところに入れた
 * * ニーモニック生成処理を遅延評価で極力動作させないようにする
 *   ->準備だけはした。ニーモニック出力のON/OFF機能は未実装
 */

#include <algorithm>
#include <cstdint>
#include <exception>
#include <functional>
#include <list>
#include <map>
#include <string>

#include "bitbuilder.hpp"

#if 0
void dummy(const char* what) { throw std::runtime_error(what); }
#define XKON_ASSERT(expr) \
  do {                    \
    if (!(expr)) {        \
      dummy(#expr);       \
    }                     \
  } while (0)
#else
#define XKON_ASSERT(expr) assert(expr)
#endif

#if not +0
// and or not を関数名として使用できる設定になっているか、
// 本家と同じ手法でエラー判定する
#error "use -fno-operator-names"
#define XKON_OPERATER_NAMES_ARE_USEABLE 0
#else
#define XKON_OPERATER_NAMES_ARE_USEABLE 1
#endif

#define XKON_INSN_NAME(x) x
#define XKON_LAZY(expr) [&]() -> const Format { return (expr); }
//#define XKON_LAZY(expr) [=]()->const Format{ return Format("x"); }

// 開墾
namespace xkon {
typedef uint32_t uint32;
typedef int32_t int32;
typedef unsigned long long addr_t;
typedef signed long long addrdiff_t;

using namespace BitBuilder;

/**
 * 命令セットアーキテクチャ
 * 
 * misaレジスタ風に、対応している拡張とCPUのビット数のビットマップ
 */
enum Isa {
  // Extensions
  EXT_I = 0x00000100,
  // EXT_E = 0x00000010,

  EXT_M = 0x00001000,
  EXT_A = 0x00000001,
  EXT_F = 0x00000020,
  EXT_D = 0x00000008,
  //   EXT_Q = 0x00010000,
  //   EXT_L = 0x00000800,
  EXT_C = 0x00000004,
  //   EXT_B = 0x00000002,
  //   EXT_J = 0x00000200,
  //   EXT_T = 0x00080000,
  //   EXT_P = 0x00008000,
  //   EXT_V = 0x00200000,
  //   EXT_N = 0x00002000,
  EXT_G = 0x00000040 | EXT_I | EXT_M | EXT_A | EXT_F | EXT_D,

  // Bases
  RV32 = 0x04000000,
  RV64 = 0x08000000,
  RV128 = 0x10000000,

  // Major variations
  RV32I = RV32 | EXT_I,
  RV32IC = RV32 | EXT_I | EXT_C,
  RV32IMA = RV32 | EXT_I | EXT_M | EXT_A,
  RV32G = RV32 | EXT_G,
  RV32GC = RV32 | EXT_G | EXT_C,
  RV64I = RV64 | EXT_I,
};

////////////////////////////////////////////////////////////////////////////////
// 例外

/// 「未サポート」例外クラス
/// 指定されたISAでは対応していない命令の生成を試みた際に発生する例外
class UnsupportedException : public std::exception {
  std::string m_what;

 public:
  UnsupportedException(const std::string& what) noexcept : exception(), m_what(what) {}
  UnsupportedException(const std::exception& e) noexcept : exception(e), m_what("") {}
  UnsupportedException(const UnsupportedException& o) noexcept { this->m_what = o.m_what; }
  virtual ~UnsupportedException() {}
  UnsupportedException& operator=(const UnsupportedException&) noexcept = default;
  virtual const char* what() const noexcept { return m_what.c_str(); }
};

////////////////////////////////////////////////////////////////////////////////
// ラベル

////////////////////////////////////////////////////////////////////////////////
// レジスタ

/// レジスタ名を整数にエンコードする
constexpr unsigned long enc(const char* s) {
  unsigned int v = 0;
  for (; *s != '\0'; s++) {
    v = (v << 8) | (*s & 0xff);
  }
  return v;
}

/// enc()でエンコードされた整数を文字列に戻す
std::string dec(unsigned long v) {
  char buf[5] = {0};
  int i = 0;
  if (v & 0xff000000u) {
    buf[i++] = (v >> 24) & 0xff;
  }
  if (v & 0x00ff0000u) {
    buf[i++] = (v >> 16) & 0xff;
  }
  if (v & 0x0000ff00u) {
    buf[i++] = (v >> 8) & 0xff;
  }
  if (v & 0x000000ffu) {
    buf[i++] = (v >> 0) & 0xff;
  }
  return std::string(buf);
}

/**
 *  レジスタ用変数から暗黙の型変換で変換されるクラス
 */
struct RegBase {
  const int idx;  /**< レジスタのインデックス番号 */
  const int cidx; /**< 圧縮命令用のレジスタのインデックス番号*/
  const std::string name;
  RegBase(int idx, int cidx, unsigned long name) : idx(idx), cidx(cidx), name(dec(name)) {}

  bool isC() const { return 0 <= cidx; }
  Constant Idx() const { return Constant(5, idx); }
  Constant CIdx() const {
    XKON_ASSERT(0 <= cidx);
    return Constant(3, cidx);
  }
};

// 整数レジスタ
struct IntOffsetReg;
struct IntReg : public RegBase {
  IntReg(int idx, int cidx, unsigned long name) : RegBase(idx, cidx, name) {}
  bool operator==(const IntReg& o) const { return this->idx == o.idx; }
  bool operator!=(const IntReg& o) const { return this->idx != o.idx; }
  IntOffsetReg operator()(long offset = 0) const;
};

struct IntOffsetReg : public IntReg {
  const long offset;
  IntOffsetReg(long offset, int idx, int cidx, unsigned long name) : IntReg(idx, cidx, name), offset(offset) {}
};

IntOffsetReg IntReg::operator()(long offset) const {
  IntOffsetReg res(offset, idx, cidx, enc(name.c_str()));
  return res;
}

template <int id, unsigned long name = 0, int cid = -1>
struct IReg {
  operator IntReg() const {
    using namespace std;
    return IntReg(id, cid, (name == 0) ? enc(("x"s + to_string(id)).c_str()) : name);
  }

  operator IntOffsetReg() const {
    using namespace std;
    return IntOffsetReg(0, id, cid, (name == 0) ? enc(("x"s + to_string(id)).c_str()) : name);
  }

  IntOffsetReg operator()(long offset = 0) const {
    using namespace std;
    return IntOffsetReg(offset, id, cid, (name == 0) ? enc(("x"s + to_string(id)).c_str()) : name);
  }
  IntOffsetReg operator[](long offset) const {
    using namespace std;
    return IntOffsetReg(offset, id, cid, (name == 0) ? enc(("x"s + to_string(id)).c_str()) : name);
  }
};  // namespace xkon

// 浮動小数点数レジスタ
struct FpReg : public RegBase {
  FpReg(int idx, int cidx, unsigned long name) : RegBase(idx, cidx, name) {}
  bool operator==(const FpReg& o) const { return this->idx == o.idx; }
  bool operator!=(const FpReg& o) const { return this->idx != o.idx; }
};

template <int id, unsigned long name = 0, int cid = -1>
struct FReg {
  operator FpReg() const {
    using namespace std;
    return FpReg(id, cid, (name == 0) ? enc(("f"s + to_string(id)).c_str()) : name);
  }
};

////////////////////////////////////////////////////////////////////////////////
// コード生成クラスの定義

/**
 * RISC-V の仕様上定義されているレジスタを表す変数を定義したクラス
 */
struct Registers {
  // 整数レジスタの定義
  static const IReg<0, enc("zero")> x0, zero;     // ---
  static const IReg<1, enc("ra")> x1, ra;         // caller
  static const IReg<2, enc("sp")> x2, sp;         // CALLEE
  static const IReg<3, enc("gp")> x3, gp;         // ---
  static const IReg<4, enc("tp")> x4, tp;         // ---
  static const IReg<5, enc("t0")> x5, t0;         // caller
  static const IReg<6, enc("t1")> x6, t1;         // caller
  static const IReg<7, enc("t2")> x7, t2;         // caller
  static const IReg<8, enc("s0"), 0> x8, s0, fp;  // CALLEE
  static const IReg<9, enc("s1"), 1> x9, s1;      // CALLEE
  static const IReg<10, enc("a0"), 2> x10, a0;    // caller
  static const IReg<11, enc("a1"), 3> x11, a1;    // caller
  static const IReg<12, enc("a2"), 4> x12, a2;    // caller
  static const IReg<13, enc("a3"), 5> x13, a3;    // caller
  static const IReg<14, enc("a4"), 6> x14, a4;    // caller
  static const IReg<15, enc("a5"), 7> x15, a5;    // caller
  static const IReg<16, enc("a6")> x16, a6;       // caller
  static const IReg<17, enc("a7")> x17, a7;       // caller
  static const IReg<18, enc("s2")> x18, s2;       // CALLEE
  static const IReg<19, enc("s3")> x19, s3;       // CALLEE
  static const IReg<20, enc("s4")> x20, s4;       // CALLEE
  static const IReg<21, enc("s5")> x21, s5;       // CALLEE
  static const IReg<22, enc("s6")> x22, s6;       // CALLEE
  static const IReg<23, enc("s7")> x23, s7;       // CALLEE
  static const IReg<24, enc("s8")> x24, s8;       // CALLEE
  static const IReg<25, enc("s9")> x25, s9;       // CALLEE
  static const IReg<26, enc("s10")> x26, s10;     // CALLEE
  static const IReg<27, enc("s11")> x27, s11;     // CALLEE
  static const IReg<28, enc("t3")> x28, t3;       // caller
  static const IReg<29, enc("t4")> x29, t4;       // caller
  static const IReg<30, enc("t5")> x30, t5;       // caller
  static const IReg<31, enc("t6")> x31, t6;       // caller

  // 浮動小数点数レジスタの定義
  static const FReg<0, enc("ft0")> f0, ft0;       // caller
  static const FReg<1, enc("ft1")> f1, ft1;       // caller
  static const FReg<2, enc("ft2")> f2, ft2;       // caller
  static const FReg<3, enc("ft3")> f3, ft3;       // caller
  static const FReg<4, enc("ft4")> f4, ft4;       // caller
  static const FReg<5, enc("ft5")> f5, ft5;       // caller
  static const FReg<6, enc("ft6")> f6, ft6;       // caller
  static const FReg<7, enc("ft7")> f7, ft7;       // caller
  static const FReg<8, enc("fs0"), 0> f8, fs0;    // CALLEE
  static const FReg<9, enc("fs1"), 1> f9, fs1;    // CALLEE
  static const FReg<10, enc("fa0"), 2> f10, fa0;  // caller
  static const FReg<11, enc("fa1"), 3> f11, fa1;  // caller
  static const FReg<12, enc("fa2"), 4> f12, fa2;  // caller
  static const FReg<13, enc("fa3"), 5> f13, fa3;  // caller
  static const FReg<14, enc("fa4"), 6> f14, fa4;  // caller
  static const FReg<15, enc("fa5"), 7> f15, fa5;  // caller
  static const FReg<16, enc("fa6")> f16, fa6;     // caller
  static const FReg<17, enc("fa7")> f17, fa7;     // caller
  static const FReg<18, enc("fs2")> f18, fs2;     // CALLEE
  static const FReg<19, enc("fs3")> f19, fs3;     // CALLEE
  static const FReg<20, enc("fs4")> f20, fs4;     // CALLEE
  static const FReg<21, enc("fs5")> f21, fs5;     // CALLEE
  static const FReg<22, enc("fs6")> f22, fs6;     // CALLEE
  static const FReg<23, enc("fs7")> f23, fs7;     // CALLEE
  static const FReg<24, enc("fs8")> f24, fs8;     // CALLEE
  static const FReg<25, enc("fs9")> f25, fs9;     // CALLEE
  static const FReg<26, enc("fs10")> f26, fs10;   // CALLEE
  static const FReg<27, enc("fs11")> f27, fs11;   // CALLEE
  static const FReg<28, enc("ft8")> f28, ft8;     // caller
  static const FReg<29, enc("ft9")> f29, ft9;     // caller
  static const FReg<30, enc("ft10")> f30, ft10;   // caller
  static const FReg<31, enc("ft11")> f31, ft11;   // caller
};

// レジスタ用変数のインスタンス化
// gccでは Registers クラスのインスタンスを用意すればOKだったが、
// MSVCでは各staticメンバー変数の実体を明示的に用意しないと
// リンク時にエラーが発生したので 明示的に実体を用意する

#define XKON_REGDEF(reg) decltype(Registers::reg) Registers::reg

XKON_REGDEF(x0);
XKON_REGDEF(zero);
XKON_REGDEF(x1);
XKON_REGDEF(ra);
XKON_REGDEF(x2);
XKON_REGDEF(sp);
XKON_REGDEF(x3);
XKON_REGDEF(gp);
XKON_REGDEF(x4);
XKON_REGDEF(tp);
XKON_REGDEF(x5);
XKON_REGDEF(t0);
XKON_REGDEF(x6);
XKON_REGDEF(t1);
XKON_REGDEF(x7);
XKON_REGDEF(t2);
XKON_REGDEF(x8);
XKON_REGDEF(s0);
XKON_REGDEF(fp);
XKON_REGDEF(x9);
XKON_REGDEF(s1);
XKON_REGDEF(x10);
XKON_REGDEF(a0);
XKON_REGDEF(x11);
XKON_REGDEF(a1);
XKON_REGDEF(x12);
XKON_REGDEF(a2);
XKON_REGDEF(x13);
XKON_REGDEF(a3);
XKON_REGDEF(x14);
XKON_REGDEF(a4);
XKON_REGDEF(x15);
XKON_REGDEF(a5);
XKON_REGDEF(x16);
XKON_REGDEF(a6);
XKON_REGDEF(x17);
XKON_REGDEF(a7);
XKON_REGDEF(x18);
XKON_REGDEF(s2);
XKON_REGDEF(x19);
XKON_REGDEF(s3);
XKON_REGDEF(x20);
XKON_REGDEF(s4);
XKON_REGDEF(x21);
XKON_REGDEF(s5);
XKON_REGDEF(x22);
XKON_REGDEF(s6);
XKON_REGDEF(x23);
XKON_REGDEF(s7);
XKON_REGDEF(x24);
XKON_REGDEF(s8);
XKON_REGDEF(x25);
XKON_REGDEF(s9);
XKON_REGDEF(x26);
XKON_REGDEF(s10);
XKON_REGDEF(x27);
XKON_REGDEF(s11);
XKON_REGDEF(x28);
XKON_REGDEF(t3);
XKON_REGDEF(x29);
XKON_REGDEF(t4);
XKON_REGDEF(x30);
XKON_REGDEF(t5);
XKON_REGDEF(x31);
XKON_REGDEF(t6);
XKON_REGDEF(f0);
XKON_REGDEF(ft0);
XKON_REGDEF(f1);
XKON_REGDEF(ft1);
XKON_REGDEF(f2);
XKON_REGDEF(ft2);
XKON_REGDEF(f3);
XKON_REGDEF(ft3);
XKON_REGDEF(f4);
XKON_REGDEF(ft4);
XKON_REGDEF(f5);
XKON_REGDEF(ft5);
XKON_REGDEF(f6);
XKON_REGDEF(ft6);
XKON_REGDEF(f7);
XKON_REGDEF(ft7);
XKON_REGDEF(f8);
XKON_REGDEF(fs0);
XKON_REGDEF(f9);
XKON_REGDEF(fs1);
XKON_REGDEF(f10);
XKON_REGDEF(fa0);
XKON_REGDEF(f11);
XKON_REGDEF(fa1);
XKON_REGDEF(f12);
XKON_REGDEF(fa2);
XKON_REGDEF(f13);
XKON_REGDEF(fa3);
XKON_REGDEF(f14);
XKON_REGDEF(fa4);
XKON_REGDEF(f15);
XKON_REGDEF(fa5);
XKON_REGDEF(f16);
XKON_REGDEF(fa6);
XKON_REGDEF(f17);
XKON_REGDEF(fa7);
XKON_REGDEF(f18);
XKON_REGDEF(fs2);
XKON_REGDEF(f19);
XKON_REGDEF(fs3);
XKON_REGDEF(f20);
XKON_REGDEF(fs4);
XKON_REGDEF(f21);
XKON_REGDEF(fs5);
XKON_REGDEF(f22);
XKON_REGDEF(fs6);
XKON_REGDEF(f23);
XKON_REGDEF(fs7);
XKON_REGDEF(f24);
XKON_REGDEF(fs8);
XKON_REGDEF(f25);
XKON_REGDEF(fs9);
XKON_REGDEF(f26);
XKON_REGDEF(fs10);
XKON_REGDEF(f27);
XKON_REGDEF(fs11);
XKON_REGDEF(f28);
XKON_REGDEF(ft8);
XKON_REGDEF(f29);
XKON_REGDEF(ft9);
XKON_REGDEF(f30);
XKON_REGDEF(ft10);
XKON_REGDEF(f31);
XKON_REGDEF(ft11);

#undef XKON_REGDEF

class Allocator {
  // 根本の原因は判らないが、spike で動作確認を行っていると
  // メモリに書き込んだ命令をうまく読みだせず落ちる。
  // 試行錯誤した結果、new したメモリ領域から2048バイトにアライメント
  // したメモリを使用するとうまく動いたので対症療法として
  // アライメント処理を追加した。
  const size_t ALIGN = 2048;

  size_t size;
  void* ptr;
  bool self_allocated;  // メモリ確保を自前でやったフラグ
  char* pMem;

 public:
  Allocator() : size(0), ptr(nullptr), self_allocated(false), pMem(nullptr) {}

  virtual ~Allocator() {
    if (self_allocated) {
      delete[](unsigned char*) this->ptr;
    }
  }

  void allocate(size_t size, void* ptr) {
    this->size = size;
    this->ptr = ptr;

    if (this->ptr == nullptr) {
      this->ptr = new unsigned char[size + ALIGN];
      self_allocated = true;
      // アライメント調整
      intptr_t p = (intptr_t)this->ptr;
      p = (p + ALIGN - 1) & (~(ALIGN - 1));
      this->pMem = (char*)p;
    } else {
      this->pMem = (char*)ptr;
    }
    assert(this->ptr != nullptr);
    assert(this->pMem != nullptr);
  }

  char* getMemory() const { return pMem; }
  size_t getSize() const { return size; }
};  // namespace internal

class Strage;

// nameが空文字列の場合は、絶対アドレスaddressを指すための相対アドレスを表し、
// それ以外の場合はラベルで指定された相対アドレスを指す
struct Label {
  const Strage* pS;
  const std::string name;
  const addr_t address;
  const bool isFar;  ///< 圧縮命令で届かないアドレスであること明示するためのフラグ

  Label(const Strage* pS, const char* name, bool isFar = false) : pS(pS), name(name), address(0), isFar(isFar) {}

 public:
  // ラベル指定用コンストラクタ
  Label(Strage& strage, const char* name, bool isFar = false) : pS(&strage), name(name), address(0), isFar(isFar) {}

  // アドレス指定用コンストラクタ
  Label(Strage& strage, addr_t address) : pS(&strage), name(""), address(address), isFar(false) {}

  addrdiff_t relAddr() const;  // Strageクラスの関数を呼ぶので後方で定義
  addr_t absAddr() const;      // Strageクラスの関数を呼ぶので後方で定義
                               // 確保したメモリー領域の先頭からのオフセット
  addr_t value() const;        // Strageクラスの関数を呼ぶので後方で定義

  bool isNear() const { return !isFar; }

  Label toFar() const { return Label(pS, name.c_str(), true); }

  std::string dump() const {
    addrdiff_t addr = relAddr();
    if (name.empty()) {
      char buf[64];
      snprintf(buf, sizeof(buf) - 1, "0x%llx", addr);
      return std::string(buf);
    } else {
      char sign = (0 <= addr) ? '+' : '-';
      if (addr < 0) {
        addr = -addr;
      }
      char buf[64];
      snprintf(buf, sizeof(buf) - 1, ":pc%c0x%llx", sign, addr);
      return name + buf;
    }
  }
};

/**
 * 命令のニーモニック表記文字列生成用フォーマットクラス
 */
class Format {
  /// フォーマット済み文字列
  const std::string str;

  /// 未処理のフォーマット指定文字列
  const std::string format;

  const enum Type {
    TypeOp = 'o',     ///< 命令
    TypeIReg = 'i',   ///<整数レジスタ
    TypeIOReg = 'I',  ///<オフセット値付き整数レジスタ
    TypeIJReg = 'J',  ///<オフセット値付き整数レジスタ(jalr用)
    TypeIMReg = 'M',  ///<オフセット値付き整数レジスタ(メモリ)
    TypeFReg = 'f',   ///<浮動小数点数レジスタ
    TypeSimm = 's',   ///<符号有り即値
    TypeUimm = 'u',   ///<符号なし即値
    TypeRM = 'r',     ///<丸めモード
    TypeLabel = 'L',  ///<ラベル文字列
    TypeRem = '#'     ///<注釈コメント
  } type;

  Format(const std::string& str, const std::string& format)  //
      : str(str), format(&format[1]), type(static_cast<enum Type>(format[0])) {}

  std::string sep() const {
    using namespace std;
    if (str.empty() || str.back() == ' ') {
      return ""s;
    } else {
      return ","s;
    }
  }

 public:
  Format(const char* format) : str(), format(format[0] ? &format[1] : ""), type(static_cast<enum Type>(format[0])) {}

  Format operator%(const IntOffsetReg& r) const {
    using namespace std;
    XKON_ASSERT(type == TypeIReg || type == TypeIOReg || type == TypeIJReg || type == TypeIMReg);
    switch (type) {
      case TypeIReg:
        return Format(str + sep() + r.name, format);
      case TypeIOReg:
        return Format(str + sep() + std::to_string(r.offset) + "("s + r.name + ")"s, format);
        break;
      case TypeIMReg:
        XKON_ASSERT(r.offset == 0);
        return Format(str + sep() + "("s + r.name + ")"s, format);
      case TypeIJReg:
        if (r.offset == 0) {
          return Format(str + sep() + r.name, format);
        } else {
          return Format(str + sep() + std::to_string(r.offset) + "("s + r.name + ")"s, format);
        }
        break;
      default:
        XKON_ASSERT(0);
        break;
    }
    XKON_ASSERT(0);
    return Format("");
  }

  Format operator%(const IntReg& r) const {
    using namespace std;
    XKON_ASSERT(type == TypeIReg || type == TypeIOReg);
    switch (type) {
      case TypeIReg:
        return Format(str + sep() + r.name, format);
      default:
        XKON_ASSERT(0);
        break;
    }
    XKON_ASSERT(0);
    return Format("");
  }

  Format operator%(const FpReg& f) const {
    XKON_ASSERT(type == TypeFReg);
    return Format(str + sep() + f.name, format);
  }
  static const char* rm2s(int rm) {
    const char* name = nullptr;
    switch (rm) {
      case 0:
        name = "rne";
        break;
      case 1:
        name = "rtz";
        break;
      case 2:
        name = "rdn";
        break;
      case 3:
        name = "rup";
        break;
      case 4:
        name = "rmm";
        break;
      case 7:
        // name = "dyn";
        break;
      default:
        XKON_ASSERT(0);
        break;
    }
    return name;
  }
  Format operator%(int imm) const {
    XKON_ASSERT(type == TypeSimm || type == TypeUimm || type == TypeRM);
    char buf[64] = {0};
    switch (type) {
      case TypeSimm:
        std::snprintf(buf, sizeof(buf) - 1, "%d", imm);
        break;
      case TypeUimm:
        std::snprintf(buf, sizeof(buf) - 1, "0x%x", imm);
        break;
      case TypeRM: {
        const char* sym = rm2s(imm);
        if (sym == nullptr) {
          return Format(str, format);
        }
        std::snprintf(buf, sizeof(buf) - 1, "%s", sym);
        break;
      }
      default:
        XKON_ASSERT(0);
        break;
    }
    return Format(str + sep() + buf, format);
  }

  Format operator%(long imm) const { return (*this) % static_cast<int>(imm); }
  Format operator%(unsigned long imm) const { return (*this) % static_cast<int>(imm); }
  Format operator%(unsigned int imm) const { return (*this) % static_cast<int>(imm); }

  Format operator%(const std::string& s) const { return (*this) % (s.c_str()); }
  Format operator%(const Label& l) const {
    // DEBUG
    if (true) {
      XKON_ASSERT(type == TypeLabel);
      char buf[256];
      snprintf(buf, sizeof(buf) - 1, "%llx <%s>", l.value(), l.name.c_str());
      return Format(str + sep() + buf, format);
    } else {
      return (*this) % (l.dump().c_str());
    }
  }

  Format operator%(const char* s) const {
    using namespace std;
    XKON_ASSERT(type == TypeOp || type == TypeRem || type == TypeLabel);
    switch (type) {
      case TypeOp:
        return Format(str + s + " "s, format);
      case TypeRem:
        return Format(str + "\t# "s + s + " "s, format);
      case TypeLabel:
        return Format(str + sep() + "<"s + s + ">"s, format);
      default:
        break;
    }
    XKON_ASSERT(0);
    return Format("");
  }

  operator std::string() const { return str; }
};  // namespace xkon

class Strage {
  inline Strage(const Strage&) = delete;

  typedef std::function<void(Strage&)> InsnGen_t;

  Allocator mem;

  // ラベル管理
  std::map<std::string, addr_t> labelMap;

  // 命令の生成管理
  addr_t p;                    ///< メモリーの書込み位置インデックス
  addr_t pc;                   ///< 現在の処理中の命令の先頭アドレス
  std::list<InsnGen_t> insns;  ///< 命令生成関数のリスト
  bool inGenerate;             ///< false:insnsへの命令生成lambda式追加とラベルのアドレス決定モード true:命令生成モード
  unsigned int lastInsn;       ///< 最後に生成した命令のopコード

  FILE* fp;  // DEBUG

 public:
  Strage(std::size_t size) : mem(), labelMap(), p(0), pc(0), insns(), inGenerate(false), lastInsn(), fp(nullptr) { mem.allocate(size, nullptr); }

  void operator<<(InsnGen_t ig) {
    ig(*this);
    pc = p;
    insns.push_back(ig);
  }

  // コード生成
  char* generate() {
    // DEBUG
    fp = fopen("out.s", "w");
    fprintf(fp, "%s",
            "\t.file   \"out.s\"\n"
            "\t.option nopic\n"
            "\t.text\n"
            "\t.align 1\n"
            "\t.globl  f\n"
            "\t.type   f, @function\n"
            "f:\n");

    // 変数の初期化
    p = 0;
    pc = 0;
    inGenerate = true;
    int count = 0;  ///< デバッグメッセージ用の文字列出力タイミング制御カウンタ

    // 命令生成メインループ
    for (auto e : insns) {
      // デバッグメッセージ用の文字列出力
      if (count++ == 0) {
#if DEBUG
        printf("%s", "Address OPcode  ------- Instruction --------------------------------------------\n");
#endif
      } else if (16 <= count) {
        count = 0;
      }

      // 命令生成用のlambda式の実行
      e(*this);

      // PCを更新
      pc = p;
    }
    inGenerate = false;

#if DEBUG
    printf("%llu bytes generated.\n", p);
#endif

    // DEBUG
    fprintf(fp, "%s",
            ""  //
                // "\tnop\n"
                // "\tnop\n"
                // "\tfmul.s ft0,ft1,ft2\n"
                // "\tfmul.s ft0,ft1,ft2,rne\n"
                // "\tfmul.s ft0,ft1,ft2,rtz\n"
                // "\tfmul.s ft0,ft1,ft2,rdn\n"
                // "\tfmul.s ft0,ft1,ft2,rup\n"
                // "\tfmul.s ft0,ft1,ft2,rmm\n"
                // "\tfmul.s ft0,ft1,ft2,dyn\n"
                //
    );
    fclose(fp);
    return mem.getMemory();
  }

  // 命令出力

  void hword(unsigned int ui16) {
    XKON_ASSERT(p + 2 <= mem.getSize());
    if (inGenerate) {
      char* pMem = mem.getMemory();
      lastInsn = ui16;
      pMem[p++] = ui16 & 0xff;
      pMem[p++] = (ui16 >> 8) & 0xff;
    } else {
      p += 2;
    }
  }

  void word(unsigned int ui32) {
    XKON_ASSERT(p + 4 <= mem.getSize());
    if (inGenerate) {
      char* pMem = mem.getMemory();
      lastInsn = ui32;
      pMem[p++] = ui32 & 0xff;
      pMem[p++] = (ui32 >> 8) & 0xff;
      pMem[p++] = (ui32 >> 16) & 0xff;
      pMem[p++] = (ui32 >> 24) & 0xff;
    } else {
      p += 4;
    }
  }

  // 命令のテキスト表記出力

  Format format(const char* format) const { return Format(format); }

  typedef std::function<const Format(void)> lazy_expr_t;
  // ニーモニック出力を無効に出来るようにした際に余計な処理を実行しないようにするためラムダ式を使った遅延評価を行う構造にした
  void desc(lazy_expr_t fs) {
    if (!inGenerate) {
      return;
    }

    std::string s = fs();
    // RISC-Vの32ビットサイズの命令は必ず下位ビットが1、それ以外の場合は16ビットサイズの命令なので、それを利用して命令のビット数を判定する
    if ((lastInsn & 3) == 3) {
#if DEBUG
      std::printf("%llx:\t\x1b[35m%08x\t\x1b[36m%s\x1b[0m\n", pc, lastInsn, s.c_str());
#endif
      if (fp != nullptr) {  // DEBUG
        std::fprintf(fp, "\t.word 0x%08x\t#%s\n", lastInsn, s.c_str());
      }
    } else {
#if DEBUG
      std::printf("%llx:\t\x1b[35m    %04x\t\x1b[36m%s\x1b[0m\n", pc, lastInsn, s.c_str());
#endif
      if (fp != nullptr) {  // DEBUG
        std::fprintf(fp, "\t.hword 0x%04x\t#%s\n", lastInsn, s.c_str());
      }
    }
  }

  // 1つの命令の処理中で複数の命令を出力する場合に、PCを強制的に更新するために使う
  void updatePC() { pc = p; }

  // ラベル管理
  void addLabel(const char* label) {
    if (inGenerate) {
#if DEBUG
      printf("\x1b[36m%08llx <%s>\x1b[0m:\n", labelMap[std::string(label)], label);
#endif
      if (fp != nullptr) {  // DEBUG
        std::fprintf(fp, "%s:\n", label);
      }
    } else {
#if DEBUG
      printf("+0x%llx = <%s>\n", pc, label);
#endif
      labelMap[std::string(label)] = pc;
    }
  }

  addrdiff_t getLabelOffset(const char* label) const {
    const auto itr = labelMap.find(std::string(label));
    if (itr == labelMap.end()) {
      if (inGenerate) {
        throw UnsupportedException("Unknown label.");
      } else {
        return 0;
      }
    } else {
      if (itr->second > pc) {
        return itr->second - pc;
      } else {
        return -(pc - itr->second);
      }
    }
  }

  addr_t getLabelValue(const char* label) const {
    const auto itr = labelMap.find(std::string(label));
    if (itr == labelMap.end()) {
      if (inGenerate) {
        throw UnsupportedException("Unknown label.");
      } else {
        return 0;
      }
    } else {
      return itr->second;
    }
  }
  addrdiff_t getLabelOffset(const std::string& label) const { return getLabelOffset(label.c_str()); }
  addr_t getLabelValue(const std::string& label) const { return getLabelValue(label.c_str()); }
  addr_t getPC() const { return pc + (intptr_t)mem.getMemory(); }
};

addrdiff_t Label::relAddr() const {
  if (name.empty()) {
    if (address > pS->getPC()) {
      return address - pS->getPC();
    } else {
      return -(pS->getPC() - address);
    }
  } else {
    return pS->getLabelOffset(name);
  }
}

addr_t Label::absAddr() const {
  if (name.empty()) {
    return address;
  } else {
    return pS->getLabelOffset(name) + pS->getPC();
  }
}

addr_t Label::value() const { return pS->getLabelValue(name); }

/*******************************************************************************
 * コード生成クラス
 ******************************************************************************/

template <Isa support_isa>
class CodeGenerator : public Registers {
  typedef CodeGenerator<support_isa> self_t;

  Strage st;

  //////////////////////////////////////////////////////////////////////////////
  // 内部実装用関数の定義

  // テンプレート引数の support_ias が、 require で示される機能を
  // すべてサポートしている（＝ターゲットである）か判定して返す
  template <int require>
  static inline constexpr bool targetIs() {
    return (support_isa & require) == require;
  }

  // 「命令未サポート例外」を発生させる関数
  // 引数には __func__ を指定する想定なので、
  // 例外メッセージに実際の命令の名前を表示するため、関数名から実際の命令の名前に変換を行う
  // 関数名は、命令の名前のドットを下線に置換し、名前がC++定義済みの演算子と被る場合はさらに末尾に下線を付加した名前にしている。
  static void unsupported(const char* insn_name) {
    using namespace std;

    // 関数名→命令の名前変換処理
    string s(insn_name);
    replace(s.begin(), s.end(), '_', '.');
    if ((!s.empty()) && s.back() == '.') {
      s.pop_back();
    }

    const string what = "'"s + s + "' instruction not supported."s;
    throw UnsupportedException(what);
  }

  //////////////////////////////////////////////////////////////////////////////

 public:
  /// 丸めモード
  enum RoundingMode {
    rne = 0,                     ///<最近の偶数へ丸める
    rtz = 1,                     ///<ゼロに向かって丸める
    rdn = 2,                     ///<切り下げ(-∞方向)
    rup = 3,                     ///<切り上げ(+∞方向)
    rmm = 4,                     ///<最も近い絶対値が大きい方向に丸める
    invalid_rounding_mode5 = 5,  ///< 不正な値。将来使用するため予約。
    invalid_rounding_mode6 = 6,  ///< 不正な値。将来使用するため予約。
    dyn = 7,                     ///<動的丸めモード(丸めモードレジスタでは使用できない値)
  };

  static Constant from(RoundingMode rm) { return Constant(3, rm); }

  template <typename T>
  T generate() {
    char* pExec = st.generate();
    return (T)pExec;
  }

 private:
  /**
   * val を size ビットの符号付整数とみなして、
   * T型の符号付整数に符号拡張して返す
   */
  template <typename T>
  inline constexpr T signextend(T val, int size) {
    const T zero = 0;
    const T one = 1;
    const T signmask = one << (size - 1);
    const T mask = (signmask - one) | signmask;
    const T sign = (~zero) ^ mask;

    val &= mask;
    return ((val & signmask) != 0) ? (sign | val) : val;
  }

  /// val が符号付nビットの整数の範囲内の値ならtrueを返す
  template <typename T>
  bool isSintN(T val, int n) {
    // 切り詰めた後で符号拡張してもとに戻るなら所定の範囲内の値
    return signextend(val, n) == val;
  }

  // val が符号無しnビットの整数の範囲内の値ならtrueを返す
  template <typename T>
  bool isUintN(T val, int n) {
    const T one = 1;
    const T signmask = one << (n - 1);
    const T mask = (signmask - one) | signmask;
    return (mask & val) == val;
  }

  // val が n の倍数であるとき true を返す
  template <typename T>
  bool isAlignedN(T val, int n) {
    return val % n == 0;
  }
  /// ラベル文字列を Label クラスのインスタンスに変換する
  Label str2label(const char* label) { return Label(st, label); }

  /// 絶対アドレスを Label クラスのインスタンスに変換する
  Label addr2label(addr_t addr) { return Label(st, addr); }

 public:
  // 命令より後方のラベルを参照する際、ラベルとの距離が圧縮命令では届かない範囲となる可能性がある場合、
  // 実装の都合上ラベルのアドレス決定処理が誤動作する場合があるので、この関数でラベルを囲って下さい。
  Label far(const char* label) { return Label(st, label, true); }
  Label far(const Label& label) { return label.toFar(); }

 public:
  //////////////////////////////////////////////////////////////////////////////
  // ラベル

  void L(const char* label) {
    std::string l(label);
    st << [=](Strage& s) { st.addLabel(l.c_str()); };
  }

  //////////////////////////////////////////////////////////////////////////////
  // CPU命令の実装
#define XKON_NOINLINE __attribute__((noinline))

  // +impl RV32::I::LUI
  // +impl RV32::C::C.LUI
  void lui(const IntReg& rd, uint32 imm20) {
    if (targetIs<RV32I>()) {
      XKON_ASSERT(isUintN(imm20, 20));
      const uint32 imm = imm20 << 12;

      if (targetIs<EXT_C>() && !(rd == x0 || rd == x2) && (isSintN(signextend(imm20, 20), 6) && imm20 != 0)) {
        const uint32 op = "3'b011"_c << _17[imm] << rd.Idx() << (_16 - _12)[imm] << "2'b01"_c;

        st << [=](Strage& s) {
          s.hword(op);
          s.desc(XKON_LAZY(s.format("oiu#iu") % "lui" % rd % imm20 % "c.lui" % rd % imm20));
        };
      } else {
        const uint32 op = (_31 - _12)[imm] << rd.Idx() << "7'b0110111"_c;

        st << [=](Strage& s) {
          s.word(op);
          s.desc(XKON_LAZY(s.format("oiu") % "lui" % rd % imm20));
        };
      }
    } else {
      unsupported(__func__);
    }
  }

  // +impl RV32::I::AUIPC
  void auipc(const IntReg& rd, uint32 imm20) {
    if (targetIs<RV32I>()) {
      XKON_ASSERT(isUintN(imm20, 20));
      const uint32 imm = imm20 << 12;

      const uint32 op = (_31 - _12)[imm] << rd.Idx() << "7'b0010111"_c;

      st << [=](Strage& s) {
        s.word(op);
        s.desc(XKON_LAZY(s.format("oiu") % "auipc" % rd % imm20));
      };
    } else {
      unsupported(__func__);
    }
  }

  // +impl RV32::I::JAL ++RV64I
  // +impl RV32::C::C.JAL
  // +impl RV32::C::C.J
  // +impl pseudo::j offset (jal x0, offset) Jump
  // +impl pseudo::jal offset (jal x1, offset) Jump and link
  void jal(const IntReg& rd, const Label& label) {
    if (targetIs<RV32I>()|| targetIs<RV64I>()) {
      st << [=](Strage& s) {
        int32 imm21 = static_cast<int32>(label.relAddr());
        // imm21は2の倍数で、符号付21ビット整数で表現可能な値
        XKON_ASSERT(isAlignedN(imm21, 2) && isSintN(imm21, 21));

        bool done = false;
        if (targetIs<EXT_C>() && label.isNear() && isSintN(imm21, 12)) {
          auto imm = (_11 | _4 | _9 - _8 | _10 | _6 | _7 | _3 - _1 | _5)[imm21];
          if (rd == ra) {
            done = true;

            const uint32 op = "3'b001"_c << imm << "2'b01"_c;

            s.hword(op);
            s.desc(XKON_LAZY(s.format("oL#L") % "jal" % label % "c.jal" % label));
          } else if (rd == zero) {
            done = true;

            const uint32 op = "3'b101"_c << imm << "2'b01"_c;

            s.hword(op);
            s.desc(XKON_LAZY(s.format("oL#L") % "j" % label % "c.j" % label));
          }
        }

        if (!done) {
          const uint32 op = (_20 | (_10 - _1) | _11 | (_19 - _12))[imm21] << rd.Idx() << "7'b1101111"_c;

          s.word(op);
          // NOTE:objdump は jal offset を jal ra, offset の形で出力する
          if (rd == zero) {
            s.desc(XKON_LAZY(s.format("oL") % "j" % label));
          } else {
            s.desc(XKON_LAZY(s.format("oiL") % "jal" % rd % label));
          }
        }
      };
    } else {
      unsupported(__func__);
    }
  }
  void jal(const IntReg& rd, const char* label) { jal(rd, str2label(label)); }
  void jal(const IntReg& rd, addr_t addr) { jal(rd, addr2label(addr)); }

  // +impl RV32::I::JALR ++RV64I
  // +impl RV32::C::C.JALR
  // +impl RV32::C::C.JR
  // +impl pseudo::ret (jalr x0, 0(x1)) Return from subroutine
  // +impl pseudo::jr rs (jalr x0, 0(rs)) Jump register
  // +impl pseudo::jalr rs (jalr x1, 0(rs)) Jump and link register
  void jalr(const IntReg& rd, const IntOffsetReg& rs1) {
    if (targetIs<RV32I>() || targetIs<RV64I>()) {
      st << [=](Strage& s) {
        int32 imm12 = rs1.offset;
        // imm12は符号付12ビット整数で表現可能な値
        XKON_ASSERT(isSintN(imm12, 12));

        bool done = false;
        if (targetIs<EXT_C>() && rs1 != zero && imm12 == 0) {
          if (rd == ra) {
            done = true;

            const uint32 op = "3'b100"_c
                              << "1'b1"_c << rs1.Idx() << "7'b0000010"_c;
            s.hword(op);
            s.desc(XKON_LAZY(s.format("oi#i") % "jalr" % rs1 % "c.jalr" % rs1));
          } else if (rd == zero) {
            done = true;

            const uint32 op = "3'b100"_c
                              << "1'b0"_c << rs1.Idx() << "7'b0000010"_c;
            s.hword(op);
            if (rs1 == ra) {
              s.desc(XKON_LAZY(s.format("o#i") % "ret" % "c.jr" % rs1));
            } else {
              s.desc(XKON_LAZY(s.format("oi#i") % "jr" % rs1 % "c.jr" % rs1));
            }
          }
        }

        if (!done) {
          const uint32 op = (_11 - _0)[imm12] << rs1.Idx() << "3'b000"_c << rd.Idx() << "7'b1100111"_c;
          s.word(op);
          if (rd == ra && imm12 == 0) {
            s.desc(XKON_LAZY(s.format("oi") % "jalr" % rs1));
          } else if (rd == zero && rs1 == ra && imm12 == 0) {
            s.desc(XKON_LAZY(s.format("o") % "ret"));
          } else if (rd == zero && imm12 == 0) {
            s.desc(XKON_LAZY(s.format("oi") % "jr" % rs1));
          } else {
            s.desc(XKON_LAZY(s.format("oiJ") % "jalr" % rd % rs1));
          }
        }
      };
    } else {
      unsupported(__func__);
    }
  }

  // +impl RV32::I::BEQ
  // +impl RV32::C::C.BEQZ
  // +impl pseudo::beqz rs, offset (beq rs, x0, offset) Branch if = zero
  void beq(const IntReg& rs1, const IntReg& rs2, const Label& label) {
    if (targetIs<RV32I>()) {
      st << [=](Strage& s) {
        int32 imm13 = static_cast<int32>(label.relAddr());
        // imm13は2の倍数で符号付13ビット整数で表現可能な値
        XKON_ASSERT(isAlignedN(imm13, 2) && isSintN(imm13, 12));

        bool done = false;
        if (targetIs<EXT_C>() && label.isNear() && rs1.isC() && rs2 == zero && (isSintN(imm13, 9) && isAlignedN(imm13, 2))) {
          done = true;

          const uint32 op = "3'b110"_c << (_8 | _4 - _3)[imm13] << rs1.CIdx() << (_7 - _6 | _2 - _1 | _5)[imm13] << "2'b01"_c;
          s.hword(op);
          s.desc(XKON_LAZY(s.format("oiL#iL") % "beqz" % rs1 % label % "c.beqz" % rs1 % label));
        }

        if (!done) {
          const uint32 op = (_12 | (_10 - _5))[imm13] << rs2.Idx() << rs1.Idx() << "3'b000"_c << ((_4 - _1) | _11)[imm13] << "7'b1100011"_c;
          s.word(op);
          if (rs2 == zero) {
            s.desc(XKON_LAZY(s.format("oiL") % "beqz" % rs1 % label));
          } else {
            s.desc(XKON_LAZY(s.format("oiiL") % "beq" % rs1 % rs2 % label));
          }
        }
      };
    } else {
      unsupported(__func__);
    }
  }
  void beq(const IntReg& rs1, const IntReg& rs2, const char* label) { beq(rs1, rs2, str2label(label)); }
  void beq(const IntReg& rs1, const IntReg& rs2, addr_t addr) { beq(rs1, rs2, addr2label(addr)); }

  // +impl RV32::I::BNE ++RV64I
  // +impl RV32::C::C.BNEZ
  // +impl pseudo::bnez rs, offset (bne rs, x0, offset) Branch if ̸= zero
  void bne(const IntReg& rs1, const IntReg& rs2, const Label& label) {
    if (targetIs<RV32I>() || targetIs<RV64I>()) {
      st << [=](Strage& s) {
        int32 imm13 = static_cast<int32>(label.relAddr());
        // imm13は2の倍数で符号付13ビット整数で表現可能な値
        XKON_ASSERT(isAlignedN(imm13, 2) && isSintN(imm13, 12));

        bool done = false;
        if (targetIs<EXT_C>() && label.isNear() && rs1.isC() && rs2 == zero && (isSintN(imm13, 9) && isAlignedN(imm13, 2))) {
          done = true;

          const uint32 op = "3'b111"_c << (_8 | _4 - _3)[imm13] << rs1.CIdx() << (_7 - _6 | _2 - _1 | _5)[imm13] << "2'b01"_c;
          s.hword(op);
          s.desc(XKON_LAZY(s.format("oiL#iL") % "bnez" % rs1 % label % "c.bnez" % rs1 % label));
        }

        if (!done) {
          const uint32 op = (_12 | (_10 - _5))[imm13] << rs2.Idx() << rs1.Idx() << "3'b001"_c << ((_4 - _1) | _11)[imm13] << "7'b1100011"_c;
          s.word(op);
          if (rs2 == zero) {
            s.desc(XKON_LAZY(s.format("oiL") % "bnez" % rs1 % label));
          } else {
            s.desc(XKON_LAZY(s.format("oiiL") % "bne" % rs1 % rs2 % label));
          }
        }
      };
    } else {
      unsupported(__func__);
    }
  }
  void bne(const IntReg& rs1, const IntReg& rs2, const char* label) { bne(rs1, rs2, str2label(label)); }
  void bne(const IntReg& rs1, const IntReg& rs2, addr_t addr) { bne(rs1, rs2, addr2label(addr)); }

  // +impl RV32::I::BLT
  // +impl pseudo::bltz rs, offset (blt rs, x0, offset) Branch if < zero
  // +impl pseudo::bgtz rs, offset (blt x0, rs, offset) Branch if > zero
  // +impl pseudo::bgt rs, rt, offset (blt rt, rs, offset) Branch if >
  void blt(const IntReg& rs1, const IntReg& rs2, const Label& label) {
    if (targetIs<RV32I>()) {
      st << [=](Strage& s) {
        int32 imm13 = static_cast<int32>(label.relAddr());
        // imm13は2の倍数で符号付13ビット整数で表現可能な値
        XKON_ASSERT(isAlignedN(imm13, 2) && isSintN(imm13, 12));

        const uint32 op = (_12 | (_10 - _5))[imm13] << rs2.Idx() << rs1.Idx() << "3'b100"_c << ((_4 - _1) | _11)[imm13] << "7'b1100011"_c;

        s.word(op);
        // NOTE: bgt 疑似命令は blt 命令に変換される
        if (rs2 == zero) {
          s.desc(XKON_LAZY(s.format("oiL") % "bltz" % rs1 % label));
        } else if (rs1 == zero) {
          s.desc(XKON_LAZY(s.format("oiL") % "bgtz" % rs2 % label));
        } else {
          s.desc(XKON_LAZY(s.format("oiiL") % "blt" % rs1 % rs2 % label));
        }
      };
    } else {
      unsupported(__func__);
    }
  }
  void blt(const IntReg& rs1, const IntReg& rs2, const char* label) { blt(rs1, rs2, str2label(label)); }
  void blt(const IntReg& rs1, const IntReg& rs2, addr_t addr) { blt(rs1, rs2, addr2label(addr)); }

  // +impl RV32::I::BGE
  // +impl pseudo::blez rs, offset (bge x0, rs, offset) Branch if ≤ zero
  // +impl pseudo::bgez rs, offset (bge rs, x0, offset) Branch if ≥ zero
  // +impl pseudo::ble rs, rt, offset (bge rt, rs, offset) Branch if ≤
  void bge(const IntReg& rs1, const IntReg& rs2, const Label& label) {
    if (targetIs<RV32I>()) {
      st << [=](Strage& s) {
        int32 imm13 = static_cast<int32>(label.relAddr());
        // imm13は2の倍数で符号付13ビット整数で表現可能な値
        XKON_ASSERT(isAlignedN(imm13, 2) && isSintN(imm13, 12));

        const uint32 op = (_12 | (_10 - _5))[imm13] << rs2.Idx() << rs1.Idx() << "3'b101"_c << ((_4 - _1) | _11)[imm13] << "7'b1100011"_c;

        s.word(op);
        if (rs1 == zero) {
          s.desc(XKON_LAZY(s.format("oiL") % "blez" % rs2 % label));
        } else if (rs2 == zero) {
          s.desc(XKON_LAZY(s.format("oiL") % "bgez" % rs1 % label));
        } else {
          // NOTE: objdump では bge を ble 疑似命令として逆アセンブルする
          s.desc(XKON_LAZY(s.format("oiiL") % "ble" % rs2 % rs1 % label));
        }
      };
    } else {
      unsupported(__func__);
    }
  }
  void bge(const IntReg& rs1, const IntReg& rs2, const char* label) { bge(rs1, rs2, str2label(label)); }
  void bge(const IntReg& rs1, const IntReg& rs2, addr_t addr) { bge(rs1, rs2, addr2label(addr)); }

  // +impl RV32::I::BLTU ++RV64I
  // +impl pseudo::bgtu rs, rt, offset (bltu rt, rs, offset) Branch if >, unsigned
  void bltu(const IntReg& rs1, const IntReg& rs2, const Label& label) {
    if (targetIs<RV32I>() || targetIs<RV64I>()) {
      st << [=](Strage& s) {
        int32 imm13 = static_cast<int32>(label.relAddr());
        // imm13は2の倍数で符号付13ビット整数で表現可能な値
        XKON_ASSERT(isAlignedN(imm13, 2) && isSintN(imm13, 12));

        const uint32 op = (_12 | (_10 - _5))[imm13] << rs2.Idx() << rs1.Idx() << "3'b110"_c << ((_4 - _1) | _11)[imm13] << "7'b1100011"_c;
        s.word(op);
        // NOTE: bgtu 疑似命令は bltu 命令に変換される
        s.desc(XKON_LAZY(s.format("oiiL") % "bltu" % rs1 % rs2 % label));
      };
    } else {
      unsupported(__func__);
    }
  }
  void bltu(const IntReg& rs1, const IntReg& rs2, const char* label) { bltu(rs1, rs2, str2label(label)); }
  void bltu(const IntReg& rs1, const IntReg& rs2, addr_t addr) { bltu(rs1, rs2, addr2label(addr)); }

  // +impl RV32::I::BGEU ++RV64I
  // +impl pseudo::bleu rs, rt, offset (bgeu rt, rs, offset) Branch if ≤, unsigned
  void bgeu(const IntReg& rs1, const IntReg& rs2, const Label& label) {
    if (targetIs<RV32I>()|| targetIs<RV64I>()) {
      st << [=](Strage& s) {
        int32 imm13 = static_cast<int32>(label.relAddr());
        // imm13は2の倍数で符号付13ビット整数で表現可能な値
        XKON_ASSERT(isAlignedN(imm13, 2) && isSintN(imm13, 12));

        const uint32 op = (_12 | (_10 - _5))[imm13] << rs2.Idx() << rs1.Idx() << "3'b111"_c << ((_4 - _1) | _11)[imm13] << "7'b1100011"_c;
        s.word(op);
        // NOTE: objdump では bgeu を bleu 疑似命令として逆アセンブルする
        s.desc(XKON_LAZY(s.format("oiiL") % "bleu" % rs2 % rs1 % label));
      };
    } else {
      unsupported(__func__);
    }
  }
  void bgeu(const IntReg& rs1, const IntReg& rs2, const char* label) { bgeu(rs1, rs2, str2label(label)); }
  void bgeu(const IntReg& rs1, const IntReg& rs2, addr_t addr) { bgeu(rs1, rs2, addr2label(addr)); }

  // +impl RV32::I::LB
  void lb(const IntReg& rd, const IntOffsetReg& rs1) {
    if (targetIs<RV32I>()) {
      int32 imm12 = rs1.offset;
      XKON_ASSERT(isSintN(imm12, 12));

      const uint32 op = (_11 - _0)[imm12] << rs1.Idx() << "3'b000"_c << rd.Idx() << "7'b0000011"_c;
      st << [=](Strage& s) {
        s.word(op);
        s.desc(XKON_LAZY(s.format("oiI") % "lb" % rd % rs1));
      };
    } else {
      unsupported(__func__);
    }
  }

  // +impl RV32::I::LH
  void lh(const IntReg& rd, const IntOffsetReg& rs1) {
    if (targetIs<RV32I>()) {
      int32 imm12 = rs1.offset;
      XKON_ASSERT(isSintN(imm12, 12));

      const uint32 op = (_11 - _0)[imm12] << rs1.Idx() << "3'b001"_c << rd.Idx() << "7'b0000011"_c;
      st << [=](Strage& s) {
        s.word(op);
        s.desc(XKON_LAZY(s.format("oiI") % "lh" % rd % rs1));
      };
    } else {
      unsupported(__func__);
    }
  }

  // +impl RV32::I::LW
  // +impl RV32::C::C.LW
  // +impl RV32::C::C.LWSP
  void lw(const IntReg& rd, const IntOffsetReg& rs1) {
    if (targetIs<RV32I>()) {
      int32 imm12 = rs1.offset;
      XKON_ASSERT(isSintN(imm12, 12));

      bool done = false;

      if (targetIs<EXT_C>()) {
        if (rd.isC() && rs1.isC() && (isAlignedN(imm12, 4) && isUintN(imm12, 7))) {
          done = true;

          const uint32 uimm = imm12;
          const uint32 op = "3'b010"_c << (_5 - _3)[uimm] << rs1.CIdx() << (_2 | _6)[uimm] << rd.CIdx() << "2'b00"_c;

          st << [=](Strage& s) {
            s.hword(op);
            s.desc(XKON_LAZY(s.format("oiI#iI") % "lw" % rd % rs1 % "c.lw" % rd % rs1));
          };
        } else if (rd != zero && rs1 == sp && (isUintN(imm12, 8) && isAlignedN(imm12, 4))) {
          done = true;

          const uint32 op = "3'b010"_c << (_5)[imm12] << rd.Idx() << (_4 - _2 | _7 - _6)[imm12] << "2'b10"_c;

          st << [=](Strage& s) {
            s.hword(op);
            s.desc(XKON_LAZY(s.format("oiI#iI") % "lw" % rd % rs1 % "c.lwsp" % rd % rs1));
          };
        }
      }

      if (!done) {
        const uint32 op = (_11 - _0)[imm12] << rs1.Idx() << "3'b010"_c << rd.Idx() << "7'b0000011"_c;
        st << [=](Strage& s) {
          s.word(op);
          s.desc(XKON_LAZY(s.format("oiI") % "lw" % rd % rs1));
        };
      }
    } else {
      unsupported(__func__);
    }
  }

  // +impl RV32::I::LBU ++RV64I
  void lbu(const IntReg& rd, const IntOffsetReg& rs1) {
    if (targetIs<RV32I>()|| targetIs<RV64I>()) {
      int32 imm12 = rs1.offset;
      XKON_ASSERT(isSintN(imm12, 12));

      const uint32 op = (_11 - _0)[imm12] << rs1.Idx() << "3'b100"_c << rd.Idx() << "7'b0000011"_c;
      st << [=](Strage& s) {
        s.word(op);
        s.desc(XKON_LAZY(s.format("oiI") % "lbu" % rd % rs1));
      };
    } else {
      unsupported(__func__);
    }
  }

  // +impl RV32::I::LHU
  void lhu(const IntReg& rd, const IntOffsetReg& rs1) {
    if (targetIs<RV32I>()) {
      int32 imm12 = rs1.offset;
      XKON_ASSERT(isSintN(imm12, 12));

      const uint32 op = (_11 - _0)[imm12] << rs1.Idx() << "3'b101"_c << rd.Idx() << "7'b0000011"_c;
      st << [=](Strage& s) {
        s.word(op);
        s.desc(XKON_LAZY(s.format("oiI") % "lhu" % rd % rs1));
      };
    } else {
      unsupported(__func__);
    }
  }

  // +impl RV32::I::SB ++RV64I
  void sb(const IntReg& rs2, const IntOffsetReg& rs1) {
    if (targetIs<RV32I>()|| targetIs<RV64I>()) {
      int32 imm12 = rs1.offset;
      XKON_ASSERT(isSintN(imm12, 12));

      const uint32 op = (_11 - _5)[imm12] << rs2.Idx() << rs1.Idx() << "3'b000"_c << (_4 - _0)[imm12] << "7'b0100011"_c;
      st << [=](Strage& s) {
        s.word(op);
        s.desc(XKON_LAZY(s.format("oiI") % "sb" % rs2 % rs1));
      };
    } else {
      unsupported(__func__);
    }
  }

  // +impl RV32::I::SH
  void sh(const IntReg& rs2, const IntOffsetReg& rs1) {
    if (targetIs<RV32I>()) {
      int32 imm12 = rs1.offset;
      XKON_ASSERT(isSintN(imm12, 12));

      const uint32 op = (_11 - _5)[imm12] << rs2.Idx() << rs1.Idx() << "3'b001"_c << (_4 - _0)[imm12] << "7'b0100011"_c;
      st << [=](Strage& s) {
        s.word(op);
        s.desc(XKON_LAZY(s.format("oiI") % "sh" % rs2 % rs1));
      };
    } else {
      unsupported(__func__);
    }
  }

  // +impl RV32::I::SW
  // +impl RV32::C::C.SW
  // +impl RV32::C::C.SWSP
  void sw(const IntReg& rs2, const IntOffsetReg& rs1) {
    if (targetIs<RV32I>()) {
      int32 imm12 = rs1.offset;
      XKON_ASSERT(isSintN(imm12, 12));

      bool done = false;

      if (targetIs<EXT_C>()) {
        if (rs2.isC() && rs1.isC() && (isAlignedN(imm12, 4) && isUintN(imm12, 7))) {
          done = true;

          const uint32 uimm = imm12;
          const uint32 op = "3'b110"_c << (_5 - _3)[uimm] << rs1.CIdx() << (_2 | _6)[uimm] << rs2.CIdx() << "2'b00"_c;

          st << [=](Strage& s) {
            s.hword(op);
            s.desc(XKON_LAZY(s.format("oiI#iI") % "sw" % rs2 % rs1 % "c.sw" % rs2 % rs1));
          };
        } else if (rs1 == sp && (isUintN(imm12, 8) && isAlignedN(imm12, 4))) {
          done = true;

          const uint32 op = "3'b110"_c << (_5 - _2 | _7 - _6)[imm12] << rs2.Idx() << "2'b10"_c;

          st << [=](Strage& s) {
            s.hword(op);
            s.desc(XKON_LAZY(s.format("oiI#iI") % "sw" % rs2 % rs1 % "c.swsp" % rs2 % rs1));
          };
        }
      }

      if (!done) {
        const uint32 op = (_11 - _5)[imm12] << rs2.Idx() << rs1.Idx() << "3'b010"_c << (_4 - _0)[imm12] << "7'b0100011"_c;
        st << [=](Strage& s) {
          s.word(op);
          s.desc(XKON_LAZY(s.format("oiI") % "sw" % rs2 % rs1));
        };
      }
    } else {
      unsupported(__func__);
    }
  }

  // +impl RV32::I::ADDI ++RV64I
  // +impl RV32::C::C.ADDI4SPN/
  // +impl RV32::C::C.ADDI/
  // +impl RV32::C::C.ADDI16SP/
  // +impl RV32::C::C.NOP/
  // +impl RV32::C::C.LI
  // +impl pseudo::nop (addi x0, x0, 0) No operation
  // +impl pseudo::mv rd, rs (addi rd, rs, 0) Copy register
  // +impl pseudo::li rd, immediate (Myriad sequences) Load immediate
  void addi(const IntReg& rd, const IntReg& rs1, int32 imm12) {
    if (targetIs<RV32I>()|| targetIs<RV64I>()) {
      XKON_ASSERT(isSintN(imm12, 12));
      bool done = false;

      // 圧縮命令が生成できないか試す
      if (targetIs<EXT_C>()) {
        if (rd.isC() && rs1 == sp && (imm12 != 0 && isSintN(imm12, 10) && isAlignedN(imm12, 4))) {
          done = true;
          const uint32 nzuimm = imm12;

          const uint32 op = "3'b000"_c << (_5 - _4 | _9 - _6 | _2 | _3)[nzuimm] << rd.CIdx() << "2'b00"_c;

          st << [=](Strage& s) {
            s.hword(op);
            s.desc(XKON_LAZY(s.format("oiis#is") % "addi" % rd % rs1 % nzuimm % "c.addi4spn" % rd % nzuimm));
          };
        } else if ((rd != zero && rd == rs1) && (imm12 != 0 && isSintN(imm12, 6))) {
          done = true;
          const uint32 nzuimm = imm12;

          const uint32 op = "3'b000"_c << (_5)[nzuimm] << rd.Idx() << (_4 - _0)[nzuimm] << "2'b01"_c;
          st << [=](Strage& s) {
            s.hword(op);
            s.desc(XKON_LAZY(s.format("oiis#is") % "addi" % rd % rs1 % nzuimm % "c.addi" % rd % nzuimm));
          };
        } else if ((rd == sp && rd == rs1) && (imm12 != 0 && isSintN(imm12, 10) && isAlignedN(imm12, 16))) {
          done = true;
          const uint32 imm = imm12;

          const uint32 op = "3'b011"_c << (_9)[imm] << "5'b00010"_c << (_4 | _6 | _8 - _7 | _5)[imm] << "2'b01"_c;

          st << [=](Strage& s) {
            s.hword(op);
            s.desc(XKON_LAZY(s.format("oiis#s") % "addi" % rd % rs1 % imm % "c.addi16sp" % imm));
          };
        } else if (rd == zero && rs1 == zero && imm12 == 0) {
          done = true;

          const uint32 op = "3'b000"_c
                            << "3'b000"_c
                            << "3'b000"_c
                            << "2'b00"_c
                            << "3'b000"_c
                            << "2'b01"_c;

          st << [=](Strage& s) {
            s.hword(op);
            s.desc(XKON_LAZY(s.format("o#") % "nop" % "c.nop"));
          };
        } else if (rd != zero && rs1 == zero && isSintN(imm12, 6)) {
          done = true;

          const uint32 op = "3'b010"_c << (_5)[imm12] << rd.Idx() << (_4 - _0)[imm12] << "2'b01"_c;

          st << [=](Strage& s) {
            s.hword(op);
            s.desc(XKON_LAZY(s.format("ois#is") % "li" % rd % imm12 % "c.li" % rd % imm12));
          };
        }
      }

      // 圧縮命令に出来なかった場合は通常のオペコードを生成する
      if (!done) {
        const uint32 imm = signextend(static_cast<uint32>(imm12), 12);

        const uint32 op = (_11 - _0)[imm] << rs1.Idx() << "3'b000"_c << rd.Idx() << "7'b0010011"_c;

        st << [=](Strage& s) {
          s.word(op);
          if (rd == zero && rs1 == zero && imm12 == 0) {
            s.desc(XKON_LAZY(s.format("o") % "nop"));
          } else if (rs1 == zero) {
            s.desc(XKON_LAZY(s.format("ois") % "li" % rd % imm12));
          } else if (imm12 == 0) {
            s.desc(XKON_LAZY(s.format("oii") % "mv" % rd % rs1));
          } else {
            s.desc(XKON_LAZY(s.format("oiis") % "addi" % rd % rs1 % imm12));
          }
        };
      }
    } else {
      unsupported(__func__);
    }
  }

  // +impl RV32::I::SLTI
  void slti(const IntReg& rd, const IntReg& rs1, int32 imm12) {
    if (targetIs<RV32I>()) {
      XKON_ASSERT(isSintN(imm12, 12));

      const uint32 op = (_11 - _0)[imm12] << rs1.Idx() << "3'b010"_c << rd.Idx() << "7'b0010011"_c;

      st << [=](Strage& s) {
        s.word(op);
        s.desc(XKON_LAZY(s.format("oiis") % "slti" % rd % rs1 % imm12));
      };
    } else {
      unsupported(__func__);
    }
  }

  // +impl RV32::I::SLTIU
  // +impl pseudo::seqz rd, rs (sltiu rd, rs, 1) Set if = zero
  void sltiu(const IntReg& rd, const IntReg& rs1, int32 imm12) {
    if (targetIs<RV32I>()) {
      XKON_ASSERT(isSintN(imm12, 12));

      const uint32 op = (_11 - _0)[imm12] << rs1.Idx() << "3'b011"_c << rd.Idx() << "7'b0010011"_c;

      st << [=](Strage& s) {
        s.word(op);
        if (imm12 == 1) {
          s.desc(XKON_LAZY(s.format("oiis") % "seqz" % rd % rs1));
        } else {
          s.desc(XKON_LAZY(s.format("oiis") % "sltiu" % rd % rs1 % imm12));
        }
      };
    } else {
      unsupported(__func__);
    }
  }

  // +impl RV32::I::XORI
  // +impl pseudo::not rd, rs (xori rd, rs, -1) One’s complement
  void xori(const IntReg& rd, const IntReg& rs1, int32 imm12) {
    if (targetIs<RV32I>()) {
      XKON_ASSERT(isSintN(imm12, 12));

      const uint32 op = (_11 - _0)[imm12] << rs1.Idx() << "3'b100"_c << rd.Idx() << "7'b0010011"_c;

      st << [=](Strage& s) {
        s.word(op);
        if (imm12 == -1) {
          s.desc(XKON_LAZY(s.format("oii") % "not" % rd % rs1));
        } else {
          s.desc(XKON_LAZY(s.format("oiis") % "xori" % rd % rs1 % imm12));
        }
      };
    } else {
      unsupported(__func__);
    }
  }

  // +impl RV32::I::ORI
  void ori(const IntReg& rd, const IntReg& rs1, int32 imm12) {
    if (targetIs<RV32I>()) {
      XKON_ASSERT(isSintN(imm12, 12));

      const uint32 op = (_11 - _0)[imm12] << rs1.Idx() << "3'b110"_c << rd.Idx() << "7'b0010011"_c;

      st << [=](Strage& s) {
        s.word(op);
        s.desc(XKON_LAZY(s.format("oiis") % "ori" % rd % rs1 % imm12));
      };
    } else {
      unsupported(__func__);
    }
  }

  // +impl RV32::I::ANDI
  // +impl RV32::C::C.ANDI
  void andi(const IntReg& rd, const IntReg& rs1, int32 imm12) {
    if (targetIs<RV32I>()) {
      XKON_ASSERT(isSintN(imm12, 12));
      bool done = false;

      if (targetIs<EXT_C>() && rd == rs1 && rd.isC() && isSintN(imm12, 6)) {
        done = true;

        const uint32 op = "3'b100"_c << (_5)[imm12] << "2'b10"_c << rd.CIdx() << (_4 - _0)[imm12] << "2'b01"_c;

        st << [=](Strage& s) {
          s.hword(op);
          s.desc(XKON_LAZY(s.format("oiis#is") % "andi" % rd % rs1 % imm12 % "c.andi" % rd % imm12));
        };
      }

      if (!done) {
        const uint32 op = (_11 - _0)[imm12] << rs1.Idx() << "3'b111"_c << rd.Idx() << "7'b0010011"_c;

        st << [=](Strage& s) {
          s.word(op);
          s.desc(XKON_LAZY(s.format("oiis") % "andi" % rd % rs1 % imm12));
        };
      }
    } else {
      unsupported(__func__);
    }
  }

  // +impl RV32::I::SLLI
  // +impl RV32::C::C.SLLI
  void slli(const IntReg& rd, const IntReg& rs1, uint32 shamt) {
    XKON_ASSERT(isUintN(shamt, 6));
    if (targetIs<RV32I>()) {
      XKON_ASSERT(isUintN(shamt, 5));
      bool done = false;

      if (targetIs<EXT_C>() && rd != zero && rd == rs1 && shamt != 0) {
        done = true;

        const uint32 op = "3'b000"_c << (_5)[shamt] << rd.Idx() << (_4 - _0)[shamt] << "2'b10"_c;

        st << [=](Strage& s) {
          s.hword(op);
          s.desc(XKON_LAZY(s.format("oiiu#iu") % "slli" % rd % rs1 % shamt % "c.slli" % rd % shamt));
        };
      }

      if (!done) {
        const uint32 op = "5'b00000"_c
                          << "2'b00"_c << (_4 - _0)[shamt] << rs1.Idx() << "3'b001"_c << rd.Idx() << "7'b0010011"_c;

        st << [=](Strage& s) {
          s.word(op);
          s.desc(XKON_LAZY(s.format("oiiu") % "slli" % rd % rs1 % shamt));
        };
      }
    } else {
      unsupported(__func__);
    }
  }

  // +impl RV32::I::SRLI
  // +impl RV32::C::C.SRLI
  void srli(const IntReg& rd, const IntReg& rs1, uint32 shamt) {
    XKON_ASSERT(isUintN(shamt, 6));
    if (targetIs<RV32I>()) {
      XKON_ASSERT(isUintN(shamt, 5));
      bool done = false;

      if (targetIs<EXT_C>() && rd == rs1 && rd.isC() && shamt != 0) {
        done = true;

        const uint32 op = "3'b100"_c << (_5)[shamt] << "2'b00"_c << rd.CIdx() << (_4 - _0)[shamt] << "2'b01"_c;
        st << [=](Strage& s) {
          s.hword(op);
          s.desc(XKON_LAZY(s.format("oiiu#iu") % "srli" % rd % rs1 % shamt % "c.srli" % rd % shamt));
        };
      }

      if (!done) {
        const uint32 op = "5'b00000"_c
                          << "2'b00"_c << (_4 - _0)[shamt] << rs1.Idx() << "3'b101"_c << rd.Idx() << "7'b0010011"_c;

        st << [=](Strage& s) {
          s.word(op);
          s.desc(XKON_LAZY(s.format("oiiu") % "srli" % rd % rs1 % shamt));
        };
      }
    } else {
      unsupported(__func__);
    }
  }

  // +impl RV32::I::SRAI
  // +impl RV32::C::C.SRAI
  void srai(const IntReg& rd, const IntReg& rs1, uint32 shamt) {
    XKON_ASSERT(isUintN(shamt, 6));
    if (targetIs<RV32I>()) {
      XKON_ASSERT(isUintN(shamt, 5));
      bool done = false;

      if (targetIs<EXT_C>() && rd == rs1 && rd.isC() && shamt != 0) {
        done = true;

        const uint32 op = "3'b100"_c << (_5)[shamt] << "2'b01"_c << rd.CIdx() << (_4 - _0)[shamt] << "2'b01"_c;
        st << [=](Strage& s) {
          s.hword(op);
          s.desc(XKON_LAZY(s.format("oiiu#iu") % "srai" % rd % rs1 % shamt % "c.srai" % rd % shamt));
        };
      }

      if (!done) {
        const uint32 op = "5'b01000"_c
                          << "2'b00"_c << (_4 - _0)[shamt] << rs1.Idx() << "3'b101"_c << rd.Idx() << "7'b0010011"_c;

        st << [=](Strage& s) {
          s.word(op);
          s.desc(XKON_LAZY(s.format("oiiu") % "srai" % rd % rs1 % shamt));
        };
      }
    } else {
      unsupported(__func__);
    }
  }

  // +impl RV32::I::ADD ++RV64I
  // +impl RV32::C::C.MV
  // +impl RV32::C::C.ADD
  void add(const IntReg& rd, const IntReg& rs1, const IntReg& rs2) {
    if (targetIs<RV32I>() || targetIs<RV64I>()) {
      bool done = false;
      if (targetIs<EXT_C>()) {
        if (rd != zero && rs1 == zero && rs2 != zero) {
          done = true;

          const uint32 op = "3'b100"_c
                            << "1'b0"_c << rd.Idx() << rs2.Idx() << "2'b10"_c;
          st << [=](Strage& s) {
            s.hword(op);
            s.desc(XKON_LAZY(s.format("oii#ii") % "mv" % rd % rs2 % "c.mv" % rd % rs2));
          };
        } else if (rd != zero && rd == rs1 && rs2 != zero) {
          done = true;

          const uint32 op = "3'b100"_c
                            << "1'b1"_c << rd.Idx() << rs2.Idx() << "2'b10"_c;
          st << [=](Strage& s) {
            s.hword(op);
            s.desc(XKON_LAZY(s.format("oiii#ii") % "add" % rd % rs1 % rs2 % "c.add" % rd % rs2));
          };
        }
      }

      if (!done) {
        const uint32 op = "5'b00000"_c
                          << "2'b00"_c << rs2.Idx() << rs1.Idx() << "3'b000"_c << rd.Idx() << "7'b0110011"_c;

        st << [=](Strage& s) {
          s.word(op);
          s.desc(XKON_LAZY(s.format("oiii") % "add" % rd % rs1 % rs2));
        };
      }
    } else {
      unsupported(__func__);
    }
  }

  // +impl RV32::I::SUB ++RV64I
  // +impl RV32::C::C.SUB
  // +impl pseudo::neg rd, rs (sub rd, x0, rs) Two’s complement
  void sub(const IntReg& rd, const IntReg& rs1, const IntReg& rs2) {
    if (targetIs<RV32I>() || targetIs<RV64I>()) {
      bool done = false;

      if (targetIs<EXT_C>() && rd.isC() && rd == rs1 && rs2.isC()) {
        done = true;

        const uint32 op = "6'b100011"_c << rd.CIdx() << "2'b00"_c << rs2.CIdx() << "2'b01"_c;

        st << [=](Strage& s) {
          s.hword(op);
          s.desc(XKON_LAZY(s.format("oiii#ii") % "sub" % rd % rs1 % rs2 % "c.sub" % rd % rs2));
        };
      }

      if (!done) {
        const uint32 op = "5'b01000"_c
                          << "2'b00"_c << rs2.Idx() << rs1.Idx() << "3'b000"_c << rd.Idx() << "7'b0110011"_c;

        st << [=](Strage& s) {
          s.word(op);
          if (rs1 == zero) {
            s.desc(XKON_LAZY(s.format("oii") % "neg" % rd % rs2));
          } else {
            s.desc(XKON_LAZY(s.format("oiii") % "sub" % rd % rs1 % rs2));
          }
        };
      }
    } else {
      unsupported(__func__);
    }
  }

  // +impl RV32::I::SLL
  void sll(const IntReg& rd, const IntReg& rs1, const IntReg& rs2) {
    if (targetIs<RV32I>()) {
      const uint32 op = "5'b00000"_c
                        << "2'b00"_c << rs2.Idx() << rs1.Idx() << "3'b001"_c << rd.Idx() << "7'b0110011"_c;

      st << [=](Strage& s) {
        s.word(op);
        s.desc(XKON_LAZY(s.format("oiii") % "sll" % rd % rs1 % rs2));
      };
    } else {
      unsupported(__func__);
    }
  }

  // +impl RV32::I::SLT
  // +impl pseudo::sltz rd, rs (slt rd, rs, x0) Set if < zero
  // +impl pseudo::sgtz rd, rs (slt rd, x0, rs) Set if > zero
  void slt(const IntReg& rd, const IntReg& rs1, const IntReg& rs2) {
    if (targetIs<RV32I>()) {
      const uint32 op = "5'b00000"_c
                        << "2'b00"_c << rs2.Idx() << rs1.Idx() << "3'b010"_c << rd.Idx() << "7'b0110011"_c;

      st << [=](Strage& s) {
        s.word(op);
        if (rs2 == zero) {
          s.desc(XKON_LAZY(s.format("oiii") % "sltz" % rd % rs1));
        } else if (rs1 == zero) {
          s.desc(XKON_LAZY(s.format("oiii") % "sgtz" % rd % rs2));
        } else {
          s.desc(XKON_LAZY(s.format("oiii") % "slt" % rd % rs1 % rs2));
        }
      };
    } else {
      unsupported(__func__);
    }
  }

  // +impl RV32::I::SLTU
  // +impl pseudo::snez rd, rs (sltu rd, x0, rs) Set if ̸= zero
  void sltu(const IntReg& rd, const IntReg& rs1, const IntReg& rs2) {
    if (targetIs<RV32I>()) {
      const uint32 op = "5'b00000"_c
                        << "2'b00"_c << rs2.Idx() << rs1.Idx() << "3'b011"_c << rd.Idx() << "7'b0110011"_c;

      st << [=](Strage& s) {
        s.word(op);
        if (rs1 == zero) {
          s.desc(XKON_LAZY(s.format("oiii") % "snez" % rd % rs2));
        } else {
          s.desc(XKON_LAZY(s.format("oiii") % "sltu" % rd % rs1 % rs2));
        }
      };
    } else {
      unsupported(__func__);
    }
  }

  // +impl RV32::I::XOR
  // +impl RV32::C::C.XOR
  void xor_(const IntReg& rd, const IntReg& rs1, const IntReg& rs2) {
    if (targetIs<RV32I>()) {
      bool done = false;

      if (targetIs<EXT_C>() && rd.isC() && rd == rs1 && rs2.isC()) {
        done = true;

        const uint32 op = "6'b100011"_c << rd.CIdx() << "2'b01"_c << rs2.CIdx() << "2'b01"_c;

        st << [=](Strage& s) {
          s.hword(op);
          s.desc(XKON_LAZY(s.format("oiii#ii") % "xor" % rd % rs1 % rs2 % "c.xor" % rd % rs2));
        };
      }

      if (!done) {
        const uint32 op = "5'b00000"_c
                          << "2'b00"_c << rs2.Idx() << rs1.Idx() << "3'b100"_c << rd.Idx() << "7'b0110011"_c;

        st << [=](Strage& s) {
          s.word(op);
          s.desc(XKON_LAZY(s.format("oiii") % "xor" % rd % rs1 % rs2));
        };
      }
    } else {
      unsupported(__func__);
    }
  }

#if XKON_OPERATER_NAMES_ARE_USEABLE
  void XKON_INSN_NAME (xor)(const IntReg& rd, const IntReg& rs1, const IntReg& rs2) { xor_(rd, rs1, rs2); }
#endif

  // +impl RV32::I::SRL
  void srl(const IntReg& rd, const IntReg& rs1, const IntReg& rs2) {
    if (targetIs<RV32I>()) {
      const uint32 op = "5'b00000"_c
                        << "2'b00"_c << rs2.Idx() << rs1.Idx() << "3'b101"_c << rd.Idx() << "7'b0110011"_c;

      st << [=](Strage& s) {
        s.word(op);
        s.desc(XKON_LAZY(s.format("oiii") % "srl" % rd % rs1 % rs2));
      };
    } else {
      unsupported(__func__);
    }
  }

  // +impl RV32::I::SRA
  void sra(const IntReg& rd, const IntReg& rs1, const IntReg& rs2) {
    if (targetIs<RV32I>()) {
      const uint32 op = "5'b01000"_c
                        << "2'b00"_c << rs2.Idx() << rs1.Idx() << "3'b101"_c << rd.Idx() << "7'b0110011"_c;

      st << [=](Strage& s) {
        s.word(op);
        s.desc(XKON_LAZY(s.format("oiii") % "sra" % rd % rs1 % rs2));
      };
    } else {
      unsupported(__func__);
    }
  }

  // +impl RV32::I::OR
  // +impl RV32::C::C.OR
  void or_(const IntReg& rd, const IntReg& rs1, const IntReg& rs2) {
    if (targetIs<RV32I>()) {
      bool done = false;

      if (targetIs<EXT_C>() && rd.isC() && rd == rs1 && rs2.isC()) {
        done = true;

        const uint32 op = "6'b100011"_c << rd.CIdx() << "2'b10"_c << rs2.CIdx() << "2'b01"_c;

        st << [=](Strage& s) {
          s.hword(op);
          s.desc(XKON_LAZY(s.format("oiii#ii") % "or" % rd % rs1 % rs2 % "c.or" % rd % rs2));
        };
      }

      if (!done) {
        const uint32 op = "5'b00000"_c
                          << "2'b00"_c << rs2.Idx() << rs1.Idx() << "3'b110"_c << rd.Idx() << "7'b0110011"_c;

        st << [=](Strage& s) {
          s.word(op);
          s.desc(XKON_LAZY(s.format("oiii") % "or" % rd % rs1 % rs2));
        };
      }
    } else {
      unsupported(__func__);
    }
  }

#if XKON_OPERATER_NAMES_ARE_USEABLE
  void XKON_INSN_NAME(or)(const IntReg& rd, const IntReg& rs1, const IntReg& rs2) { or_(rd, rs1, rs2); }
#endif

  // +impl RV32::I::AND
  // +impl RV32::C::C.AND
  void and_(const IntReg& rd, const IntReg& rs1, const IntReg& rs2) {
    if (targetIs<RV32I>()) {
      bool done = false;

      if (targetIs<EXT_C>() && rd.isC() && rd == rs1 && rs2.isC()) {
        done = true;

        const uint32 op = "6'b100011"_c << rd.CIdx() << "2'b11"_c << rs2.CIdx() << "2'b01"_c;

        st << [=](Strage& s) {
          s.hword(op);
          s.desc(XKON_LAZY(s.format("oiii#ii") % "and" % rd % rs1 % rs2 % "c.and" % rd % rs2));
        };
      }

      if (!done) {
        const uint32 op = "5'b00000"_c
                          << "2'b00"_c << rs2.Idx() << rs1.Idx() << "3'b111"_c << rd.Idx() << "7'b0110011"_c;

        st << [=](Strage& s) {
          s.word(op);
          s.desc(XKON_LAZY(s.format("oiii") % "and" % rd % rs1 % rs2));
        };
      }
    } else {
      unsupported(__func__);
    }
  }

#if XKON_OPERATER_NAMES_ARE_USEABLE
  void XKON_INSN_NAME(and)(const IntReg& rd, const IntReg& rs1, const IntReg& rs2) { and_(rd, rs1, rs2); }
#endif

  // -impl RV32::I::FENCE
  // -impl RV32::I::ECALL
  // -impl RV32::I::EBREAK

  //////////////////////////////////////////////////////////////////////////////

  // !impl RV64::I::LWU
  void lwu(const IntReg& rd, const IntOffsetReg& rs1) {
    if (targetIs<RV64I>()) {
      const int32 imm12 = rs1.offset;
      XKON_ASSERT(isSintN(imm12, 12));

      const uint32 op = (_11 - _0)[imm12] << rs1.Idx() << "3'b110"_c << rd.Idx() << "7'b0000011"_c;

      st << [=](Strage& s) {
        s.word(op);
        s.desc(XKON_LAZY(s.format("oiI") % "lwu" % rd % rs1));
      };
    }
  }
  // !impl RV64::I::LD
  // !impl RV64::I::SD
  // !impl RV64::I::SLLI
  // !impl RV64::I::SRLI
  // !impl RV64::I::SRAI
  // !impl RV64::I::ADDIW
  // !impl RV64::I::SLLIW
  // !impl RV64::I::SRLIW
  // !impl RV64::I::SRAIW
  // !impl RV64::I::ADDW
  // !impl RV64::I::SUBW
  // !impl RV64::I::SLLW
  // !impl RV64::I::SRLW
  // !impl RV64::I::SRAW

  //////////////////////////////////////////////////////////////////////////////

  // +impl RV32::M::MUL
  void mul(const IntReg& rd, const IntReg& rs1, const IntReg& rs2) {
    if (targetIs<RV32I | EXT_M>()) {
      const uint32 op = "5'b00000"_c
                        << "2'b01"_c << rs2.Idx() << rs1.Idx() << "3'b000"_c << rd.Idx() << "7'b0110011"_c;

      st << [=](Strage& s) {
        s.word(op);
        s.desc(XKON_LAZY(s.format("oiii") % "mul" % rd % rs1 % rs2));
      };
    } else {
      unsupported(__func__);
    }
  }

  // +impl RV32::M::MULH
  void mulh(const IntReg& rd, const IntReg& rs1, const IntReg& rs2) {
    if (targetIs<RV32I | EXT_M>()) {
      const uint32 op = "5'b00000"_c
                        << "2'b01"_c << rs2.Idx() << rs1.Idx() << "3'b001"_c << rd.Idx() << "7'b0110011"_c;

      st << [=](Strage& s) {
        s.word(op);
        s.desc(XKON_LAZY(s.format("oiii") % "mulh" % rd % rs1 % rs2));
      };
    } else {
      unsupported(__func__);
    }
  }

  // +impl RV32::M::MULHSU
  void mulhsu(const IntReg& rd, const IntReg& rs1, const IntReg& rs2) {
    if (targetIs<RV32I | EXT_M>()) {
      const uint32 op = "5'b00000"_c
                        << "2'b01"_c << rs2.Idx() << rs1.Idx() << "3'b010"_c << rd.Idx() << "7'b0110011"_c;

      st << [=](Strage& s) {
        s.word(op);
        s.desc(XKON_LAZY(s.format("oiii") % "mulhsu" % rd % rs1 % rs2));
      };
    } else {
      unsupported(__func__);
    }
  }

  // +impl RV32::M::MULHU
  void mulhu(const IntReg& rd, const IntReg& rs1, const IntReg& rs2) {
    if (targetIs<RV32I | EXT_M>()) {
      const uint32 op = "5'b00000"_c
                        << "2'b01"_c << rs2.Idx() << rs1.Idx() << "3'b011"_c << rd.Idx() << "7'b0110011"_c;

      st << [=](Strage& s) {
        s.word(op);
        s.desc(XKON_LAZY(s.format("oiii") % "mulhu" % rd % rs1 % rs2));
      };
    } else {
      unsupported(__func__);
    }
  }

  // +impl RV32::M::DIV
  void div(const IntReg& rd, const IntReg& rs1, const IntReg& rs2) {
    if (targetIs<RV32I | EXT_M>()) {
      const uint32 op = "5'b00000"_c
                        << "2'b01"_c << rs2.Idx() << rs1.Idx() << "3'b100"_c << rd.Idx() << "7'b0110011"_c;

      st << [=](Strage& s) {
        s.word(op);
        s.desc(XKON_LAZY(s.format("oiii") % "div" % rd % rs1 % rs2));
      };
    } else {
      unsupported(__func__);
    }
  }

  // +impl RV32::M::DIVU
  void divu(const IntReg& rd, const IntReg& rs1, const IntReg& rs2) {
    if (targetIs<RV32I | EXT_M>()) {
      const uint32 op = "5'b00000"_c
                        << "2'b01"_c << rs2.Idx() << rs1.Idx() << "3'b101"_c << rd.Idx() << "7'b0110011"_c;

      st << [=](Strage& s) {
        s.word(op);
        s.desc(XKON_LAZY(s.format("oiii") % "divu" % rd % rs1 % rs2));
      };
    } else {
      unsupported(__func__);
    }
  }

  // +impl RV32::M::REM
  void rem(const IntReg& rd, const IntReg& rs1, const IntReg& rs2) {
    if (targetIs<RV32I | EXT_M>()) {
      const uint32 op = "5'b00000"_c
                        << "2'b01"_c << rs2.Idx() << rs1.Idx() << "3'b110"_c << rd.Idx() << "7'b0110011"_c;

      st << [=](Strage& s) {
        s.word(op);
        s.desc(XKON_LAZY(s.format("oiii") % "rem" % rd % rs1 % rs2));
      };
    } else {
      unsupported(__func__);
    }
  }

  // +impl RV32::M::REMU
  void remu(const IntReg& rd, const IntReg& rs1, const IntReg& rs2) {
    if (targetIs<RV32I | EXT_M>()) {
      const uint32 op = "5'b00000"_c
                        << "2'b01"_c << rs2.Idx() << rs1.Idx() << "3'b111"_c << rd.Idx() << "7'b0110011"_c;

      st << [=](Strage& s) {
        s.word(op);
        s.desc(XKON_LAZY(s.format("oiii") % "remu" % rd % rs1 % rs2));
      };
    } else {
      unsupported(__func__);
    }
  }

  //////////////////////////////////////////////////////////////////////////////

  // +impl RV32::A::LR.W
  void lr_w(const IntReg& rd, const IntOffsetReg& rs1) {
    XKON_ASSERT(rs1.offset == 0);
    if (targetIs<RV32I | EXT_A>()) {
      const uint32 op = "5'b00010"_c
                        << "2'b00"_c
                        << "5'b00000"_c << rs1.Idx() << "3'b010"_c << rd.Idx() << "7'b0101111"_c;
      st << [=](Strage& s) {
        s.word(op);
        s.desc(XKON_LAZY(s.format("oiM") % "lr.w" % rd % rs1));
      };
    } else {
      unsupported(__func__);
    }
  }

  // +impl RV32::A::SC.W
  void sc_w(const IntReg& rd, const IntReg& rs2, const IntOffsetReg& rs1) {
    XKON_ASSERT(rs1.offset == 0);
    if (targetIs<RV32I | EXT_A>()) {
      const uint32 op = "5'b00011"_c
                        << "2'b00"_c << rs2.Idx() << rs1.Idx() << "3'b010"_c << rd.Idx() << "7'b0101111"_c;
      st << [=](Strage& s) {
        s.word(op);
        s.desc(XKON_LAZY(s.format("oiiM") % "sc.w" % rd % rs2 % rs1));
      };
    } else {
      unsupported(__func__);
    }
  }

  // +impl RV32::A::AMOSWAP.W
  void amoswap_w(const IntReg& rd, const IntReg& rs2, const IntOffsetReg& rs1) {
    XKON_ASSERT(rs1.offset == 0);
    if (targetIs<RV32I | EXT_A>()) {
      const uint32 op = "5'b00001"_c
                        << "2'b00"_c << rs2.Idx() << rs1.Idx() << "3'b010"_c << rd.Idx() << "7'b0101111"_c;
      st << [=](Strage& s) {
        s.word(op);
        s.desc(XKON_LAZY(s.format("oiiM") % "amoswap.w" % rd % rs2 % rs1));
      };
    } else {
      unsupported(__func__);
    }
  }

  // +impl RV32::A::AMOADD.W
  void amoadd_w(const IntReg& rd, const IntReg& rs2, const IntOffsetReg& rs1) {
    XKON_ASSERT(rs1.offset == 0);
    if (targetIs<RV32I | EXT_A>()) {
      const uint32 op = "5'b00000"_c
                        << "2'b00"_c << rs2.Idx() << rs1.Idx() << "3'b010"_c << rd.Idx() << "7'b0101111"_c;
      st << [=](Strage& s) {
        s.word(op);
        s.desc(XKON_LAZY(s.format("oiiM") % "amoadd.w" % rd % rs2 % rs1));
      };
    } else {
      unsupported(__func__);
    }
  }

  // +impl RV32::A::AMOXOR.W
  void amoxor_w(const IntReg& rd, const IntReg& rs2, const IntOffsetReg& rs1) {
    XKON_ASSERT(rs1.offset == 0);
    if (targetIs<RV32I | EXT_A>()) {
      const uint32 op = "5'b00100"_c
                        << "2'b00"_c << rs2.Idx() << rs1.Idx() << "3'b010"_c << rd.Idx() << "7'b0101111"_c;
      st << [=](Strage& s) {
        s.word(op);
        s.desc(XKON_LAZY(s.format("oiiM") % "amoxor.w" % rd % rs2 % rs1));
      };
    } else {
      unsupported(__func__);
    }
  }

  // +impl RV32::A::AMOAND.W
  void amoand_w(const IntReg& rd, const IntReg& rs2, const IntOffsetReg& rs1) {
    XKON_ASSERT(rs1.offset == 0);
    if (targetIs<RV32I | EXT_A>()) {
      const uint32 op = "5'b01100"_c
                        << "2'b00"_c << rs2.Idx() << rs1.Idx() << "3'b010"_c << rd.Idx() << "7'b0101111"_c;
      st << [=](Strage& s) {
        s.word(op);
        s.desc(XKON_LAZY(s.format("oiiM") % "amoand.w" % rd % rs2 % rs1));
      };
    } else {
      unsupported(__func__);
    }
  }

  // +impl RV32::A::AMOOR.W
  void amoor_w(const IntReg& rd, const IntReg& rs2, const IntOffsetReg& rs1) {
    XKON_ASSERT(rs1.offset == 0);
    if (targetIs<RV32I | EXT_A>()) {
      const uint32 op = "5'b01000"_c
                        << "2'b00"_c << rs2.Idx() << rs1.Idx() << "3'b010"_c << rd.Idx() << "7'b0101111"_c;
      st << [=](Strage& s) {
        s.word(op);
        s.desc(XKON_LAZY(s.format("oiiM") % "amoor.w" % rd % rs2 % rs1));
      };
    } else {
      unsupported(__func__);
    }
  }

  // +impl RV32::A::AMOMIN.W
  void amomin_w(const IntReg& rd, const IntReg& rs2, const IntOffsetReg& rs1) {
    XKON_ASSERT(rs1.offset == 0);
    if (targetIs<RV32I | EXT_A>()) {
      const uint32 op = "5'b10000"_c
                        << "2'b00"_c << rs2.Idx() << rs1.Idx() << "3'b010"_c << rd.Idx() << "7'b0101111"_c;
      st << [=](Strage& s) {
        s.word(op);
        s.desc(XKON_LAZY(s.format("oiiM") % "amomin.w" % rd % rs2 % rs1));
      };
    } else {
      unsupported(__func__);
    }
  }

  // +impl RV32::A::AMOMAX.W
  void amomax_w(const IntReg& rd, const IntReg& rs2, const IntOffsetReg& rs1) {
    XKON_ASSERT(rs1.offset == 0);
    if (targetIs<RV32I | EXT_A>()) {
      const uint32 op = "5'b10100"_c
                        << "2'b00"_c << rs2.Idx() << rs1.Idx() << "3'b010"_c << rd.Idx() << "7'b0101111"_c;
      st << [=](Strage& s) {
        s.word(op);
        s.desc(XKON_LAZY(s.format("oiiM") % "amomax.w" % rd % rs2 % rs1));
      };
    } else {
      unsupported(__func__);
    }
  }

  // +impl RV32::A::AMOMINU.W
  void amominu_w(const IntReg& rd, const IntReg& rs2, const IntOffsetReg& rs1) {
    XKON_ASSERT(rs1.offset == 0);
    if (targetIs<RV32I | EXT_A>()) {
      const uint32 op = "5'b11000"_c
                        << "2'b00"_c << rs2.Idx() << rs1.Idx() << "3'b010"_c << rd.Idx() << "7'b0101111"_c;
      st << [=](Strage& s) {
        s.word(op);
        s.desc(XKON_LAZY(s.format("oiiM") % "amominu.w" % rd % rs2 % rs1));
      };
    } else {
      unsupported(__func__);
    }
  }

  // +impl RV32::A::AMOMAXU.W
  void amomaxu_w(const IntReg& rd, const IntReg& rs2, const IntOffsetReg& rs1) {
    XKON_ASSERT(rs1.offset == 0);
    if (targetIs<RV32I | EXT_A>()) {
      const uint32 op = "5'b11100"_c
                        << "2'b00"_c << rs2.Idx() << rs1.Idx() << "3'b010"_c << rd.Idx() << "7'b0101111"_c;
      st << [=](Strage& s) {
        s.word(op);
        s.desc(XKON_LAZY(s.format("oiiM") % "amomaxu.w" % rd % rs2 % rs1));
      };
    } else {
      unsupported(__func__);
    }
  }

  //////////////////////////////////////////////////////////////////////////////

  // +impl RV32::F::FLW
  // +impl RV32::C::C.FLW
  // +impl RV32::C::C.FLWSP
  void flw(const FpReg& rd, const IntOffsetReg& rs1) {
    const int32 imm12 = rs1.offset;
    XKON_ASSERT(isSintN(imm12, 12));
    if (targetIs<RV32I | EXT_F>()) {
      bool done = false;

      if (targetIs<EXT_C>()) {
        if (rd.isC() && rs1.isC() && (isUintN(imm12, 7) && isAlignedN(imm12, 4))) {
          done = true;

          const uint32 op = "3'b011"_c << (_5 - _3)[imm12] << rs1.CIdx() << (_2 | _6)[imm12] << rd.CIdx() << "2'b00"_c;

          st << [=](Strage& s) {
            s.hword(op);
            s.desc(XKON_LAZY(s.format("ofI#fI") % "flw" % rd % rs1 % "c.flw" % rd % rs1));
          };
        } else if (rs1 == sp && (isUintN(imm12, 8) && isAlignedN(imm12, 4))) {
          done = true;

          const uint32 op = "3'b011"_c << (_5)[imm12] << rd.Idx() << (_4 - _2 | _7 - _6)[imm12] << "2'b10"_c;

          st << [=](Strage& s) {
            s.hword(op);
            s.desc(XKON_LAZY(s.format("ofI#fI") % "flw" % rd % rs1 % "c.flwsp" % rd % rs1));
          };
        }
      }

      if (!done) {
        const uint32 op = (_11 - _0)[imm12] << rs1.Idx() << "3'b010"_c << rd.Idx() << "7'b0000111"_c;

        st << [=](Strage& s) {
          s.word(op);
          s.desc(XKON_LAZY(s.format("ofI") % "flw" % rd % rs1));
        };
      }
    } else {
      unsupported(__func__);
    }
  }

  // +impl RV32::F::FSW
  // +impl RV32::C::C.FSW
  // +impl RV32::C::C.FSWSP
  void fsw(const FpReg& rs2, const IntOffsetReg& rs1) {
    const int32 imm12 = rs1.offset;
    XKON_ASSERT(isSintN(imm12, 12));
    if (targetIs<RV32I | EXT_F>()) {
      bool done = false;

      if (targetIs<EXT_C>()) {
        if (rs2.isC() && rs1.isC() && (isUintN(imm12, 7) && isAlignedN(imm12, 4))) {
          done = true;

          const uint32 op = "3'b111"_c << (_5 - _3)[imm12] << rs1.CIdx() << (_2 | _6)[imm12] << rs2.CIdx() << "2'b00"_c;

          st << [=](Strage& s) {
            s.hword(op);
            s.desc(XKON_LAZY(s.format("ofI#fI") % "fsw" % rs2 % rs1 % "c.fsw" % rs2 % rs1));
          };
        } else if (rs1 == sp && (isUintN(imm12, 8) && isAlignedN(imm12, 4))) {
          done = true;

          const uint32 op = "3'b111"_c << (_5 - _2 | _7 - _6)[imm12] << rs2.Idx() << "2'b10"_c;

          st << [=](Strage& s) {
            s.hword(op);
            s.desc(XKON_LAZY(s.format("ofI#fI") % "fsw" % rs2 % rs1 % "c.fswsp" % rs2 % rs1));
          };
        }
      }

      if (!done) {
        const uint32 op = (_11 - _5)[imm12] << rs2.Idx() << rs1.Idx() << "3'b010"_c << (_4 - _0)[imm12] << "7'b0100111"_c;

        st << [=](Strage& s) {
          s.word(op);
          s.desc(XKON_LAZY(s.format("ofI") % "fsw" % rs2 % rs1));
        };
      }
    } else {
      unsupported(__func__);
    }
  }

  // +impl RV32::F::FMADD.S
  void fmadd_s(const FpReg& rd, const FpReg& rs1, const FpReg& rs2, const FpReg& rs3, RoundingMode rm = RoundingMode::dyn) {
    if (targetIs<RV32I | EXT_F>()) {
      const uint32 op = rs3.Idx() << "2'b00"_c << rs2.Idx() << rs1.Idx() << from(rm) << rd.Idx() << "7'b1000011"_c;

      st << [=](Strage& s) {
        s.word(op);
        s.desc(XKON_LAZY(s.format("offffr") % "fmadd.s" % rd % rs1 % rs2 % rs3 % rm));
      };
    } else {
      unsupported(__func__);
    }
  }

  // +impl RV32::F::FMSUB.S
  void fmsub_s(const FpReg& rd, const FpReg& rs1, const FpReg& rs2, const FpReg& rs3, RoundingMode rm = RoundingMode::dyn) {
    if (targetIs<RV32I | EXT_F>()) {
      const uint32 op = rs3.Idx() << "2'b00"_c << rs2.Idx() << rs1.Idx() << from(rm) << rd.Idx() << "7'b1000111"_c;

      st << [=](Strage& s) {
        s.word(op);
        s.desc(XKON_LAZY(s.format("offffr") % "fmsub.s" % rd % rs1 % rs2 % rs3 % rm));
      };
    } else {
      unsupported(__func__);
    }
  }

  // +impl RV32::F::FNMSUB.S
  void fnmsub_s(const FpReg& rd, const FpReg& rs1, const FpReg& rs2, const FpReg& rs3, RoundingMode rm = RoundingMode::dyn) {
    if (targetIs<RV32I | EXT_F>()) {
      const uint32 op = rs3.Idx() << "2'b00"_c << rs2.Idx() << rs1.Idx() << from(rm) << rd.Idx() << "7'b1001011"_c;

      st << [=](Strage& s) {
        s.word(op);
        s.desc(XKON_LAZY(s.format("offffr") % "fnmsub.s" % rd % rs1 % rs2 % rs3 % rm));
      };
    } else {
      unsupported(__func__);
    }
  }

  // +impl RV32::F::FNMADD.S
  void fnmadd_s(const FpReg& rd, const FpReg& rs1, const FpReg& rs2, const FpReg& rs3, RoundingMode rm = RoundingMode::dyn) {
    if (targetIs<RV32I | EXT_F>()) {
      const uint32 op = rs3.Idx() << "2'b00"_c << rs2.Idx() << rs1.Idx() << from(rm) << rd.Idx() << "7'b1001111"_c;

      st << [=](Strage& s) {
        s.word(op);
        s.desc(XKON_LAZY(s.format("offffr") % "fnmadd.s" % rd % rs1 % rs2 % rs3 % rm));
      };
    } else {
      unsupported(__func__);
    }
  }

  // +impl RV32::F::FADD.S
  void fadd_s(const FpReg& rd, const FpReg& rs1, const FpReg& rs2, RoundingMode rm = RoundingMode::dyn) {
    if (targetIs<RV32I | EXT_F>()) {
      const uint32 op = "5'b00000"_c
                        << "2'b00"_c << rs2.Idx() << rs1.Idx() << from(rm) << rd.Idx() << "7'b1010011"_c;

      st << [=](Strage& s) {
        s.word(op);
        s.desc(XKON_LAZY(s.format("offfr") % "fadd.s" % rd % rs1 % rs2 % rm));
      };
    } else {
      unsupported(__func__);
    }
  }

  // +impl RV32::F::FSUB.S
  void fsub_s(const FpReg& rd, const FpReg& rs1, const FpReg& rs2, RoundingMode rm = RoundingMode::dyn) {
    if (targetIs<RV32I | EXT_F>()) {
      const uint32 op = "5'b00001"_c
                        << "2'b00"_c << rs2.Idx() << rs1.Idx() << from(rm) << rd.Idx() << "7'b1010011"_c;

      st << [=](Strage& s) {
        s.word(op);
        s.desc(XKON_LAZY(s.format("offfr") % "fsub.s" % rd % rs1 % rs2 % rm));
      };
    } else {
      unsupported(__func__);
    }
  }

  // +impl RV32::F::FMUL.S
  void fmul_s(const FpReg& rd, const FpReg& rs1, const FpReg& rs2, RoundingMode rm = RoundingMode::dyn) {
    if (targetIs<RV32I | EXT_F>()) {
      const uint32 op = "5'b00010"_c
                        << "2'b00"_c << rs2.Idx() << rs1.Idx() << from(rm) << rd.Idx() << "7'b1010011"_c;

      st << [=](Strage& s) {
        s.word(op);
        s.desc(XKON_LAZY(s.format("offfr") % "fmul.s" % rd % rs1 % rs2 % rm));
      };
    } else {
      unsupported(__func__);
    }
  }

  // +impl RV32::F::FDIV.S
  void fdiv_s(const FpReg& rd, const FpReg& rs1, const FpReg& rs2, RoundingMode rm = RoundingMode::dyn) {
    if (targetIs<RV32I | EXT_F>()) {
      const uint32 op = "5'b00011"_c
                        << "2'b00"_c << rs2.Idx() << rs1.Idx() << from(rm) << rd.Idx() << "7'b1010011"_c;

      st << [=](Strage& s) {
        s.word(op);
        s.desc(XKON_LAZY(s.format("offfr") % "fdiv.s" % rd % rs1 % rs2 % rm));
      };
    } else {
      unsupported(__func__);
    }
  }

  // +impl RV32::F::FSQRT.S
  void fsqrt_s(const FpReg& rd, const FpReg& rs1, RoundingMode rm = RoundingMode::dyn) {
    if (targetIs<RV32I | EXT_F>()) {
      const uint32 op = "5'b01011"_c
                        << "2'b00"_c
                        << "5'b00000"_c << rs1.Idx() << from(rm) << rd.Idx() << "7'b1010011"_c;

      st << [=](Strage& s) {
        s.word(op);
        s.desc(XKON_LAZY(s.format("offr") % "fsqrt.s" % rd % rs1 % rm));
      };
    } else {
      unsupported(__func__);
    }
  }

  // +impl RV32::F::FSGNJ.S
  // +impl pseudo::fmv.s rd, rs (fsgnj.s rd, rs, rs) Copy single-precision register
  void fsgnj_s(const FpReg& rd, const FpReg& rs1, const FpReg& rs2) {
    if (targetIs<RV32I | EXT_F>()) {
      const uint32 op = "5'b00100"_c
                        << "2'b00"_c << rs2.Idx() << rs1.Idx() << "3'b000"_c << rd.Idx() << "7'b1010011"_c;

      st << [=](Strage& s) {
        s.word(op);
        if (rs1 == rs2) {
          s.desc(XKON_LAZY(s.format("offf") % "fmv.s" % rd % rs1));
        } else {
          s.desc(XKON_LAZY(s.format("offf") % "fsgnj.s" % rd % rs1 % rs2));
        }
      };
    } else {
      unsupported(__func__);
    }
  }

  // +impl RV32::F::FSGNJN.S
  // +impl pseudo::fneg.s rd, rs (fsgnjn.s rd, rs, rs) Single-precision negate
  void fsgnjn_s(const FpReg& rd, const FpReg& rs1, const FpReg& rs2) {
    if (targetIs<RV32I | EXT_F>()) {
      const uint32 op = "5'b00100"_c
                        << "2'b00"_c << rs2.Idx() << rs1.Idx() << "3'b001"_c << rd.Idx() << "7'b1010011"_c;

      st << [=](Strage& s) {
        s.word(op);
        if (rs1 == rs2) {
          s.desc(XKON_LAZY(s.format("off") % "fneg.s" % rd % rs1));
        } else {
          s.desc(XKON_LAZY(s.format("offf") % "fsgnjn.s" % rd % rs1 % rs2));
        }
      };
    } else {
      unsupported(__func__);
    }
  }

  // +impl RV32::F::FSGNJX.S
  // +impl pseudo::fabs.s rd, rs (fsgnjx.s rd, rs, rs) Single-precision absolute value
  void fsgnjx_s(const FpReg& rd, const FpReg& rs1, const FpReg& rs2) {
    if (targetIs<RV32I | EXT_F>()) {
      const uint32 op = "5'b00100"_c
                        << "2'b00"_c << rs2.Idx() << rs1.Idx() << "3'b010"_c << rd.Idx() << "7'b1010011"_c;

      st << [=](Strage& s) {
        s.word(op);
        if (rs1 == rs2) {
          s.desc(XKON_LAZY(s.format("off") % "fabs.s" % rd % rs1));
        } else {
          s.desc(XKON_LAZY(s.format("offf") % "fsgnjx.s" % rd % rs1 % rs2));
        }
      };
    } else {
      unsupported(__func__);
    }
  }

  // +impl RV32::F::FMIN.S
  void fmin_s(const FpReg& rd, const FpReg& rs1, const FpReg& rs2) {
    if (targetIs<RV32I | EXT_F>()) {
      const uint32 op = "5'b00101"_c
                        << "2'b00"_c << rs2.Idx() << rs1.Idx() << "3'b000"_c << rd.Idx() << "7'b1010011"_c;

      st << [=](Strage& s) {
        s.word(op);
        s.desc(XKON_LAZY(s.format("offf") % "fmin.s" % rd % rs1 % rs2));
      };
    } else {
      unsupported(__func__);
    }
  }

  // +impl RV32::F::FMAX.S
  void fmax_s(const FpReg& rd, const FpReg& rs1, const FpReg& rs2) {
    if (targetIs<RV32I | EXT_F>()) {
      const uint32 op = "5'b00101"_c
                        << "2'b00"_c << rs2.Idx() << rs1.Idx() << "3'b001"_c << rd.Idx() << "7'b1010011"_c;

      st << [=](Strage& s) {
        s.word(op);
        s.desc(XKON_LAZY(s.format("offf") % "fmax.s" % rd % rs1 % rs2));
      };
    } else {
      unsupported(__func__);
    }
  }

  // +impl RV32::F::FCVT.W.S
  void fcvt_w_s(const IntReg& rd, const FpReg& rs1, RoundingMode rm = RoundingMode::dyn) {
    if (targetIs<RV32I | EXT_F>()) {
      const uint32 op = "5'b11000"_c
                        << "2'b00"_c
                        << "5'b00000"_c << rs1.Idx() << from(rm) << rd.Idx() << "7'b1010011"_c;

      st << [=](Strage& s) {
        s.word(op);
        s.desc(XKON_LAZY(s.format("oifr") % "fcvt.w.s" % rd % rs1 % rm));
      };
    } else {
      unsupported(__func__);
    }
  }

  // +impl RV32::F::FCVT.WU.S
  void fcvt_wu_s(const IntReg& rd, const FpReg& rs1, RoundingMode rm = RoundingMode::dyn) {
    if (targetIs<RV32I | EXT_F>()) {
      const uint32 op = "5'b11000"_c
                        << "2'b00"_c
                        << "5'b00001"_c << rs1.Idx() << from(rm) << rd.Idx() << "7'b1010011"_c;

      st << [=](Strage& s) {
        s.word(op);
        s.desc(XKON_LAZY(s.format("oifr") % "fcvt.wu.s" % rd % rs1 % rm));
      };
    } else {
      unsupported(__func__);
    }
  }

  // +impl RV32::F::FMV.X.W
  void fmv_x_w(const IntReg& rd, const FpReg& rs1) {
    if (targetIs<RV32I | EXT_F>()) {
      const uint32 op = "5'b11100"_c
                        << "2'b00"_c
                        << "5'b00000"_c << rs1.Idx() << "3'b000"_c << rd.Idx() << "7'b1010011"_c;

      st << [=](Strage& s) {
        s.word(op);
        s.desc(XKON_LAZY(s.format("oif") % "fmv.x.w" % rd % rs1));
      };
    } else {
      unsupported(__func__);
    }
  }

  // +impl RV32::F::FEQ.S
  void feq_s(const IntReg& rd, const FpReg& rs1, const FpReg& rs2) {
    if (targetIs<RV32I | EXT_F>()) {
      const uint32 op = "5'b10100"_c
                        << "2'b00"_c << rs2.Idx() << rs1.Idx() << "3'b010"_c << rd.Idx() << "7'b1010011"_c;
      st << [=](Strage& s) {
        s.word(op);
        s.desc(XKON_LAZY(s.format("oiff") % "feq.s" % rd % rs1 % rs2));
      };
    } else {
      unsupported(__func__);
    }
  }

  // +impl RV32::F::FLT.S
  void flt_s(const IntReg& rd, const FpReg& rs1, const FpReg& rs2) {
    if (targetIs<RV32I | EXT_F>()) {
      const uint32 op = "5'b10100"_c
                        << "2'b00"_c << rs2.Idx() << rs1.Idx() << "3'b001"_c << rd.Idx() << "7'b1010011"_c;
      st << [=](Strage& s) {
        s.word(op);
        s.desc(XKON_LAZY(s.format("oiff") % "flt.s" % rd % rs1 % rs2));
      };
    } else {
      unsupported(__func__);
    }
  }

  // +impl RV32::F::FLE.S
  void fle_s(const IntReg& rd, const FpReg& rs1, const FpReg& rs2) {
    if (targetIs<RV32I | EXT_F>()) {
      const uint32 op = "5'b10100"_c
                        << "2'b00"_c << rs2.Idx() << rs1.Idx() << "3'b000"_c << rd.Idx() << "7'b1010011"_c;
      st << [=](Strage& s) {
        s.word(op);
        s.desc(XKON_LAZY(s.format("oiff") % "fle.s" % rd % rs1 % rs2));
      };
    } else {
      unsupported(__func__);
    }
  }

  // +impl RV32::F::FCLASS.S
  void fclass_s(const IntReg& rd, const FpReg& rs1) {
    if (targetIs<RV32I | EXT_F>()) {
      const uint32 op = "5'b11100"_c
                        << "2'b00"_c
                        << "5'b00000"_c << rs1.Idx() << "3'b001"_c << rd.Idx() << "7'b1010011"_c;

      st << [=](Strage& s) {
        s.word(op);
        s.desc(XKON_LAZY(s.format("oif") % "fclass.s" % rd % rs1));
      };
    } else {
      unsupported(__func__);
    }
  }

  // +impl RV32::F::FCVT.S.W
  void fcvt_s_w(const FpReg& rd, const IntReg& rs1, RoundingMode rm = RoundingMode::dyn) {
    if (targetIs<RV32I | EXT_F>()) {
      const uint32 op = "5'b11010"_c
                        << "2'b00"_c
                        << "5'b00000"_c << rs1.Idx() << from(rm) << rd.Idx() << "7'b1010011"_c;

      st << [=](Strage& s) {
        s.word(op);
        s.desc(XKON_LAZY(s.format("ofir") % "fcvt.s.w" % rd % rs1 % rm));
      };
    } else {
      unsupported(__func__);
    }
  }

  // +impl RV32::F::FCVT.S.WU
  void fcvt_s_wu(const FpReg& rd, const IntReg& rs1, RoundingMode rm = RoundingMode::dyn) {
    if (targetIs<RV32I | EXT_F>()) {
      const uint32 op = "5'b11010"_c
                        << "2'b00"_c
                        << "5'b00001"_c << rs1.Idx() << from(rm) << rd.Idx() << "7'b1010011"_c;

      st << [=](Strage& s) {
        s.word(op);
        s.desc(XKON_LAZY(s.format("ofir") % "fcvt.s.wu" % rd % rs1 % rm));
      };
    } else {
      unsupported(__func__);
    }
  }

  // +impl RV32::F::FMV.W.X
  void fmv_w_x(const FpReg& rd, const IntReg& rs1) {
    if (targetIs<RV32I | EXT_F>()) {
      const uint32 op = "5'b11110"_c
                        << "2'b00"_c
                        << "5'b00000"_c << rs1.Idx() << "3'b000"_c << rd.Idx() << "7'b1010011"_c;

      st << [=](Strage& s) {
        s.word(op);
        s.desc(XKON_LAZY(s.format("ofi") % "fmv.w.x" % rd % rs1));
      };
    } else {
      unsupported(__func__);
    }
  }

  //////////////////////////////////////////////////////////////////////////////

  // +impl RV32::D::FLD
  // +impl RV32::C::C.FLD
  // +impl RV32::C::C.FLDSP
  void fld(const FpReg& rs2, const IntOffsetReg& rs1) {
    const int32 imm12 = rs1.offset;
    XKON_ASSERT(isSintN(imm12, 12));

    if (targetIs<RV32I | EXT_D>()) {
      bool done = false;
      if (targetIs<EXT_C>()) {
        if (rs1 == sp && (isUintN(imm12, 9) && isAlignedN(imm12, 8))) {
          done = true;

          const uint32 op = "3'b001"_c << (_5)[imm12] << rs2.Idx() << (_4 - _3 | _8 - _6)[imm12] << "2'b10"_c;

          st << [=](Strage& s) {
            s.hword(op);
            s.desc(XKON_LAZY(s.format("ofI#fI") % "fld" % rs2 % rs1 % "c.fldsp" % rs2 % rs1));
          };
        } else if (rs2.isC() && rs1.isC() && (isUintN(imm12, 8) && isAlignedN(imm12, 8))) {
          done = true;

          const uint32 op = "3'b001"_c << (_5 - _3)[imm12] << rs1.CIdx() << (_7 - _6)[imm12] << rs2.CIdx() << "2'b00"_c;

          st << [=](Strage& s) {
            s.hword(op);
            s.desc(XKON_LAZY(s.format("ofI#fI") % "fld" % rs2 % rs1 % "c.fld" % rs2 % rs1));
          };
        }
      }

      if (!done) {
        const uint32 op = (_11 - _0)[imm12] << rs1.Idx() << "3'b011"_c << rs2.Idx() << "7'b0000111"_c;

        st << [=](Strage& s) {
          s.word(op);
          s.desc(XKON_LAZY(s.format("ofI") % "fld" % rs2 % rs1));
        };
      }
    } else {
      unsupported(__func__);
    }
  }

  // +impl RV32::D::FSD
  // +impl RV32::C::C.FSD
  // +impl RV32::C::C.FSDSP
  void fsd(const FpReg& rs2, const IntOffsetReg& rs1) {
    const int32 imm12 = rs1.offset;
    XKON_ASSERT(isSintN(imm12, 12));

    if (targetIs<RV32I | EXT_D>()) {
      bool done = false;

      if (targetIs<EXT_C>()) {
        if (rs2.isC() && rs1.isC() && (isUintN(imm12, 8) && isAlignedN(imm12, 8))) {
          done = true;

          const uint32 op = "3'b101"_c << (_5 - _3)[imm12] << rs1.CIdx() << (_7 - _6)[imm12] << rs2.CIdx() << "2'b00"_c;

          st << [=](Strage& s) {
            s.hword(op);
            s.desc(XKON_LAZY(s.format("ofI#fI") % "fsd" % rs2 % rs1 % "c.fsd" % rs2 % rs1));
          };
        } else if (rs1 == sp && (isUintN(imm12, 9) && isAlignedN(imm12, 8))) {
          done = true;

          const uint32 op = "3'b101"_c << (_5 - _3 | _8 - _6)[imm12] << rs2.Idx() << "2'b10"_c;

          st << [=](Strage& s) {
            s.hword(op);
            s.desc(XKON_LAZY(s.format("ofI#fI") % "fsd" % rs2 % rs1 % "c.fsdsp" % rs2 % rs1));
          };
        }
      }

      if (!done) {
        const uint32 op = (_11 - _5)[imm12] << rs2.Idx() << rs1.Idx() << "3'b011"_c << (_4 - _0)[imm12] << "7'b0100111"_c;

        st << [=](Strage& s) {
          s.word(op);
          s.desc(XKON_LAZY(s.format("ofI") % "fsd" % rs2 % rs1));
        };
      }
    } else {
      unsupported(__func__);
    }
  }

  // +impl RV32::D::FMADD.D
  void fmadd_d(const FpReg& rd, const FpReg& rs1, const FpReg& rs2, const FpReg& rs3, RoundingMode rm = RoundingMode::dyn) {
    if (targetIs<RV32I | EXT_D>()) {
      const uint32 op = rs3.Idx() << "2'b01"_c << rs2.Idx() << rs1.Idx() << from(rm) << rd.Idx() << "7'b1000011"_c;

      st << [=](Strage& s) {
        s.word(op);
        s.desc(XKON_LAZY(s.format("offffr") % "fmadd.d" % rd % rs1 % rs2 % rs3 % rm));
      };
    } else {
      unsupported(__func__);
    }
  }

  // +impl RV32::D::FMSUB.D
  void fmsub_d(const FpReg& rd, const FpReg& rs1, const FpReg& rs2, const FpReg& rs3, RoundingMode rm = RoundingMode::dyn) {
    if (targetIs<RV32I | EXT_D>()) {
      const uint32 op = rs3.Idx() << "2'b01"_c << rs2.Idx() << rs1.Idx() << from(rm) << rd.Idx() << "7'b1000111"_c;

      st << [=](Strage& s) {
        s.word(op);
        s.desc(XKON_LAZY(s.format("offffr") % "fmsub.d" % rd % rs1 % rs2 % rs3 % rm));
      };
    } else {
      unsupported(__func__);
    }
  }

  // +impl RV32::D::FNMSUB.D
  void fnmsub_d(const FpReg& rd, const FpReg& rs1, const FpReg& rs2, const FpReg& rs3, RoundingMode rm = RoundingMode::dyn) {
    if (targetIs<RV32I | EXT_D>()) {
      const uint32 op = rs3.Idx() << "2'b01"_c << rs2.Idx() << rs1.Idx() << from(rm) << rd.Idx() << "7'b1001011"_c;

      st << [=](Strage& s) {
        s.word(op);
        s.desc(XKON_LAZY(s.format("offffr") % "fnmsub.d" % rd % rs1 % rs2 % rs3 % rm));
      };
    } else {
      unsupported(__func__);
    }
  }

  // +impl RV32::D::FNMADD.D
  void fnmadd_d(const FpReg& rd, const FpReg& rs1, const FpReg& rs2, const FpReg& rs3, RoundingMode rm = RoundingMode::dyn) {
    if (targetIs<RV32I | EXT_D>()) {
      const uint32 op = rs3.Idx() << "2'b01"_c << rs2.Idx() << rs1.Idx() << from(rm) << rd.Idx() << "7'b1001111"_c;

      st << [=](Strage& s) {
        s.word(op);
        s.desc(XKON_LAZY(s.format("offffr") % "fnmadd.d" % rd % rs1 % rs2 % rs3 % rm));
      };
    } else {
      unsupported(__func__);
    }
  }

  // +impl RV32::D::FADD.D
  void fadd_d(const FpReg& rd, const FpReg& rs1, const FpReg& rs2, RoundingMode rm = RoundingMode::dyn) {
    if (targetIs<RV32I | EXT_D>()) {
      const uint32 op = "5'b00000"_c
                        << "2'b01"_c << rs2.Idx() << rs1.Idx() << from(rm) << rd.Idx() << "7'b1010011"_c;

      st << [=](Strage& s) {
        s.word(op);
        s.desc(XKON_LAZY(s.format("offfr") % "fadd.d" % rd % rs1 % rs2 % rm));
      };
    } else {
      unsupported(__func__);
    }
  }

  // +impl RV32::D::FSUB.D
  void fsub_d(const FpReg& rd, const FpReg& rs1, const FpReg& rs2, RoundingMode rm = RoundingMode::dyn) {
    if (targetIs<RV32I | EXT_D>()) {
      const uint32 op = "5'b00001"_c
                        << "2'b01"_c << rs2.Idx() << rs1.Idx() << from(rm) << rd.Idx() << "7'b1010011"_c;

      st << [=](Strage& s) {
        s.word(op);
        s.desc(XKON_LAZY(s.format("offfr") % "fsub.d" % rd % rs1 % rs2 % rm));
      };
    } else {
      unsupported(__func__);
    }
  }

  // +impl RV32::D::FMUL.D
  void fmul_d(const FpReg& rd, const FpReg& rs1, const FpReg& rs2, RoundingMode rm = RoundingMode::dyn) {
    if (targetIs<RV32I | EXT_D>()) {
      const uint32 op = "5'b00010"_c
                        << "2'b01"_c << rs2.Idx() << rs1.Idx() << from(rm) << rd.Idx() << "7'b1010011"_c;
      st << [=](Strage& s) {
        s.word(op);
        s.desc(XKON_LAZY(s.format("offfr") % "fmul.d" % rd % rs1 % rs2 % rm));
      };
    } else {
      unsupported(__func__);
    }
  }

  // +impl RV32::D::FDIV.D
  void fdiv_d(const FpReg& rd, const FpReg& rs1, const FpReg& rs2, RoundingMode rm = RoundingMode::dyn) {
    if (targetIs<RV32I | EXT_D>()) {
      const uint32 op = "5'b00011"_c
                        << "2'b01"_c << rs2.Idx() << rs1.Idx() << from(rm) << rd.Idx() << "7'b1010011"_c;

      st << [=](Strage& s) {
        s.word(op);
        s.desc(XKON_LAZY(s.format("offfr") % "fdiv.d" % rd % rs1 % rs2 % rm));
      };
    } else {
      unsupported(__func__);
    }
  }

  // +impl RV32::D::FSQRT.D
  void fsqrt_d(const FpReg& rd, const FpReg& rs1, RoundingMode rm = RoundingMode::dyn) {
    if (targetIs<RV32I | EXT_D>()) {
      const uint32 op = "5'b01011"_c
                        << "2'b01"_c
                        << "5'b00000"_c << rs1.Idx() << from(rm) << rd.Idx() << "7'b1010011"_c;

      st << [=](Strage& s) {
        s.word(op);
        s.desc(XKON_LAZY(s.format("offr") % "fsqrt.d" % rd % rs1 % rm));
      };
    } else {
      unsupported(__func__);
    }
  }

  // +impl RV32::D::FSGNJ.D
  // +impl pseudo::fmv.d rd, rs (fsgnj.d rd, rs, rs) Copy double-precision register
  void fsgnj_d(const FpReg& rd, const FpReg& rs1, const FpReg& rs2) {
    if (targetIs<RV32I | EXT_D>()) {
      const uint32 op = "5'b00100"_c
                        << "2'b01"_c << rs2.Idx() << rs1.Idx() << "3'b000"_c << rd.Idx() << "7'b1010011"_c;

      st << [=](Strage& s) {
        s.word(op);
        if (rs1 == rs2) {
          s.desc(XKON_LAZY(s.format("off") % "fmv.d" % rd % rs1));
        } else {
          s.desc(XKON_LAZY(s.format("offf") % "fsgnj.d" % rd % rs1 % rs2));
        }
      };
    } else {
      unsupported(__func__);
    }
  }

  // +impl RV32::D::FSGNJN.D
  // +impl pseudo::fneg.d rd, rs (fsgnjn.d rd, rs, rs) Double-precision negate
  void fsgnjn_d(const FpReg& rd, const FpReg& rs1, const FpReg& rs2) {
    if (targetIs<RV32I | EXT_D>()) {
      const uint32 op = "5'b00100"_c
                        << "2'b01"_c << rs2.Idx() << rs1.Idx() << "3'b001"_c << rd.Idx() << "7'b1010011"_c;

      st << [=](Strage& s) {
        s.word(op);
        if (rs1 == rs2) {
          s.desc(XKON_LAZY(s.format("off") % "fneg.d" % rd % rs1));
        } else {
          s.desc(XKON_LAZY(s.format("offf") % "fsgnjn.d" % rd % rs1 % rs2));
        }
      };
    } else {
      unsupported(__func__);
    }
  }

  // +impl RV32::D::FSGNJX.D
  // +impl pseudo::fabs.d rd, rs (fsgnjx.d rd, rs, rs) Double-precision absolute value
  void fsgnjx_d(const FpReg& rd, const FpReg& rs1, const FpReg& rs2) {
    if (targetIs<RV32I | EXT_D>()) {
      const uint32 op = "5'b00100"_c
                        << "2'b01"_c << rs2.Idx() << rs1.Idx() << "3'b010"_c << rd.Idx() << "7'b1010011"_c;

      st << [=](Strage& s) {
        s.word(op);
        if (rs1 == rs2) {
          s.desc(XKON_LAZY(s.format("off") % "fabs.d" % rd % rs1));
        } else {
          s.desc(XKON_LAZY(s.format("offf") % "fsgnjx.d" % rd % rs1 % rs2));
        }
      };
    } else {
      unsupported(__func__);
    }
  }

  // +impl RV32::D::FMIN.D
  void fmin_d(const FpReg& rd, const FpReg& rs1, const FpReg& rs2) {
    if (targetIs<RV32I | EXT_D>()) {
      const uint32 op = "5'b00101"_c
                        << "2'b01"_c << rs2.Idx() << rs1.Idx() << "3'b000"_c << rd.Idx() << "7'b1010011"_c;

      st << [=](Strage& s) {
        s.word(op);
        s.desc(XKON_LAZY(s.format("offf") % "fmin.d" % rd % rs1 % rs2));
      };
    } else {
      unsupported(__func__);
    }
  }

  // +impl RV32::D::FMAX.D
  void fmax_d(const FpReg& rd, const FpReg& rs1, const FpReg& rs2) {
    if (targetIs<RV32I | EXT_D>()) {
      const uint32 op = "5'b00101"_c
                        << "2'b01"_c << rs2.Idx() << rs1.Idx() << "3'b001"_c << rd.Idx() << "7'b1010011"_c;

      st << [=](Strage& s) {
        s.word(op);
        s.desc(XKON_LAZY(s.format("offf") % "fmax.d" % rd % rs1 % rs2));
      };
    } else {
      unsupported(__func__);
    }
  }

  // +impl RV32::D::FCVT.S.D
  void fcvt_s_d(const FpReg& rd, const FpReg& rs1, RoundingMode rm = RoundingMode::dyn) {
    if (targetIs<RV32I | EXT_D>()) {
      const uint32 op = "5'b01000"_c
                        << "2'b00"_c
                        << "5'b00001"_c << rs1.Idx() << from(rm) << rd.Idx() << "7'b1010011"_c;

      st << [=](Strage& s) {
        s.word(op);
        s.desc(XKON_LAZY(s.format("offr") % "fcvt.s.d" % rd % rs1 % rm));
      };
    } else {
      unsupported(__func__);
    }
  }

  // +impl RV32::D::FCVT.D.S
  // NOTE:単精度から倍精度への変換なので丸め誤差は発生しないので引数で指定できないようにした
  void fcvt_d_s(const FpReg& rd, const FpReg& rs1) {
    if (targetIs<RV32I | EXT_D>()) {
      const uint32 op = "5'b01000"_c
                        << "2'b01"_c
                        << "5'b00000"_c << rs1.Idx() << "3'b000"_c << rd.Idx() << "7'b1010011"_c;

      st << [=](Strage& s) {
        s.word(op);
        s.desc(XKON_LAZY(s.format("off") % "fcvt.d.s" % rd % rs1));
      };
    } else {
      unsupported(__func__);
    }
  }

  // +impl RV32::D::FEQ.D
  void feq_d(const IntReg& rd, const FpReg& rs1, const FpReg& rs2) {
    if (targetIs<RV32I | EXT_D>()) {
      const uint32 op = "5'b10100"_c
                        << "2'b01"_c << rs2.Idx() << rs1.Idx() << "3'b010"_c << rd.Idx() << "7'b1010011"_c;
      st << [=](Strage& s) {
        s.word(op);
        s.desc(XKON_LAZY(s.format("oiff") % "feq.d" % rd % rs1 % rs2));
      };
    } else {
      unsupported(__func__);
    }
  }

  // +impl RV32::D::FLT.D
  void flt_d(const IntReg& rd, const FpReg& rs1, const FpReg& rs2) {
    if (targetIs<RV32I | EXT_D>()) {
      const uint32 op = "5'b10100"_c
                        << "2'b01"_c << rs2.Idx() << rs1.Idx() << "3'b001"_c << rd.Idx() << "7'b1010011"_c;
      st << [=](Strage& s) {
        s.word(op);
        s.desc(XKON_LAZY(s.format("oiff") % "flt.d" % rd % rs1 % rs2));
      };
    } else {
      unsupported(__func__);
    }
  }

  // +impl RV32::D::FLE.D
  void fle_d(const IntReg& rd, const FpReg& rs1, const FpReg& rs2) {
    if (targetIs<RV32I | EXT_D>()) {
      const uint32 op = "5'b10100"_c
                        << "2'b01"_c << rs2.Idx() << rs1.Idx() << "3'b000"_c << rd.Idx() << "7'b1010011"_c;
      st << [=](Strage& s) {
        s.word(op);
        s.desc(XKON_LAZY(s.format("oiff") % "fle.d" % rd % rs1 % rs2));
      };
    } else {
      unsupported(__func__);
    }
  }

  // +impl RV32::D::FCLASS.D
  void fclass_d(const IntReg& rd, const FpReg& rs1) {
    if (targetIs<RV32I | EXT_D>()) {
      const uint32 op = "5'b11100"_c
                        << "2'b01"_c
                        << "5'b00000"_c << rs1.Idx() << "3'b001"_c << rd.Idx() << "7'b1010011"_c;

      st << [=](Strage& s) {
        s.word(op);
        s.desc(XKON_LAZY(s.format("oif") % "fclass.d" % rd % rs1));
      };
    } else {
      unsupported(__func__);
    }
  }

  // +impl RV32::D::FCVT.W.D
  void fcvt_w_d(const IntReg& rd, const FpReg& rs1, RoundingMode rm = RoundingMode::dyn) {
    if (targetIs<RV32I | EXT_D>()) {
      const uint32 op = "5'b11000"_c
                        << "2'b01"_c
                        << "5'b00000"_c << rs1.Idx() << from(rm) << rd.Idx() << "7'b1010011"_c;
      st << [=](Strage& s) {
        s.word(op);
        s.desc(XKON_LAZY(s.format("oifr") % "fcvt.w.d" % rd % rs1 % rm));
      };
    } else {
      unsupported(__func__);
    }
  }

  // +impl RV32::D::FCVT.WU.D
  void fcvt_wu_d(const IntReg& rd, const FpReg& rs1, RoundingMode rm = RoundingMode::dyn) {
    if (targetIs<RV32I | EXT_D>()) {
      const uint32 op = "5'b11000"_c
                        << "2'b01"_c
                        << "5'b00001"_c << rs1.Idx() << from(rm) << rd.Idx() << "7'b1010011"_c;
      st << [=](Strage& s) {
        s.word(op);
        s.desc(XKON_LAZY(s.format("oifr") % "fcvt.wu.d" % rd % rs1 % rm));
      };
    } else {
      unsupported(__func__);
    }
  }

  // +impl RV32::D::FCVT.D.W
  void fcvt_d_w(const FpReg& rd, const IntReg& rs1) {
    if (targetIs<RV32I | EXT_D>()) {
      const uint32 op = "5'b11010"_c
                        << "2'b01"_c
                        << "5'b00000"_c << rs1.Idx() << "3'b000"_c << rd.Idx() << "7'b1010011"_c;
      st << [=](Strage& s) {
        s.word(op);
        s.desc(XKON_LAZY(s.format("ofi") % "fcvt.d.w" % rd % rs1));
      };
    } else {
      unsupported(__func__);
    }
  }

  // +impl RV32::D::FCVT.D.WU
  void fcvt_d_wu(const FpReg& rd, const IntReg& rs1) {
    if (targetIs<RV32I | EXT_D>()) {
      const uint32 op = "5'b11010"_c
                        << "2'b01"_c
                        << "5'b00001"_c << rs1.Idx() << "3'b000"_c << rd.Idx() << "7'b1010011"_c;
      st << [=](Strage& s) {
        s.word(op);
        s.desc(XKON_LAZY(s.format("ofi") % "fcvt.d.wu" % rd % rs1));
      };
    } else {
      unsupported(__func__);
    }
  }

  //////////////////////////////////////////////////////////////////////////////

  // -impl RV64::C::C.LD
  // -impl RV32::C::C.EBREAK
  // -impl RV128::C::C.SQSP

  //////////////////////////////////////////////////////////////////////////////
  // 疑似命令

  // +impl pseudo::nop (addi x0, x0, 0) No operation
  void nop() { addi(x0, x0, 0); }

  // +impl pseudo::li rd, immediate (Myriad sequences) Load immediate
  void li(const IntReg& rd, uint32 imm) {
    // addi命令は即値を12ビットの「符号付き」整数として扱うので注意
    uint32 hi = (imm & 0xfffff000) + ((imm & 0x0800) << 1);
    int32 lo = signextend(imm & 0x00000fff, 12);

    // immの上位20ビットが非0ならluiで上位20ビットをセット
    if (hi != 0) {
      lui(rd, 0xfffff & (hi >> 12));
      // immの下位12ビットが非0ならaddiで下位12ビットをセット
      if (lo != 0) {
        addi(rd, rd, lo);
      }
    } else {
      // 上位20ビットは0だったので下位12ビットをaddiでセット
      addi(rd, zero, lo);
    }
  }

  // +impl pseudo::mv rd, rs (addi rd, rs, 0) Copy register
  void mv(const IntReg& rd, const IntReg& rs) {
    if (targetIs<EXT_C>()) {
      add(rd, zero, rs);
    } else {
      addi(rd, rs, 0);
    }
  }

  // +impl pseudo::not rd, rs (xori rd, rs, -1) One’s complement
  void not_(const IntReg& rd, const IntReg& rs) { xori(rd, rs, -1); }
#if XKON_OPERATER_NAMES_ARE_USEABLE
  void XKON_INSN_NAME(not )(const IntReg& rd, const IntReg& rs) { not_(rd, rs); }
#endif

  // +impl pseudo::neg rd, rs (sub rd, x0, rs) Two’s complement
  void neg(const IntReg& rd, const IntReg& rs) { sub(rd, x0, rs); }

  // NOTE:RV64I
  // -impl pseudo::negw rd, rs (subw rd, x0, rs) Two’s complement word

  // NOTE:RV64I
  // -impl pseudo::sext.w rd, rs (addiw rd, rs, 0) Sign extend word

  // +impl pseudo::seqz rd, rs (sltiu rd, rs, 1) Set if = zero
  void seqz(const IntReg& rd, const IntReg& rs) { sltiu(rd, rs, 1); }

  // +impl pseudo::snez rd, rs (sltu rd, x0, rs) Set if ̸= zero
  void snez(const IntReg& rd, const IntReg& rs) { sltu(rd, x0, rs); }

  // +impl pseudo::sltz rd, rs (slt rd, rs, x0) Set if < zero
  void sltz(const IntReg& rd, const IntReg& rs) { slt(rd, rs, x0); }

  // +impl pseudo::sgtz rd, rs (slt rd, x0, rs) Set if > zero
  void sgtz(const IntReg& rd, const IntReg& rs) { slt(rd, x0, rs); }

  // +impl pseudo::fmv.s rd, rs (fsgnj.s rd, rs, rs) Copy single-precision register
  void fmv_s(const FpReg& rd, const FpReg& rs) { fsgnj_s(rd, rs, rs); }

  // +impl pseudo::fabs.s rd, rs (fsgnjx.s rd, rs, rs) Single-precision absolute value
  void fabs_s(const FpReg& rd, const FpReg& rs) { fsgnjx_s(rd, rs, rs); }

  // +impl pseudo::fneg.s rd, rs (fsgnjn.s rd, rs, rs) Single-precision negate
  void fneg_s(const FpReg& rd, const FpReg& rs) { fsgnjn_s(rd, rs, rs); }

  // +impl pseudo::fmv.d rd, rs (fsgnj.d rd, rs, rs) Copy double-precision register
  void fmv_d(const FpReg& rd, const FpReg& rs) { fsgnj_d(rd, rs, rs); }

  // +impl pseudo::fabs.d rd, rs (fsgnjx.d rd, rs, rs) Double-precision absolute value
  void fabs_d(const FpReg& rd, const FpReg& rs) { fsgnjx_d(rd, rs, rs); }

  // +impl pseudo::fneg.d rd, rs (fsgnjn.d rd, rs, rs) Double-precision negate
  void fneg_d(const FpReg& rd, const FpReg& rs) { fsgnjn_d(rd, rs, rs); }

  // +impl pseudo::beqz rs, offset (beq rs, x0, offset) Branch if = zero
  void beqz(const IntReg& rs, const Label& label) { beq(rs, x0, label); }
  void beqz(const IntReg& rs, const char* label) { beqz(rs, str2label(label)); }
  void beqz(const IntReg& rs, addr_t addr) { beqz(rs, addr2label(addr)); }

  // +impl pseudo::bnez rs, offset (bne rs, x0, offset) Branch if ̸= zero
  void bnez(const IntReg& rs, const Label& label) { bne(rs, x0, label); }
  void bnez(const IntReg& rs, const char* label) { bnez(rs, str2label(label)); }
  void bnez(const IntReg& rs, addr_t addr) { bnez(rs, addr2label(addr)); }

  // +impl pseudo::blez rs, offset (bge x0, rs, offset) Branch if ≤ zero
  void blez(const IntReg& rs, const Label& label) { bge(x0, rs, label); }
  void blez(const IntReg& rs, const char* label) { blez(rs, str2label(label)); }
  void blez(const IntReg& rs, addr_t addr) { blez(rs, addr2label(addr)); }

  // +impl pseudo::bgez rs, offset (bge rs, x0, offset) Branch if ≥ zero
  void bgez(const IntReg& rs, const Label& label) { bge(rs, x0, label); }
  void bgez(const IntReg& rs, const char* label) { bgez(rs, str2label(label)); }
  void bgez(const IntReg& rs, addr_t addr) { bgez(rs, addr2label(addr)); }

  // +impl pseudo::bltz rs, offset (blt rs, x0, offset) Branch if < zero
  void bltz(const IntReg& rs, const Label& label) { blt(rs, x0, label); }
  void bltz(const IntReg& rs, const char* label) { bltz(rs, str2label(label)); }
  void bltz(const IntReg& rs, addr_t addr) { bltz(rs, addr2label(addr)); }

  // +impl pseudo::bgtz rs, offset (blt x0, rs, offset) Branch if > zero
  void bgtz(const IntReg& rs, const Label& label) { blt(x0, rs, label); }
  void bgtz(const IntReg& rs, const char* label) { bgtz(rs, str2label(label)); }
  void bgtz(const IntReg& rs, addr_t addr) { bgtz(rs, addr2label(addr)); }

  // +impl pseudo::bgt rs, rt, offset (blt rt, rs, offset) Branch if >
  void bgt(const IntReg& rs, const IntReg& rt, const Label& label) { blt(rt, rs, label); }
  void bgt(const IntReg& rs, const IntReg& rt, const char* label) { bgt(rs, rt, str2label(label)); }
  void bgt(const IntReg& rs, const IntReg& rt, addr_t addr) { bgt(rs, rt, addr2label(addr)); }

  // +impl pseudo::ble rs, rt, offset (bge rt, rs, offset) Branch if ≤
  void ble(const IntReg& rs, const IntReg& rt, const Label& label) { bge(rt, rs, label); }
  void ble(const IntReg& rs, const IntReg& rt, const char* label) { ble(rs, rt, str2label(label)); }
  void ble(const IntReg& rs, const IntReg& rt, addr_t addr) { ble(rs, rt, addr2label(addr)); }

  // +impl pseudo::bgtu rs, rt, offset (bltu rt, rs, offset) Branch if >, unsigned
  void bgtu(const IntReg& rs, const IntReg& rt, const Label& label) { bltu(rt, rs, label); }
  void bgtu(const IntReg& rs, const IntReg& rt, const char* label) { bgtu(rs, rt, str2label(label)); }
  void bgtu(const IntReg& rs, const IntReg& rt, addr_t addr) { bgtu(rs, rt, addr2label(addr)); }

  // +impl pseudo::bleu rs, rt, offset (bgeu rt, rs, offset) Branch if ≤, unsigned
  void bleu(const IntReg& rs, const IntReg& rt, const Label& label) { bgeu(rt, rs, label); }
  void bleu(const IntReg& rs, const IntReg& rt, const char* label) { bleu(rs, rt, str2label(label)); }
  void bleu(const IntReg& rs, const IntReg& rt, addr_t addr) { bleu(rs, rt, addr2label(addr)); }

  // +impl pseudo::j offset (jal x0, offset) Jump
  void j(const Label& label) { jal(zero, label); }
  void j(const char* label) { j(str2label(label)); }
  void j(addr_t addr) { j(addr2label(addr)); }

  // +impl pseudo::jal offset (jal x1, offset) Jump and link
  void jal(const Label& label) { jal(x1, label); }
  void jal(const char* label) { jal(str2label(label)); }
  void jal(addr_t addr) { jal(addr2label(addr)); }

  // +impl pseudo::jr rs (jalr x0, 0(rs)) Jump register
  void jr(const IntReg& rs) { jalr(x0, rs(0)); }

  // +impl pseudo::jalr rs (jalr x1, 0(rs)) Jump and link register
  void jalr(const IntReg& rs) { jalr(x1, rs(0)); }

  // +impl pseudo::ret (jalr x0, 0(x1)) Return from subroutine
  void ret() { jalr(zero, ra(0)); }

  // NOTE:とりあえず実装したが、動作検証が不十分
  // ~impl pseudo::call offset (auipc x1, offset[31 : 12] + offset[11] |jalr x1, offset[11:0](x1)) Call far-away subroutine
  void call(const Label& label) {
    if (targetIs<RV32I>()) {
      st << [=](Strage& s) {
        // 命令生成に必要な定数の計算
        uint32 offset = static_cast<uint32>(label.relAddr());
        uint32 hi = (((offset & 0xfffff000) + ((offset & 0x0800) << 1))) >> 12;
        int32 lo = offset & 0x00000fff;
        if (offset & 0x00000800) {  //符号拡張
          lo |= 0xfffff000;
        }
        XKON_ASSERT((hi << 12) + lo == offset);

        // 命令の生成
        {
          const IntReg rd = ra;
          const uint32 imm = hi << 12;
          const uint32 op = (_31 - _12)[imm] << rd.Idx() << "7'b0010111"_c;
          s.word(op);
          s.desc(XKON_LAZY(s.format("oiu") % "auipc" % rd % hi));
        }
        s.updatePC();
        {
          const IntReg rd = x1;
          const IntOffsetReg rs1 = x1[lo];
          const uint32 op = (_11 - _0)[lo] << rs1.Idx() << "3'b000"_c << rd.Idx() << "7'b1100111"_c;
          s.word(op);
          s.desc(XKON_LAZY(s.format("oJ") % "jalr" % rs1));
        }
      };
    } else {
      unsupported(__func__);
    }
  }
  void call(const char* label) { call(str2label(label)); }
  void call(addr_t addr) { call(addr2label(addr)); }

  // NOTE:とりあえず実装したが、動作検証が不十分
  // ~impl pseudo::tail offset (auipc x6, offset[31 : 12] + offset[11] |jalr x0, offset[11:0](x6)) Tail call far-away subroutine
  void tail(const Label& label) {
    if (targetIs<RV32I>()) {
      st << [=](Strage& s) {
        // 命令生成に必要な定数の計算
        uint32 offset = static_cast<uint32>(label.relAddr());
        uint32 hi = (((offset & 0xfffff000) + ((offset & 0x0800) << 1))) >> 12;
        int32 lo = offset & 0x00000fff;
        if (offset & 0x00000800) {  //符号拡張
          lo |= 0xfffff000;
        }
        XKON_ASSERT((hi << 12) + lo == offset);

        // 命令の生成
        {
          const IntReg rd = x6;
          const uint32 imm = hi << 12;
          const uint32 op = (_31 - _12)[imm] << rd.Idx() << "7'b0010111"_c;
          s.word(op);
          s.desc(XKON_LAZY(s.format("oiu") % "auipc" % rd % hi));
        }
        s.updatePC();
        {
          const IntReg rd = x0;
          const IntOffsetReg rs1 = x6[lo];
          const uint32 op = (_11 - _0)[lo] << rs1.Idx() << "3'b000"_c << rd.Idx() << "7'b1100111"_c;
          s.word(op);
          s.desc(XKON_LAZY(s.format("oJ") % "jr" % rs1));
        }
      };
    } else {
      unsupported(__func__);
    }
  }
  void tail(const char* label) { tail(str2label(label)); }
  void tail(addr_t addr) { tail(addr2label(addr)); }

  // !impl pseudo::fence (fence iorw, iorw) Fence on all memory and I/O
  // !impl pseudo::rdinstret[h] rd (csrrs rd, instret[h], x0) Read instructions-retired counter
  // !impl pseudo::rdcycle[h] rd (csrrs rd, cycle[h], x0) Read cycle counter
  // !impl pseudo::rdtime[h] rd (csrrs rd, time[h], x0) Read real-time clock
  // !impl pseudo::csrr rd, csr (csrrs rd, csr, x0) Read CSR
  // !impl pseudo::csrw csr, rs (csrrw x0, csr, rs) Write CSR
  // !impl pseudo::csrs csr, rs (csrrs x0, csr, rs) Set bits in CSR
  // !impl pseudo::csrc csr, rs (csrrc x0, csr, rs) Clear bits in CSR
  // !impl pseudo::csrwi csr, imm (csrrwi x0, csr, imm) Write CSR, immediate
  // !impl pseudo::csrsi csr, imm (csrrsi x0, csr, imm) Set bits in CSR, immediate
  // !impl pseudo::csrci csr, imm (csrrci x0, csr, imm) Clear bits in CSR, immediate
  // !impl pseudo::frcsr rd (csrrs rd, fcsr, x0) Read FP control/status register
  // !impl pseudo::fscsr rd, rs (csrrw rd, fcsr, rs) Swap FP control/status register
  // !impl pseudo::fscsr rs (csrrw x0, fcsr, rs) Write FP control/status register
  // !impl pseudo::frrm rd (csrrs rd, frm, x0) Read FP rounding mode
  // !impl pseudo::fsrm rd, rs (csrrw rd, frm, rs) Swap FP rounding mode
  // !impl pseudo::fsrm rs (csrrw x0, frm, rs) Write FP rounding mode
  // !impl pseudo::frflags rd (csrrs rd, fflags, x0) Read FP exception flags
  // !impl pseudo::fsflags rd, rs (csrrw rd, fflags, rs) Swap FP exception flags
  // !impl pseudo::fsflags rs (csrrw x0, fflags, rs) Write FP exception flags

  // 以下の要素を定義した自動生成されるヘッダファイルの取り込み
  // ・ドットを含む命令の実装用のクラスの定義
  // ・メンバー変数の定義
  // ・コンストラクタの定義
#include "xkon_dot.hpp"
};
}  // namespace xkon
