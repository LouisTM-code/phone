/* The math functions the ports use, on top of the soft float: plain
   series and Newton steps, accurate enough for drawing and game logic. */
#include <math.h>

#define PI 3.14159265358979323846

double fabs(double x) { return x < 0 ? -x : x; }
float fabsf(float x) { return x < 0 ? -x : x; }

double floor(double x) {
	double t;
	if (x >= 4503599627370496.0 || x <= -4503599627370496.0) return x;
	t = (double)(long long)x;
	return t > x ? t - 1 : t;
}
double ceil(double x) { return -floor(-x); }
float floorf(float x) { return floor(x); }
float roundf(float x) { return x < 0 ? -floor(-x + 0.5) : floor(x + 0.5); }

double sqrt(double x) {
	double r;
	int i;
	if (x <= 0) return 0;
	/* start from the exponent halved */
	r = x;
	{
		union { double d; unsigned long long u; } v = { x };
		v.u = (v.u >> 1) + (0x3ffull << 51);
		r = v.d;
	}
	for (i = 0; i < 6; i++) r = 0.5 * (r + x / r);
	return r;
}
float sqrtf(float x) { return sqrt(x); }

/* sin on [-pi/4, pi/4] and cos likewise, by Taylor series */
static double sin_k(double x) {
	double x2 = x * x;
	return x * (1 - x2 / 6 * (1 - x2 / 20 * (1 - x2 / 42 * (1 - x2 / 72 * (1 - x2 / 110)))));
}
static double cos_k(double x) {
	double x2 = x * x;
	return 1 - x2 / 2 * (1 - x2 / 12 * (1 - x2 / 30 * (1 - x2 / 56 * (1 - x2 / 90))));
}

double sin(double x) {
	double q = floor(x / (PI / 2) + 0.5);
	double r = x - q * (PI / 2);
	switch ((long long)q & 3) {
	case 0: return sin_k(r);
	case 1: return cos_k(r);
	case 2: return -sin_k(r);
	default: return -cos_k(r);
	}
}
double cos(double x) { return sin(x + PI / 2); }
float sinf(float x) { return sin(x); }
float cosf(float x) { return cos(x); }

/* atan on [0, 1]: halve the angle, atan(x) = 2 atan(x / (1 + sqrt(1 + x^2))),
   then the series to x^11 */
static double atan_k(double x) {
	double x2;
	x = x / (1 + sqrt(1 + x * x));
	x2 = x * x;
	return 2 * x * (1 - x2 * (1.0 / 3 - x2 * (1.0 / 5 - x2 * (1.0 / 7 - x2 * (1.0 / 9 - x2 / 11)))));
}

double atan(double x) {
	if (x < 0) return -atan(-x);
	if (x > 1) return PI / 2 - atan_k(1 / x);
	return atan_k(x);
}

double atan2(double y, double x) {
	if (x > 0) return atan(y / x);
	if (x < 0) return y >= 0 ? atan(y / x) + PI : atan(y / x) - PI;
	if (y > 0) return PI / 2;
	if (y < 0) return -PI / 2;
	return 0;
}

float acosf(float x) {
	if (x <= -1) return PI;
	if (x >= 1) return 0;
	return atan2(sqrt(1 - (double)x * x), x);
}

double atof(const char *s) {
	double v = 0, f = 1;
	int neg = 0;
	while (*s == ' ' || *s == '\t') s++;
	if (*s == '-' || *s == '+') neg = *s++ == '-';
	while (*s >= '0' && *s <= '9') v = v * 10 + (*s++ - '0');
	if (*s == '.')
		for (s++; *s >= '0' && *s <= '9'; s++) v += (*s - '0') * (f /= 10);
	return neg ? -v : v;
}
