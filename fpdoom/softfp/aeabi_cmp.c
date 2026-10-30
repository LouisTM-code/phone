/* __aeabi_[df]cmp{eq,lt,le,ge,gt} for ARMv5 Thumb: compiler-rt has them only as
   Thumb-2/ARM assembly (the cmpun ones are in compare[ds]f2.c) */
int __eqdf2(double, double);
int __ltdf2(double, double);
int __ledf2(double, double);
int __gedf2(double, double);
int __gtdf2(double, double);
int __eqsf2(float, float);
int __ltsf2(float, float);
int __lesf2(float, float);
int __gesf2(float, float);
int __gtsf2(float, float);

int __aeabi_dcmpeq(double a, double b) { return __eqdf2(a, b) == 0; }
int __aeabi_dcmplt(double a, double b) { return __ltdf2(a, b) < 0; }
int __aeabi_dcmple(double a, double b) { return __ledf2(a, b) <= 0; }
int __aeabi_dcmpge(double a, double b) { return __gedf2(a, b) >= 0; }
int __aeabi_dcmpgt(double a, double b) { return __gtdf2(a, b) > 0; }
int __aeabi_fcmpeq(float a, float b) { return __eqsf2(a, b) == 0; }
int __aeabi_fcmplt(float a, float b) { return __ltsf2(a, b) < 0; }
int __aeabi_fcmple(float a, float b) { return __lesf2(a, b) <= 0; }
int __aeabi_fcmpge(float a, float b) { return __gesf2(a, b) >= 0; }
int __aeabi_fcmpgt(float a, float b) { return __gtsf2(a, b) > 0; }
