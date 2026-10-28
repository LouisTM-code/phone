/* math.h for the ports (the fpdoom one is empty), functions in libm.c */
#ifndef SOFTFP_MATH_H
#define SOFTFP_MATH_H
#define M_PI 3.14159265358979323846
#define HUGE_VAL __builtin_huge_val()
double fabs(double x);
float fabsf(float x);
double floor(double x);
double ceil(double x);
float floorf(float x);
float roundf(float x);
double sqrt(double x);
float sqrtf(float x);
double sin(double x);
double cos(double x);
float sinf(float x);
float cosf(float x);
double atan(double x);
double atan2(double y, double x);
float acosf(float x);
double atof(const char *s);
#endif
