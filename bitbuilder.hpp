#include <cassert>
#include <string>
#include <type_traits>

namespace BitBuilder {

namespace {
/**
 *  Width to mask
 */
inline constexpr unsigned long long w2m(const int width) {
  assert(0 <= width && width <= 64 && "Width must [0,64].");
  if (width == 64) {
    return ~0ull;
  } else {
    return (1ull << width) - 1;
  }
}

}  // namespace

/**
 * ビット幅を持つ定数
 * 最大64ビットまで対応
 */
struct Constant {
  const int width;                /** ビット幅 */
  const unsigned long long value; /** 値 */
  inline constexpr Constant(int width, unsigned long long value) : width(width), value(w2m(width) & value) {}

  std::string toString() const {
    char buf[128];
    std::snprintf(buf, sizeof(buf) - 1, "%d'h%llx", width, value);
    return std::string(buf);
  }

  template <typename T>
  T as() const {
    assert((width % 8) == 0);
    return static_cast<T>(value);
  }

  template <typename T>
  operator T() const {
    assert((width % 8) == 0);
    return static_cast<T>(value);
  }
};

namespace {

////////////////////////////////////////////////////////////////////////////////
// BitBuilder の内部処理用クラスの定義

struct Tag {};  // 内部処理用クラス識別用タグ

template <int i, int n>
struct RepBit;

template <int i>
struct Bit : public Tag {
  typedef Bit<i> self_t;
  enum { bit = i, len = 1 };
  static const unsigned long long mask = 1ull << i;

  template <typename T>
  static T apply(T src, int shift = 0) {
    return (!!(src & mask)) << shift;
  }

  template <typename T>
  Constant operator[](T val) {
    return Constant(1, self_t::apply(val));
  }

  template <int n>
  auto Repeat() {
    return RepBit<bit, n>();
  }
};

template <int i, int n>
struct RepBit : public Bit<i> {
  typedef RepBit<i, n> self_t;
  enum { len = n };
  static const unsigned long long mask = 1ull << i;
  static const unsigned long long bits = w2m(n);

  template <typename T>
  static T apply(T src, int shift = 0) {
    return ((src & mask) ? bits : 0) << shift;
  }

  template <typename T>
  Constant operator[](T val) {
    return Constant(n, self_t::apply(val));
  }
};

template <int hi, int lo>
struct Range : public Tag {
  typedef Range<hi, lo> self_t;
  static const unsigned long long hi_ = w2m(hi + 1);
  static const unsigned long long lo_ = w2m(lo + 1);
  static const unsigned long long mask = hi_ ^ (lo_ >> 1);
  enum { shift = lo, len = hi - lo + 1 };

  template <typename T>
  static T apply(T src, int s = 0) {
    return ((src & mask) >> shift) << s;
  }
  template <typename T>
  Constant operator[](T val) {
    return Constant(len, self_t::apply(val));
  }
};  // namespace BitBuilder

template <class L, class R>
struct List : public Tag {
  typedef List<L, R> self_t;

  enum { len = L::len + R::len };

