#pragma once

// Enable stopwatch measurement and lap printing
// #define ENABLE_STOPWATCHES

#include "utilities/debug_console.hpp"

#include <chrono>

// Stopwatch and performance measurement utilities.
//
// polymeric 2026

#ifdef ENABLE_STOPWATCHES
#define MAKE_STOPWATCH_SCOPE(var, label) ScopeTimer var(label)
#define MAKE_STOPWATCH_CHECKPOINT(var, label) CheckpointTimer var(label)
#define STOPWATCH_CHECKPOINT_LAP(var, checkpoint_name) var.lap(checkpoint_name)
#else
#define MAKE_STOPWATCH_SCOPE(var, label)
#define MAKE_STOPWATCH_CHECKPOINT(var, label)
#define STOPWATCH_CHECKPOINT_LAP(var, checkpoint_name)
#endif

class ScopeTimer {
public:
    ScopeTimer(std::string label) {
        this->label_ = label;
        this->start_ = std::chrono::steady_clock::now();
    }

    ~ScopeTimer() {
        std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
        std::chrono::duration duration = std::chrono::duration_cast<std::chrono::nanoseconds>(now - this->start_);
        D::debug("scope {}: took {}", this->label_, duration);
    }
private:
    std::string label_;
    std::chrono::steady_clock::time_point start_;
};

class CheckpointTimer {
public:
    CheckpointTimer(std::string label) {
        this->label_ = label;
        this->last_checkpoint_name_ = "start";
        this->last_ = std::chrono::steady_clock::now();
    }

    void lap(std::string checkpoint_name) {
        std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
        std::chrono::duration duration = std::chrono::duration_cast<std::chrono::nanoseconds>(now - this->last_);
        D::debug("checkpoint {} ({} -> {}): took {}", this->label_, this->last_checkpoint_name_, checkpoint_name, duration);
        this->last_checkpoint_name_ = checkpoint_name;
        this->last_ = now;
    }

private:
    std::string label_;
    std::string last_checkpoint_name_;
    std::chrono::steady_clock::time_point last_;
};

#undef ENABLE_STOPWATCHES
