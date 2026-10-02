#include "strings.h"
#include "core/vfs.h"

#include <fstream>

#include "json.hpp"

namespace sbso::i18n {

bool Strings::load_language(const std::string& dir, const std::string& lang) {
    std::string text;
    if (!vfs::read_text(dir + "/" + lang + ".json", &text)) return false;
    nlohmann::json j = nlohmann::json::parse(text, nullptr, false);
    if (j.is_discarded() || !j.is_object()) return false;
    for (auto it = j.begin(); it != j.end(); ++it)
        if (it.value().is_string()) table_[it.key()] = it.value().get<std::string>();
    lang_ = lang;
    return true;
}

const std::string& Strings::tr(const std::string& key, const std::string& fallback) const {
    auto it = table_.find(key);
    return it == table_.end() ? fallback : it->second;
}

std::string Strings::format(const std::string& tmpl, std::initializer_list<std::pair<std::string, std::string>> args) {
    std::string out = tmpl;
    for (const auto& [k, v] : args) {
        std::string ph = "{" + k + "}";
        for (size_t p = 0; (p = out.find(ph, p)) != std::string::npos; p += v.size()) out.replace(p, ph.size(), v);
    }
    return out;
}

std::string Strings::resolve(const std::string& setting, const std::string& system_locale) {
    auto norm = [](std::string s) {
        for (char& c : s) if (c == '_') c = '-';
        if (s.rfind("es", 0) == 0) return std::string("es-ES");
        return std::string("en");
    };
    if (!setting.empty()) return norm(setting);
    return norm(system_locale);
}

}  // namespace sbso::i18n
