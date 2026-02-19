#include "ui/ui_types.h"
#include <cstdlib>

namespace UI {
namespace Modern {

Variable::Variable(const P::String& n, const P::String& t, const P::String& def)
    : name(n), type(t), default_val(def) {}

Timer::Timer(int ms, const P::String& cb)
    : interval_ms(ms), callback(cb) {}

StyleProperty::StyleProperty(const P::String& n, const P::String& v)
    : name(n), value(v) {}

int StyleProperty::toInt() const {
    return std::atoi(value.c_str());
}

uint32_t StyleProperty::toColor() const {
    if (value.empty()) return 0;
    const char* s = value.c_str();
    if (s[0] == '#') s++;
    return static_cast<uint32_t>(std::strtoul(s, nullptr, 16));
}

void Style::addProperty(const P::String& name, const P::String& value) {
    properties.emplace_back(name, value);
}

} // namespace Modern
} // namespace UI
