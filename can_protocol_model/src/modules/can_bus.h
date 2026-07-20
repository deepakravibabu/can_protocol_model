#ifndef CAN_BUS_H
#define CAN_BUS_H

#include <systemc.h>
#include "can_types.h"

/// @brief Minimal broadcast medium carrying a plain CanDataFrame - not
/// bit-accurate: no stuffing, no wired-AND, no arbitration between
/// simultaneous senders (single-sender phase). A single sender drives
/// a frame, and any number of receiver processes sensitive to
/// m_frameValidEvent observe it at the same simulated time.
class CanBus : public sc_core::sc_module {
public:
    sc_core::sc_event m_frameValidEvent;

    SC_CTOR(CanBus) {}

    /// @brief Called by CanCore once a message has been validated.
    /// Broadcast is atomic and instantaneous in this frame-level
    /// (loosely-timed) model.
    void broadcast(const CanDataFrame& frame) {
        m_lastFrame = frame;
        m_frameValidEvent.notify(sc_core::SC_ZERO_TIME);
    }

    const CanDataFrame& lastFrame() const { return m_lastFrame; }

private:
    CanDataFrame m_lastFrame;
};

#endif // CAN_BUS_H