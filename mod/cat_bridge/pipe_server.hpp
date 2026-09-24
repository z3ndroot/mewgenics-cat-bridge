#pragma once

#include <string>
#include <deque>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <thread>

// Named pipe bridge between the injected DLL and the outside world (our
// Python MCP server).
//
// Design notes / why it's shaped this way:
//  - Mewgenics' internal data structures (Scene::Entities, ComponentLists,
//    etc.) are NOT thread-safe. Reading/writing them from a random background
//    thread while the game's own thread is simultaneously mutating them
//    (e.g. mid-frame, cats leveling up, items changing) is a recipe for an
//    intermittent crash.
//  - So: this class only owns the named pipe I/O (blocking accept/read/write
//    on its own background thread). It never touches game memory directly.
//  - Actual game-memory work happens once per frame, on the game's own
//    thread, inside the glaiel__MewDirector__always_update hook in
//    cat_bridge.cpp -- exactly mirroring the pattern the upstream
//    randomize_item_picks mod uses for its own button-click queue.
//  - One request is in flight at a time (simple, and matches upstream's
//    "at most one action per frame" caution). Good enough for a personal
//    tool; if you want concurrent clients/requests, that's a good next
//    step for Claude Code to build once the basics work.
//
// Wire protocol (deliberately NOT full JSON on the way in, to avoid needing
// a JSON *parser* in C++ -- only a JSON *writer*, see json_writer.hpp):
//   Client -> Server: one line of UTF-8 text, newline-terminated, e.g.
//       LIST_CATS
//       GET_CAT 123456789
//       SET_STAT 123456789 str 5
//       SET_HP 123456789 40
//   Server -> Client: one line of JSON, newline-terminated, e.g.
//       {"ok":true,"cats":[...]}
//       {"ok":false,"error":"cat not found"}
//
// See docs/DEVELOPMENT.md for the full command list this MVP implements and ideas
// for extending it.

class PipeServer {
public:
    // pipe_name example: "cat_bridge" -> \\.\pipe\cat_bridge
    explicit PipeServer(std::string pipe_name);
    ~PipeServer();

    void start();
    // Stops the server thread and joins it. Must NOT be called from DllMain
    // (the exiting thread needs the loader lock, so joining under it
    // deadlocks) -- use CatBridgeShutdown (exported) for that.
    void stop();
    // Non-blocking stop for DllMain: signals the thread and detaches it.
    // Only safe on process exit (other threads are already gone), or after
    // stop() has already run.
    void abandon();

    // Called from the game thread (inside the always_update hook).
    // Returns true and fills `out_request` if a request is waiting.
    bool try_pop_request(std::string &out_request);

    // Called from the game thread once a request has been handled.
    void push_response(std::string response);

private:
    void server_thread_main();

    std::string pipe_name;
    std::atomic<bool> running{false};
    std::thread thread;

    std::mutex mtx;
    std::condition_variable cv;
    std::deque<std::string> pending_requests;
    std::deque<std::string> pending_responses;
    // Set when a request timed out while the game thread was mid-handling
    // it, so its late response isn't handed to the next client.
    bool drop_next_response = false;

    void nudge();
};
