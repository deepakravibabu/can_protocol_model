#ifndef MESSAGE_RAM_H
#define MESSAGE_RAM_H

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <systemc.h>
#include "can_types.h"

/// @brief Storage for the dedicated Tx buffer region of the shared
/// Message RAM (RM0433 56.4.3), sized to the documented maximum of 32
/// dedicated buffers (FDCAN_TXBC.NDTB).
///
/// Pure content storage only - no request/pending state lives here.
/// FDCAN_TXBAR and FDCAN_TXBRP are genuinely separate registers that
/// live in TxHandler now (not merged in here), matching real hardware:
/// Message RAM itself has no awareness of which buffers have a pending
/// transmission request - that state lives elsewhere in the
/// peripheral's register file. Consequently, this module cannot (and
/// does not) warn about writing to a buffer whose request is still
/// pending - real Message RAM can't detect that either; avoiding that
/// hazard is a software/sequencing responsibility, not something the
/// storage itself enforces.
///
/// No process of its own: real Message RAM doesn't scan or decide
/// anything - that logic belongs to TxHandler.
class MessageRam : public sc_core::sc_module {
public:
    static constexpr unsigned kNumTxBuffers = 32U;

    SC_CTOR(MessageRam) {}

    /// @brief Writes one Tx buffer's content - equivalent to the CPU
    /// writing a Tx buffer element via the Message RAM interface.
    /// @param index    Buffer index, 0 to kNumTxBuffers-1.
    /// @param payload  Must point to at least dlc valid bytes.
    void writeTxBuffer(unsigned index, std::uint32_t id, bool ide, bool rtr,
        std::uint8_t dlc, const std::uint8_t* payload) {
        if (!indexValid(index, "writeTxBuffer")) {
            return;
        }
        CanDataFrame& frame = m_txBuffer[index];
        frame.id = id;
        frame.ide = ide;
        frame.rtr = rtr;
        frame.dlc = dlc;
        const std::uint8_t copyLen = std::min<std::uint8_t>(dlc, 8U);
        std::memcpy(frame.data, payload, copyLen);
    }

    /// @brief Reads buffer index's current contents.
    CanDataFrame readTxBuffer(unsigned index) const {
        if (!indexValid(index, "readTxBuffer")) {
            return CanDataFrame();
        }
        return m_txBuffer[index];
    }

private:
    std::array<CanDataFrame, kNumTxBuffers> m_txBuffer{};

    bool indexValid(unsigned index, const char* callerName) const {
        if (index >= kNumTxBuffers) {
            SC_REPORT_WARNING("MessageRam",
                (std::string(callerName) + "() called with an out-of-range "
                    "buffer index (must be 0.." + std::to_string(kNumTxBuffers - 1U)
                    + ")").c_str());
            return false;
        }
        return true;
    }
};

#endif // MESSAGE_RAM_H