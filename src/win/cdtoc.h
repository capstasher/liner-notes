#pragma once
#include "../core/discid.h"
#include <string>

namespace ln {
// Reads the TOC straight from the drive ("D:") with IOCTL_CDROM_READ_TOC.
// Needs no elevation; only GENERIC_READ on the volume.
bool readDiscToc(const std::wstring& drive, DiscToc& out, std::string& error);
}
