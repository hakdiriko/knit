// Who each of the cut's neurons is in FlyWire's 783 release (generated: tools/rig/identities.py).
// Indexed like FlyData: neuron n of the plug-in is rootId[n] in FlyWire.
#pragma once

#include <cstdint>

namespace dendrite::rig::identities
{
constexpr int count = 11705;
extern const uint64_t rootId[count];
extern const uint16_t type[count];       // into typeName ("" when FlyWire has none)
extern const uint8_t transmitter[count]; // into transmitterName (FlyWire's predicted top transmitter)
extern const uint8_t superClass[count];  // into className
extern const uint8_t side[count];        // into sideName
extern const char* const typeName[];
extern const char* const transmitterName[];
extern const char* const className[];
extern const char* const sideName[];
} // namespace dendrite::rig::identities
