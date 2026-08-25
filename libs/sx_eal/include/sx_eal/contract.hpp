#pragma once
// Sigmaxx Engine Abstraction Layer - bridge contract v0 (placeholder).
// Host <-> engine workers will exchange FlatBuffers messages over shared-memory
// SPSC rings using this protocol version. Frozen in P0, extended only backwards-
// compatibly until P1 exit.

#include <cstdint>
#include <flatbuffers/flatbuffers.h>

namespace sx_eal {

inline constexpr std::uint32_t kBridgeProtocolVersion = 0;
inline constexpr const char*   kProductName           = "Sigmaxx";

} // namespace sx_eal

static_assert(FLATBUFFERS_VERSION_MAJOR >= 23,
              "Sigmaxx requires FlatBuffers >= 23.x");
