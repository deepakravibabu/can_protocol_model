#ifndef CAN_TYPES_H
#define CAN_TYPES_H

#include <cstdint>
#include <cstring>
#include <ostream>
#include <systemc.h>

/// @brief Simplified classic-CAN DATA frame element - the fields a Tx
/// buffer element actually holds (RM0433 Table 493), restricted to
/// classic CAN (no FDF/BRS/ESI yet). rtr is kept as a field for future
/// REMOTE-frame support, unused for now.
///
/// @note Plain public data aggregate, not given the m_ prefix used on
/// encapsulated class state elsewhere in this project - it's closer to
/// a hardware register layout (meant to be read/written directly) than
/// to an object with invariants to protect.
struct CanDataFrame {
    std::uint32_t id;      ///< 11-bit standard or 29-bit extended identifier.
    bool          ide;     ///< false = standard (11-bit), true = extended (29-bit).
    bool          rtr;     ///< false = data frame, true = remote frame (unused for now).
    std::uint8_t  dlc;     ///< 0-8 data bytes (classic CAN only for now).
    std::uint8_t  data[8];

    CanDataFrame() : id(0U), ide(false), rtr(false), dlc(0U) {
        std::memset(data, 0, sizeof(data));
    }

    bool operator==(const CanDataFrame& rhs) const {
        return (id == rhs.id) && (ide == rhs.ide) && (rtr == rhs.rtr) &&
            (dlc == rhs.dlc) && (std::memcmp(data, rhs.data, dlc) == 0);
    }
    bool operator!=(const CanDataFrame& rhs) const { return !(*this == rhs); }
};

inline std::ostream& operator<<(std::ostream& os, const CanDataFrame& f) {
    os << "ID=0x" << std::hex << f.id << std::dec
        << " IDE=" << f.ide << " RTR=" << f.rtr
        << " DLC=" << static_cast<int>(f.dlc) << " DATA=[";
    for (unsigned i = 0U; i < f.dlc; ++i) {
        os << std::hex << static_cast<int>(f.data[i]) << std::dec;
        if ((i + 1U) < f.dlc) { os << " "; }
    }
    os << "]";
    return os;
}

// Required by SystemC whenever a user type is carried on an sc_signal<>
// or traced with sc_trace().
inline void sc_trace(sc_core::sc_trace_file* tf, const CanDataFrame& f, const std::string& name) {
    sc_trace(tf, f.id, name + ".id");
    sc_trace(tf, f.dlc, name + ".dlc");
}

/// @brief Reasons CanCore may refuse to generate/transmit a selected
/// message. Exists so rejection is diagnosable (by a testbench, or
/// eventually by software polling protocol status) rather than only
/// observable as "no frame appeared on the bus".
enum class CanRejectReason : std::uint8_t {
    NONE = 0,          ///< Not rejected.
    INIT_MODE,         ///< FDCAN_CCCR.INIT is active; no transmission permitted.
    ID_OUT_OF_RANGE,   ///< id exceeds 11 bits (standard) or 29 bits (extended).
    INVALID_DLC        ///< dlc exceeds the classic-CAN maximum of 8 this phase.
};

inline const char* toString(CanRejectReason reason) {
    switch (reason) {
    case CanRejectReason::NONE:            return "NONE";
    case CanRejectReason::INIT_MODE:       return "INIT_MODE";
    case CanRejectReason::ID_OUT_OF_RANGE: return "ID_OUT_OF_RANGE";
    case CanRejectReason::INVALID_DLC:     return "INVALID_DLC";
    default:                               return "UNKNOWN";
    }
}

#endif // CAN_TYPES_H