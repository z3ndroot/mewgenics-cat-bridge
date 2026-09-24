#pragma once

#include <charconv>
#include <cmath>
#include <string>
#include <string_view>
#include <vector>
#include <cstdint>
#include <cstdio>

// A tiny hand-rolled JSON *writer* (no parser -- we don't need one, since the
// mod only ever emits JSON; the Python side sends plain-text commands, see
// pipe_server.hpp / docs/DEVELOPMENT.md for the wire protocol).
//
// Deliberately minimal: this is a starting point for Claude Code to extend,
// not a general-purpose JSON library. If the schema grows a lot, swap this
// for a vendored single-header library (e.g. nlohmann::json) instead.

class JsonWriter {
public:
    JsonWriter() { buf.reserve(256); }

    JsonWriter& begin_object() { comma_if_needed(); buf += '{'; need_comma.push_back(false); return *this; }
    JsonWriter& end_object() { buf += '}'; pop_scope(); return *this; }

    JsonWriter& begin_array() { comma_if_needed(); buf += '['; need_comma.push_back(false); return *this; }
    JsonWriter& end_array() { buf += ']'; pop_scope(); return *this; }

    // Object key (call before a value)
    JsonWriter& key(std::string_view k) {
        comma_if_needed();
        buf += '"';
        escape_into(k);
        buf += "\":";
        // suppress the automatic comma logic for the upcoming value,
        // since the key+colon already separated it from the previous member
        if(!need_comma.empty()) need_comma.back() = false;
        return *this;
    }

    JsonWriter& value(std::string_view s) {
        comma_if_needed();
        buf += '"';
        escape_into(s);
        buf += '"';
        return *this;
    }

    JsonWriter& value(int64_t v) { comma_if_needed(); buf += std::to_string(v); return *this; }
    JsonWriter& value(uint64_t v) { comma_if_needed(); buf += std::to_string(v); return *this; }
    JsonWriter& value(int32_t v) { return value(static_cast<int64_t>(v)); }
    JsonWriter& value(uint32_t v) { return value(static_cast<uint64_t>(v)); }
    // Shortest round-trip form; NaN/inf aren't valid JSON, so they become null.
    JsonWriter& value(double v) {
        comma_if_needed();
        if(!std::isfinite(v)) {
            buf += "null";
            return *this;
        }
        char tmp[32];
        auto res = std::to_chars(tmp, tmp + sizeof(tmp), v);
        buf.append(tmp, res.ptr);
        return *this;
    }
    JsonWriter& value(bool v) { comma_if_needed(); buf += v ? "true" : "false"; return *this; }
    JsonWriter& null() { comma_if_needed(); buf += "null"; return *this; }

    // Convenience: key + string value in one call
    JsonWriter& kv(std::string_view k, std::string_view v) { key(k); return value(v); }
    JsonWriter& kv(std::string_view k, int64_t v) { key(k); return value(v); }
    JsonWriter& kv(std::string_view k, uint64_t v) { key(k); return value(v); }
    JsonWriter& kv(std::string_view k, int32_t v) { key(k); return value(v); }
    JsonWriter& kv(std::string_view k, uint32_t v) { key(k); return value(v); }
    JsonWriter& kv(std::string_view k, double v) { key(k); return value(v); }
    JsonWriter& kv(std::string_view k, bool v) { key(k); return value(v); }

    const std::string& str() const { return buf; }

private:
    std::string buf;
    std::vector<bool> need_comma; // one entry per open object/array scope

    void comma_if_needed() {
        if(!need_comma.empty()) {
            if(need_comma.back()) {
                buf += ',';
            }
            need_comma.back() = true;
        }
    }

    void pop_scope() {
        if(!need_comma.empty()) need_comma.pop_back();
        // whatever scope we just closed counts as "a value was written"
        // in its parent scope
        if(!need_comma.empty()) need_comma.back() = true;
    }

    void escape_into(std::string_view s) {
        for(char c : s) {
            switch(c) {
                case '"': buf += "\\\""; break;
                case '\\': buf += "\\\\"; break;
                case '\n': buf += "\\n"; break;
                case '\r': buf += "\\r"; break;
                case '\t': buf += "\\t"; break;
                default:
                    if(static_cast<unsigned char>(c) < 0x20) {
                        char hexbuf[8];
                        snprintf(hexbuf, sizeof(hexbuf), "\\u%04x", c);
                        buf += hexbuf;
                    } else {
                        buf += c;
                    }
            }
        }
    }
};
