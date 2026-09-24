#include "pipe_server.hpp"
#include "utilities/debug_console.hpp"

#include <windows.h>

#include <chrono>

PipeServer::PipeServer(std::string pipe_name_) : pipe_name(std::move(pipe_name_)) {}

PipeServer::~PipeServer() {
    stop();
}

void PipeServer::start() {
    if(running.exchange(true)) {
        return; // already running
    }
    thread = std::thread(&PipeServer::server_thread_main, this);
}

// Unblock a ConnectNamedPipe/ReadFile by connecting to ourselves briefly.
void PipeServer::nudge() {
    std::string full_name = R"(\\.\pipe\)" + pipe_name;
    HANDLE h = CreateFileA(full_name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if(h != INVALID_HANDLE_VALUE) {
        CloseHandle(h);
    }
}

void PipeServer::stop() {
    if(running.exchange(false)) {
        nudge();
    }
    if(thread.joinable()) {
        thread.join();
    }
}

void PipeServer::abandon() {
    if(running.exchange(false)) {
        nudge();
    }
    if(thread.joinable()) {
        thread.detach();
    }
}

bool PipeServer::try_pop_request(std::string &out_request) {
    std::lock_guard<std::mutex> lock(mtx);
    if(pending_requests.empty()) {
        return false;
    }
    out_request = std::move(pending_requests.front());
    pending_requests.pop_front();
    return true;
}

void PipeServer::push_response(std::string response) {
    {
        std::lock_guard<std::mutex> lock(mtx);
        if(drop_next_response) {
            drop_next_response = false;
            return;
        }
        pending_responses.push_back(std::move(response));
    }
    cv.notify_all();
}

void PipeServer::server_thread_main() {
    std::string full_name = R"(\\.\pipe\)" + pipe_name;

    D::info("cat_bridge: pipe server starting on {}", full_name);

    while(running.load()) {
        HANDLE pipe = CreateNamedPipeA(
            full_name.c_str(),
            PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
            1,          // max instances -- one client at a time for this MVP
            4096, 4096, // out/in buffer size
            0,          // default timeout
            nullptr
        );
        if(pipe == INVALID_HANDLE_VALUE) {
            D::error("cat_bridge: CreateNamedPipeA failed, gle={}", GetLastError());
            std::this_thread::sleep_for(std::chrono::seconds(1));
            continue;
        }

        BOOL connected = ConnectNamedPipe(pipe, nullptr) ? TRUE : (GetLastError() == ERROR_PIPE_CONNECTED);
        if(!running.load()) {
            CloseHandle(pipe);
            break;
        }
        if(!connected) {
            CloseHandle(pipe);
            continue;
        }

        // Read one newline-terminated command line.
        std::string line;
        char buf[512];
        bool got_line = false;
        while(!got_line) {
            DWORD bytes_read = 0;
            BOOL ok = ReadFile(pipe, buf, sizeof(buf), &bytes_read, nullptr);
            if(!ok || bytes_read == 0) {
                break; // client disconnected mid-read
            }
            for(DWORD i = 0; i < bytes_read; i++) {
                if(buf[i] == '\n') {
                    got_line = true;
                    break;
                }
                if(buf[i] != '\r') {
                    line += buf[i];
                }
            }
        }

        if(got_line) {
            // Hand the request to the game thread and wait for a response.
            {
                std::lock_guard<std::mutex> lock(mtx);
                pending_requests.push_back(line);
            }

            std::string response;
            {
                std::unique_lock<std::mutex> lock(mtx);
                // 2s timeout: if the game is paused on a blocking menu (see
                // cat_bridge.cpp's YesNoPrompt/PauseMenu guards) a request
                // may legitimately need to wait a little.
                bool got_response = cv.wait_for(lock, std::chrono::seconds(2), [&] {
                    return !pending_responses.empty();
                });
                if(got_response) {
                    response = std::move(pending_responses.front());
                    pending_responses.pop_front();
                } else {
                    // Only one request is ever in flight: if it's still queued
                    // the game thread never saw it, otherwise it's mid-handling
                    // and its eventual response must be discarded.
                    if(!pending_requests.empty()) {
                        pending_requests.clear();
                    } else {
                        drop_next_response = true;
                    }
                    response = R"({"ok":false,"error":"timed out waiting for game thread"})";
                }
            }

            response += '\n';
            DWORD bytes_written = 0;
            WriteFile(pipe, response.data(), static_cast<DWORD>(response.size()), &bytes_written, nullptr);
        }

        FlushFileBuffers(pipe);
        DisconnectNamedPipe(pipe);
        CloseHandle(pipe);
    }

    D::info("cat_bridge: pipe server stopped");
}
