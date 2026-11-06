#pragma once

#include <cstdint>

namespace sid
{
// Firmware init patches, one per engine (512 bytes each).
extern const uint8_t kInit_lead[512];
extern const uint8_t kInit_bassline[512];
extern const uint8_t kInit_drum[512];
extern const uint8_t kInit_multi[512];
} // namespace sid
