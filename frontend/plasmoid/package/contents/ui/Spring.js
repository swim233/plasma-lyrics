.pragma library

// The under-damped second-order step response both springs of this widget
// share: a word's lift (DESIGN.md decision 73, LyricLine) and a line moving
// into place on a line switch (decision 28, AnimatedLyric). Parameterised
// the way both decisions state them -- how long to the first peak, and how
// far that peak overshoots -- and a pure function of time, so a spring can be
// evaluated at any moment without state of its own.

// ζ from the overshoot, os = exp(-ζπ/√(1-ζ²)). The floor keeps the system
// under-damped so the closed form stays finite; the overshoot 0.1% implies
// is invisible.
function damping(overshoot) {
    const l = Math.log(Math.max(0.001, Math.min(0.9, overshoot)));
    return -l / Math.sqrt(Math.PI * Math.PI + l * l);
}

function dampingRoot(zeta) {
    return Math.sqrt(1 - zeta * zeta);
}

// The natural frequency (rad/s) that puts the first peak at peakMs:
// t_peak = π / (ω·√(1-ζ²)).
function omega(peakMs, zeta) {
    return Math.PI / (peakMs / 1000 * dampingRoot(zeta));
}

// How long until the oscillation stays within 1% of rest: its amplitude is
// bounded by e^(-ζωt)/√(1-ζ²).
function settleMs(zeta, omega) {
    return 1000 * Math.log(100 / dampingRoot(zeta)) / (zeta * omega);
}

// Unit step response at tauMs, 0 before it starts.
function step(tauMs, zeta, omega) {
    if (tauMs <= 0) {
        return 0;
    }
    const tau = tauMs / 1000;
    const q = dampingRoot(zeta);
    return 1 - Math.exp(-zeta * omega * tau)
        * (Math.cos(omega * q * tau) + (zeta / q) * Math.sin(omega * q * tau));
}
