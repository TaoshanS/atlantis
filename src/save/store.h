// Directory based save store.
//
//   <root>/settings.json
//   <root>/profiles/<profile-id>/profile.json
//   <root>/profiles/<profile-id>/slot<N>.json     (N = 0 autosave, 1..kManualSlots manual)
//
// Every file is {"format","version","crc32","payload"} and is written atomically (temp file +
// rename), keeping the previous version as ".bak" so one bad write never loses a save.
#pragma once
#include <filesystem>
#include <string>
#include <vector>

#include "save_data.h"

namespace sbso::save {

class SaveStore {
public:
    explicit SaveStore(std::filesystem::path root);

    bool load_settings(Settings* out, std::string* err);  // a missing file yields defaults
    bool save_settings(const Settings& s, std::string* err);

    std::vector<ProfileInfo> list_profiles();
    bool create_profile(const std::string& name, const std::vector<DefaultCard>& defaults, ProfileInfo* out, std::string* err);
    bool rename_profile(const std::string& id, const std::string& name, std::string* err);
    bool delete_profile(const std::string& id, std::string* err);
    bool set_last_slot(const std::string& id, int slot, std::string* err);

    std::vector<SlotInfo> list_slots(const std::string& profile_id);
    bool load_slot(const std::string& profile_id, int slot, Slot* out, std::string* err);
    bool save_slot(const std::string& profile_id, int slot, const Slot& s, std::string* err);
    bool delete_slot(const std::string& profile_id, int slot, std::string* err);
    bool copy_slot(const std::string& profile_id, int from, int to, std::string* err);

    static bool valid_name(const std::string& name);  // 1..15 UTF-8 characters, no control characters
    static bool valid_id(const std::string& id);

private:
    std::filesystem::path profile_dir(const std::string& id) const { return root_ / "profiles" / id; }
    std::filesystem::path slot_path(const std::string& id, int slot) const;
    std::filesystem::path root_;
};

// Exposed for tests and tools.
std::uint32_t crc32(const std::uint8_t* data, size_t n);
std::string base64_encode(const std::vector<std::uint8_t>& data);
bool base64_decode(const std::string& s, std::vector<std::uint8_t>* out);

}  // namespace sbso::save
