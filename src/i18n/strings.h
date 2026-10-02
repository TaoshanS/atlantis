// Localized string tables: flat {"key": "text"} JSON per language, English as the fallback.
// Templates use {name}-style placeholders filled by format().
#pragma once
#include <initializer_list>
#include <map>
#include <string>
#include <utility>

namespace sbso::i18n {

class Strings {
public:
    // Loads <dir>/<lang>.json over the (already loaded) English table; returns false if the file is missing/invalid.
    bool load_language(const std::string& dir, const std::string& lang);
    void set(const std::string& key, const std::string& text) { table_[key] = text; }
    void clear() { table_.clear(); lang_ = "en"; }
    // `fallback` is the original English text hard-coded in the calling screen.
    const std::string& tr(const std::string& key, const std::string& fallback) const;
    static std::string format(const std::string& tmpl, std::initializer_list<std::pair<std::string, std::string>> args);
    const std::string& language() const { return lang_; }

    // "es-ES"/"es_ES"/"es" -> "es-ES"; anything else supported -> "en". `system_locale` e.g. from SDL_GetPreferredLocales.
    static std::string resolve(const std::string& setting, const std::string& system_locale);

private:
    std::map<std::string, std::string> table_;
    std::string lang_ = "en";
};

}  // namespace sbso::i18n