  template <typename T>
  static T apply(T src, int shift = 0) {
    return L::apply(src, R::len + shift) | R::apply(src, shift);
  }
  template <typename T>
  Constant operator[](T val) {
    return Constant(len, self_t::apply(val));
  }
};

////////////////////////////////////////////////////////////////////////////////
// Constant クラスインスタンス生成用ユーザー定義リテラルの内部処理関数

inline constexpr int strtoi(const char* s) {
  int i = 0;
  while ('0' <= *s && *s <= '9') {
    i = i * 10 + (*s++ - '0');
  }
  return i;
}

inline constexpr unsigned long long bin2ull(const char* s) {
  unsigned long long val = 0;
  for (; *s != '\0'; s++) {
    switch (*s) {
      case '0':
        val <<= 1;
        break;
      case '1':
        val = (val << 1) | 1;
        break;
      case '_':
        /* do nothing */
        break;
      default:
        return val;
    }
  }
  return val;
}

inline constexpr unsigned long long oct2ull(const char* s) {
  unsigned long long val = 0;
  for (; *s != '\0'; s++) {
    switch (*s) {
      case '0':
      case '1':
      case '2':
      case '3':
      case '4':
      case '5':
      case '6':
      case '7':
        val = val * 8 + *s - '0';
        break;
      default:
        return val;
    }
  }
  return val;
}

inline constexpr unsigned long long dec2ull(const char* s) {
  unsigned long long val = 0;
  for (; *s != '\0'; s++) {
    switch (*s) {
      case '0':
      case '1':
      case '2':
      case '3':
      case '4':
      case '5':
      case '6':
      case '7':
      case '8':
      case '9':
        val = val * 10 + *s - '0';
        break;
      default:
        return val;
    }
  }
  return val;
}

inline constexpr unsigned long long hex2ull(const char* s) {
  unsigned long long val = 0;
  for (; *s != '\0'; s++) {
    switch (*s) {
      case '0':
      case '1':
      case '2':
      case '3':
      case '4':
      case '5':
      case '6':
      case '7':
      case '8':
      case '9':
        val = val * 16 + *s - '0';
        break;
      case 'a':
      case 'b':
      case 'c':
      case 'd':
      case 'e':
      case 'f':
        val = val * 16 + *s - 'a' + 10;
        break;
      case 'A':
      case 'B':
      case 'C':
      case 'D':
      case 'E':
      case 'F':
        val = val * 16 + *s - 'A' + 10;
        break;
      default:
        return val;
    }
  }
  return val;
}

}  // namespace

////////////////////////////////////////////////////////////////////////////////
// BitBuilder 用の定数の定義

// コンパイラやC++の仕様バージョンの差異の吸収

#if 201703L <= __cplusplus
#define MAYBE_UNUSED [[maybe_unused]]
#endif

#ifdef __GNUC__
#ifndef MAYBE_UNUSED
#define MAYBE_UNUSED __attribute__((unused))
#endif
#endif

#ifndef MAYBE_UNUSED
#define MAYBE_UNUSED
#endif

MAYBE_UNUSED Bit<0> _0;
MAYBE_UNUSED Bit<1> _1;
MAYBE_UNUSED Bit<2> _2;
MAYBE_UNUSED Bit<3> _3;
MAYBE_UNUSED Bit<4> _4;
MAYBE_UNUSED Bit<5> _5;
MAYBE_UNUSED Bit<6> _6;
MAYBE_UNUSED Bit<7> _7;
MAYBE_UNUSED Bit<8> _8;
MAYBE_UNUSED Bit<9> _9;
MAYBE_UNUSED Bit<10> _10;
MAYBE_UNUSED Bit<11> _11;
MAYBE_UNUSED Bit<12> _12;
MAYBE_UNUSED Bit<13> _13;
MAYBE_UNUSED Bit<14> _14;
MAYBE_UNUSED Bit<15> _15;
MAYBE_UNUSED Bit<16> _16;
MAYBE_UNUSED Bit<17> _17;
MAYBE_UNUSED Bit<18> _18;
MAYBE_UNUSED Bit<19> _19;
MAYBE_UNUSED Bit<20> _20;
MAYBE_UNUSED Bit<21> _21;
MAYBE_UNUSED Bit<22> _22;
MAYBE_UNUSED Bit<23> _23;
MAYBE_UNUSED Bit<24> _24;
MAYBE_UNUSED Bit<25> _25;
MAYBE_UNUSED Bit<26> _26;
MAYBE_UNUSED Bit<27> _27;
MAYBE_UNUSED Bit<28> _28;
MAYBE_UNUSED Bit<29> _29;
MAYBE_UNUSED Bit<30> _30;
MAYBE_UNUSED Bit<31> _31;
MAYBE_UNUSED Bit<32> _32;
MAYBE_UNUSED Bit<33> _33;
MAYBE_UNUSED Bit<34> _34;
MAYBE_UNUSED Bit<35> _35;
MAYBE_UNUSED Bit<36> _36;
MAYBE_UNUSED Bit<37> _37;
MAYBE_UNUSED Bit<38> _38;
MAYBE_UNUSED Bit<39> _39;
MAYBE_UNUSED Bit<40> _40;
MAYBE_UNUSED Bit<41> _41;
MAYBE_UNUSED Bit<42> _42;
MAYBE_UNUSED Bit<43> _43;
MAYBE_UNUSED Bit<44> _44;
MAYBE_UNUSED Bit<45> _45;
MAYBE_UNUSED Bit<46> _46;
MAYBE_UNUSED Bit<47> _47;
MAYBE_UNUSED Bit<48> _48;
MAYBE_UNUSED Bit<49> _49;
MAYBE_UNUSED Bit<50> _50;
MAYBE_UNUSED Bit<51> _51;
MAYBE_UNUSED Bit<52> _52;
MAYBE_UNUSED Bit<53> _53;
MAYBE_UNUSED Bit<54> _54;
MAYBE_UNUSED Bit<55> _55;
MAYBE_UNUSED Bit<56> _56;
MAYBE_UNUSED Bit<57> _57;
MAYBE_UNUSED Bit<58> _58;
MAYBE_UNUSED Bit<59> _59;
MAYBE_UNUSED Bit<60> _60;
MAYBE_UNUSED Bit<61> _61;
MAYBE_UNUSED Bit<62> _62;
MAYBE_UNUSED Bit<63> _63;
#undef MAYBE_UNUSED

////////////////////////////////////////////////////////////////////////////////
// Expression template 用の演算子オーバーロードの定義

template <class L, class R,                                                       //
          class = typename std::enable_if<std::is_base_of<Tag, L>::value>::type,  //
          class = typename std::enable_if<std::is_base_of<Tag, R>::value>::type>
inline constexpr auto operator-(const L& l, const R& r) {
  Range<L::bit, R::bit> x;
  return x;
}

template <class L, class R,                                                       //
          class = typename std::enable_if<std::is_base_of<Tag, L>::value>::type,  //
          class = typename std::enable_if<std::is_base_of<Tag, R>::value>::type>
inline constexpr List<L, R> operator|(const L& l, const R& r) {
  List<L, R> x;
  return x;
}

inline constexpr Constant operator<<(const Constant& a, const Constant& b) {  //
  return Constant(a.width + b.width, (a.value << b.width) | b.value);
}

/**
 *  Verilog風の数値定数のユーザー定義リテラル
 *  "3'b110"_c
 * のように書くとコンパイル時にConstantクラスのインスタンスに変換される。
 */
constexpr inline const Constant operator""_c(const char* str, std::size_t length) {
  // ビット数取得
  const int width = strtoi(str);
  assert(0 < width && width <= 64 && "Width must (0,64].");

  // ' の次の文字までスキップ
  while (*++str != '\0') {
    if (*str == '\'') {
      str++;
      break;
    }
  }
  assert(*str != '\0' && "\"'\" cannot be found.");

  // 進数の判定と数値の取得
  unsigned long long val = 0;
  const int base = *str++;
  switch (base) {
    case 'b':
      val = bin2ull(str);
      break;
    case 'o':
      val = oct2ull(str);
      break;
    case 'd':
      val = dec2ull(str);
      break;
    case 'h':
      val = hex2ull(str);
      break;
    default:
      assert(false && "Unknown base.");
      break;
  }

  return Constant(width, val);
}
}  // namespace BitBuilder
