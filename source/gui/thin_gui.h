#pragma once
#include <functional>
#include <memory>
namespace MS2000 { class Ms2kRunner; }

// Thin test GUI: audio out and MIDI in device selection, the LCD, measured status.
// The runner must be init()-ed; this starts and stops it.
int run_thin_gui(MS2000::Ms2kRunner& runner, std::function<std::unique_ptr<MS2000::Ms2kRunner>()> makeRunner);   // PWR-SW-1: a power-on builds a new machine
