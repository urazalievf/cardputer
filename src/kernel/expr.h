// Arithmetic expression evaluation, split out of the Calc app so it can be
// exercised by the self-test instead of only by hand on the device.
#pragma once
#include <Arduino.h>

namespace expr {

struct Options {
    bool degrees = false;
    double ans = 0;
};

// Returns false and fills `err` on a malformed expression.
bool eval(const String& input, double& out, String& err, const Options& opt = Options());

// Human formatting: integers stay integral, long tails get trimmed.
String format(double v);

}  // namespace expr
