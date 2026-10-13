// Minimal portable SHA-1 (used only for the MusicBrainz Disc ID).
#pragma once
#include <cstdint>
#include <string>

namespace ln {
// Returns the 20-byte raw digest of `data`.
std::string sha1(const std::string& data);
}
