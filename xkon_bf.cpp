#define DEBUG 1
#include <cstdio>
#include <stack>
#include <string>
#include <chrono>
#include <iostream>

#include "xkon.hpp"

using namespace std;

typedef unsigned char uchar;
typedef void(func_t)(void);

static void put(int ch) { putchar(ch); }
static int getch(void) {
  while (true) {
    int ch = getchar();
    if (ch != EOF) return ch;
  }
}

class BfJIT : public xkon::CodeGenerator<xkon::RV32GC> {
  void operator=(const BfJIT &);

  int label_count;
  uchar mem[10000];
  func_t *jit;

  string getLabel() {
    char buf[16];
    sprintf(buf, ".L%d", label_count++);
    return buf;
  }

 public:
  BfJIT(const char *src) : xkon::CodeGenerator<xkon::RV32GC>(1024), label_count(0), mem{0},jit(NULL) {
    // Register usage
    // a0 : Temporary for memory access & function argument/result.
    // s1 : BF memory pointer.
    // s2 : Pointer to put function.
    // s3 : Pointer to get function.

    // [ and ] command nesting management stack.
    stack<string> par;

    // Save registers to stack area.
    addi(sp, sp, -32);
    sw(ra, sp[24]);
    sw(s0, sp[20]);
    sw(s1, sp[16]);
    sw(s2, sp[12]);
    sw(s3, sp[8]);
    addi(s0, sp, 32);

    li(s1, (intptr_t) & (this->mem[0]));
    li(s2, (intptr_t)put);
    li(s3, (intptr_t)getch);

    // Flag for current pointer is equals to a0 register value.
    bool store = false;

    // Variables for optimize command repeat.
    char code = '\0'; // Unprocessed command character code.
    int count = 0; // Count unprocessed command 

    // JIT compile main loop
    for (const char *p = src;; ++p) {
      // 最後の命令の読み出し後にコンパイル未完了な命令がcode/countに残る可能性があるので
      // for文内でループを抜けず、未処理の命令の処理が終わるタイミングでbreakする

      // Generate optimized code.
      if (*p != code && (0 < count && code != '\0')) {
        switch (code) {
          case '>':
            addi(s1, s1, count);
            store = false;
            break;
          case '<':
            addi(s1, s1, -count);
            store = false;
            break;
          case '+':
            if (!store) {
              lbu(a0, s1[0]);
            }
            addi(a0, a0, count);
            sb(a0, s1[0]);
            store = true;
            break;
          case '-':
            if (!store) {
              lbu(a0, s1[0]);
            }
            addi(a0, a0, -count);
            sb(a0, s1[0]);
            store = false;
            break;
        }
        code = '\0';
        count = 0;
      }

      // Check main loop is ended.
      if (*p == '\0') {
        break;
      }

      // Read command.
      switch (*p) {
        case '<':
        case '>':
        case '+':
        case '-':
          code = *p;
          count++;
          break;
        case '[': {
          string l = getLabel();
          par.push(l);

          L((l + "B").c_str());
          lbu(a0, s1[0]);
          beqz(a0, (l + "E").c_str());

          store = false;
          break;
        }
        case ']': {
          string l = par.top();
          par.pop();
          j((l + "B").c_str());
          L((l + "E").c_str());

          store = false;
          break;
        }
        case '.':
          if (!store) {
            lbu(a0, s1[0]);
          }
          jalr(ra, s2(0));

          store = false;
          break;
        case ',':
          jalr(ra, s3(0));
          sb(a0, s1[0]);

          store = true;
          break;
        default:
          break;
      }
    }

    // Restore register from stack area.
    lw(s3, sp[8]);
    lw(s2, sp[12]);
    lw(s1, sp[16]);
    lw(s0, sp[20]);
    lw(ra, sp[24]);
    addi(sp, sp, 32);
    ret();
  }

  void gen() {
    this->jit = this->generate<void (*)(void)>();
  }

  void exec() {
    for(int i=0;i<10000;++i)mem[i]=0;
    jit();
  }
};

// Implement as interpreter.
class Bf {
  const char *src;
  uchar mem[10000];

public:
  Bf(const char *src) : src(src),mem{0} {
  }

  void exec() {
    for(int i=0;i<10000;++i)mem[i]=0;
    const char *pc=&src[0];
    uchar *p = &mem[0];
    
    while(*pc!='\0'){
      switch(*pc){
        case '+':
          (*p)++;
          ++pc;
          break;
        case '-':
          (*p)--;
          ++pc;
          break;
        case '>':
          p++;
          ++pc;
          break;
        case '<':
          p--;
          ++pc;
          break;
        case '[':
          if(*p==0){
            while(*pc!=']'){
              ++pc;
            }
          } 
          ++pc;
          break;
        case ']':
          if(*p!=0){
            while(*pc!='['){
              --pc;
            }
          }
          ++pc;
          break;
        case '.':
          put(*p);
          ++pc;
          break;
        case ',':
          *p=getch();
          ++pc;
          break;
      }
    }
  }
};

using namespace  std::chrono;
int main(void) {
  const char *hello_world =
      "+++++++++[>++++++++>+++++++++++>+++>+<<<<-]>.>++.+++++++..+++.>+++++.<<+"
      "++++++++++++++.>.+++.------.--------.>+.>+" 
      ">"
      "+++++++++[>++++++++>+++++++++++>+++>+<<<<-]>.>++.+++++++..+++.>+++++.<<+"
      "++++++++++++++.>.+++.------.--------.>+.>+."
      ;
  int sum=0;
  printf("\n\nxkon JIT assembler sample program.\n");
  printf("Input program:%s\n\n", hello_world);

  printf("==================================================================================================\n");
  printf("= Interpret 5 times.\n\n");
  sum=0;
  Bf *o = new Bf(hello_world);
  for(int i=0 ; i<5 ; ++i) {
    auto iStart=std::chrono::system_clock::now(); 
    o->exec();
    auto iEnd = system_clock::now(); 
    int usec= duration_cast<std::chrono::microseconds>(iEnd-iStart).count();
    std::cout<< "Exec time:"<< usec<<"[usec]"<<std::endl; 
    sum+=usec;
  }
  std::cout<< std::endl;
  std::cout<< "Average:"<< (sum/5)<<"[usec]"<<std::endl; 
  std::cout<< std::endl;

  printf("==================================================================================================\n");
  printf("Execute 5 times with JIT precompile using xkon.\n\n");
  sum=0;
  BfJIT *jito = new BfJIT(hello_world);
  jito->gen();
  for(int i=0 ; i<5 ; ++i) {
    auto jitStart=std::chrono::system_clock::now(); 
    jito->exec();
    auto jitEnd = system_clock::now(); 
    int usec= duration_cast<std::chrono::microseconds>(jitEnd-jitStart).count();
    std::cout<< "Exec time:"<< usec<<"[usec]"<<std::endl; 
    sum+=usec;
  }
  std::cout<< std::endl;
  std::cout<< "Averate:"<< (sum/5)<<"[usec]"<<std::endl; 
  std::cout<< std::endl;
}
