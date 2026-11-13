// What the panel shows, and how the API's daily response maps to it.
// No IDF headers: this compiles on the dev machine for testing.
#pragma once

#include "etl/expected.h"
#include "etl/string.h"
#include "etl/string_view.h"
#include "etl/to_string.h"

namespace dash {

enum class ScreenKind { status, message };

// A value type: the main task keeps one as its model and redraws from it.
struct Screen {
    ScreenKind kind{ScreenKind::message};
    etl::string<7> elapsed;  // "5m", "1h05", "23h"
    int missing{0};
    etl::string<32> line1;  // a message, or a Wi-Fi SSID (at most 32 bytes)
    etl::string<15> line2;  // "try 3"; empty for a one-line message

    static Screen message(etl::string_view line1, etl::string_view line2 = {})
    {
        Screen s;
        s.kind = ScreenKind::message;
        s.line1 = line1;
        s.line2 = line2;
        return s;
    }

    static Screen status(etl::string_view elapsed, int missing)
    {
        Screen s;
        s.kind = ScreenKind::status;
        s.elapsed = elapsed;
        s.missing = missing;
        return s;
    }
};

// Why a fetch produced no status. text() is the error line for the panel.
struct FetchError {
    enum class Kind { no_network, bad_token, server, bad_response };

    Kind kind{Kind::bad_response};
    int http_status{0};  // only for Kind::server

    etl::string<15> text() const
    {
        switch (kind) {
        case Kind::no_network:
            return etl::string<15>{"no network"};
        case Kind::bad_token:
            return etl::string<15>{"bad token"};
        case Kind::server: {
            etl::string<15> s{"server "};
            etl::to_string(http_status, s, true);
            return s;
        }
        case Kind::bad_response:
            break;
        }
        return etl::string<15>{"bad response"};
    }
};

// "5m" under an hour, "1h05" under three, then whole hours "3h" (see the
// sitting-status spec, "Elapsed time format").
etl::string<7> format_elapsed(int minutes);

// Maps a /api/v1/daily body to the status screen, or "no wakeup" when stats is
// null. Fails with Kind::bad_response when the body is not JSON or lacks the
// expected stats fields.
etl::expected<Screen, FetchError> screen_from_daily(etl::string_view body);

}  // namespace dash
