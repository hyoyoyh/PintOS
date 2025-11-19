#include "thread.h"
#define F (1 << 14)   // 17.14 고정소수점

/* 정수 <-> 고정소수점 */
int fixed(int n)            { return n * F; }   // fixed(a) 역할
int unfixed(int x)      { return x / F; }   // unfixed(b) 역할 (버림)
int banup(int x)      { return x >= 0 ? (x + F/2) / F: (x - F/2) / F; }

int fp_add(int x, int y)        { return x + y; }
int fp_sub(int x, int y)        { return x - y; }

int fp_add_int(int x, int n)    { return x + n * F; }
int fp_sub_int(int x, int n)    { return x - n * F; }

int fp_mul(int x, int y)        { return (int)((int64_t)x * y / F); }
int fp_mul_int(int x, int n)    { return x * n; }

int fp_div(int x, int y)        { return (int)((int64_t)x * F / y); }
int fp_div_int(int x, int n)    { return x / n; }
