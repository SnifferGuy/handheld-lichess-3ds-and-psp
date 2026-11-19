#pragma once
#include "screen.hpp"
#include <cstdio>

namespace handheld {
enum class ThemeId { Dark, Light };
struct Theme {
    Ink background, surface, text, secondary, border, focus, accent, heading;
    Ink active, waiting, danger;
};
constexpr Ink primary=rgba(129,182,76), primaryText=rgba(30,35,24);
constexpr Ink boardLight=rgba(235,236,208), boardDark=rgba(115,149,82);
constexpr Theme darkTheme{
    rgba(48,46,43),rgba(38,37,34),rgba(245,245,242),rgba(189,187,183),
    rgba(86,83,77),rgba(57,74,42),primary,rgba(176,219,136),
    rgba(175,225,135),rgba(255,163,153),rgba(224,96,86)
};
constexpr Theme lightTheme{
    rgba(245,243,239),rgba(255,255,255),rgba(38,37,34),rgba(102,99,94),
    rgba(207,204,195),rgba(219,235,201),rgba(61,105,34),rgba(49,94,29),
    rgba(50,101,34),rgba(156,43,39),rgba(176,50,40)
};
constexpr const Theme& palette(ThemeId id) {
    return id==ThemeId::Light?lightTheme:darkTheme;
}
inline ThemeId loadTheme(const char* path) {
    if (!path) return ThemeId::Dark;
    auto* file=std::fopen(path,"rb");
    if (!file) return ThemeId::Dark;
    char bytes[6];
    const auto length=std::fread(bytes,1,sizeof(bytes),file);
    std::fclose(file);
    return std::string_view(bytes,length)=="light"?ThemeId::Light:ThemeId::Dark;
}
inline bool saveTheme(const char* path,ThemeId theme) {
    if (!path) return true;
    auto* file=std::fopen(path,"wb");
    if (!file) return false;
    const bool wrote=std::fputs(theme==ThemeId::Light?"light":"dark",file)>=0;
    return std::fclose(file)==0 && wrote;
}
}
