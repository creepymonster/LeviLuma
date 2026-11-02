#pragma once
// Kalibrierung per RBF (Thin-Plate-Spline) ueber 17 Messpunkte.
// Methode und Stuetzpunkte aus dem LED-Calibrator (Normalisierung dunkler Farben,
// Umschaltung bei 30..70 % Helligkeit). Ohne Arduino-Abhaengigkeit, damit testbar.
#include <stdint.h>
#include <string.h>
#include <math.h>

namespace LeviCal {

static const int N = 17;

// Zielfarben (sRGB), Reihenfolge fix. Die Firmware speichert pro Zielfarbe die
// Ansteuerung (R,G,B), mit der die LED dieser Farbe entspricht.
static const uint8_t TARGET[N][3] = {
    {255,255,255}, {255,243,224}, {255,228,189}, {255,197,143}, {255,180,117},   // Weiss (Planck)
    {255,64,64}, {255,255,64}, {64,255,64}, {64,255,255}, {64,64,255}, {255,64,255}, // 75 %
    {255,128,64}, {0,255,128}, {0,128,255}, {128,0,255}, {255,0,128}, {0,192,255}   // 100 %
};

struct Model {
    uint8_t pts[N][3];   // Ansteuerung je Zielfarbe (0..255)
    double  w[3][N + 4]; // RBF-Gewichte je Ausgangskanal
};

inline double phi(double r) { return r < 1e-10 ? 0.0 : r * r * log(r); }

inline double dist3(const double *a, const double *b) {
    double d0 = a[0] - b[0], d1 = a[1] - b[1], d2 = a[2] - b[2];
    return sqrt(d0 * d0 + d1 * d1 + d2 * d2);
}

// Gewichte aus den Messpunkten berechnen (einmal bei Aenderung).
inline void solve(Model &m) {
    const int M = N + 4;
    static double A[M][M];
    static double B[M][3];
    double X[N][3];
    for (int i = 0; i < N; i++)
        for (int c = 0; c < 3; c++) X[i][c] = TARGET[i][c] / 255.0;

    for (int i = 0; i < M; i++) {
        for (int j = 0; j < M; j++) A[i][j] = 0;
        for (int c = 0; c < 3; c++) B[i][c] = 0;
    }
    for (int i = 0; i < N; i++) {
        for (int j = 0; j < N; j++) A[i][j] = phi(dist3(X[i], X[j]));
        A[i][N] = 1; A[i][N + 1] = X[i][0]; A[i][N + 2] = X[i][1]; A[i][N + 3] = X[i][2];
        A[N][i] = 1; A[N + 1][i] = X[i][0]; A[N + 2][i] = X[i][1]; A[N + 3][i] = X[i][2];
        for (int c = 0; c < 3; c++) B[i][c] = m.pts[i][c] / 255.0;
    }

    // Gauss mit Teilpivotisierung, alle drei rechten Seiten zugleich
    for (int c = 0; c < M; c++) {
        int p = c;
        for (int r = c + 1; r < M; r++) if (fabs(A[r][c]) > fabs(A[p][c])) p = r;
        if (p != c) {
            for (int j = 0; j < M; j++) { double t = A[c][j]; A[c][j] = A[p][j]; A[p][j] = t; }
            for (int k = 0; k < 3; k++) { double t = B[c][k]; B[c][k] = B[p][k]; B[p][k] = t; }
        }
        double d = A[c][c];
        if (fabs(d) < 1e-12) continue;
        for (int r = c + 1; r < M; r++) {
            double f = A[r][c] / d;
            if (f == 0) continue;
            for (int j = c; j < M; j++) A[r][j] -= f * A[c][j];
            for (int k = 0; k < 3; k++) B[r][k] -= f * B[c][k];
        }
    }
    // Rueckeinsetzen
    for (int k = 0; k < 3; k++) {
        double x[M];
        for (int r = M - 1; r >= 0; r--) {
            double s = B[r][k];
            for (int j = r + 1; j < M; j++) s -= A[r][j] * x[j];
            x[r] = fabs(A[r][r]) < 1e-12 ? 0.0 : s / A[r][r];
        }
        for (int i = 0; i < M; i++) m.w[k][i] = x[i];
    }
}

// Auswertung der RBF an Punkt q (Eingang und Ausgang in 0..1).
inline void eval(const Model &m, const double q[3], double out[3]) {
    for (int k = 0; k < 3; k++) {
        double v = m.w[k][N] + m.w[k][N + 1] * q[0] + m.w[k][N + 2] * q[1] + m.w[k][N + 3] * q[2];
        for (int i = 0; i < N; i++) {
            double xi[3] = {TARGET[i][0] / 255.0, TARGET[i][1] / 255.0, TARGET[i][2] / 255.0};
            v += m.w[k][i] * phi(dist3(q, xi));
        }
        out[k] = v;
    }
}

// Kalibrierte Ansteuerung fuer die gewuenschte Farbe (ohne Helligkeit, die macht der Controller).
inline void apply(const Model &m, uint8_t r, uint8_t g, uint8_t b, uint8_t out[3]) {
    double rr = r / 255.0, gg = g / 255.0, bb = b / 255.0;
    double v = fmax(rr, fmax(gg, bb));
    if (v <= 0) { out[0] = out[1] = out[2] = 0; return; }
    double qn[3] = {rr / v, gg / v, bb / v};   // dunkle Farben auf volle Helligkeit normalisieren
    double qd[3] = {rr, gg, bb};
    double nrm[3], dir[3];
    eval(m, qn, nrm);
    eval(m, qd, dir);
    double x = (v - 0.3) / 0.4;                 // weiche Umschaltung 30..70 %
    if (x < 0) x = 0;
    if (x > 1) x = 1;
    double t = x * x * (3 - 2 * x);
    for (int k = 0; k < 3; k++) {
        double val = nrm[k] * v * (1 - t) + dir[k] * t;
        if (val < 0) val = 0;
        if (val > 1) val = 1;
        out[k] = (uint8_t)(val * 255.0 + 0.5);
    }
}

// Standardpunkte: lineare Kanal-Gains (Standard = FastLED TypicalSMD5050).
// Eine lineare Abbildung wird von der RBF exakt wiedergegeben.
inline void defaultPts(Model &m, float gr, float gg, float gb) {
    const float g[3] = {gr, gg, gb};
    for (int i = 0; i < N; i++)
        for (int c = 0; c < 3; c++) {
            float v = TARGET[i][c] * g[c];
            if (v < 0) v = 0;
            if (v > 255) v = 255;
            m.pts[i][c] = (uint8_t)(v + 0.5f);
        }
}

} // namespace LeviCal
