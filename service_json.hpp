#pragma once
#include <cstdlib>
#include <string_view>
#define PICOJSON_ASSERT(test) do { if (!(test)) std::abort(); } while (false)
#define PICOJSON_USE_LOCALE 0
#include "picojson.hpp"
namespace handheld {
inline bool serviceJson(std::string_view body,picojson::value& value) {
    if (body.empty() || body.size()>262144) return false;
    int depth=0;
    bool quoted=false, escape=false;
    for (char c:body) {
        if (quoted) {
            if (escape) escape=false;
            else if (c=='\\') escape=true;
            else if (c=='"') quoted=false;
        } else if (c=='"') quoted=true;
        else if (c=='{' || c=='[') { if (++depth>16) return false; }
        else if (c=='}' || c==']') { if (--depth<0) return false; }
    }
    if (depth || quoted) return false;
    std::string error;
    const auto end=picojson::parse(value,body.begin(),body.end(),&error);
    return error.empty() && body.substr(size_t(end-body.begin())).find_first_not_of(" \t\r\n")==std::string_view::npos;
}
}
