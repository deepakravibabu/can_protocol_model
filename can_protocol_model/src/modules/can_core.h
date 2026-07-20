#ifndef CAN_CORE_H
#define CAN_CORE_H

#include <cstdint>
#include <sstream>
#include <systemc.h>
#include "can_bus.h"
#include "can_types.h"
#include "i_protocol_config.h"
#include "i_tx_message_source.h"

/// @brief Validates the selected message against the current protocol
/// mode state, and - if legal - broadcasts it on the bus unchanged.
///
/// Deliberately simple at this phase: no SOF/CRC/ACK/EOF assembly, no
/// bit-level construction. The CanDataFrame that reaches the bus is
/// exactly the CanDataFrame TxHandler selected; CanCore's job here is
/// purely the legality/field-validation gate plus the handoff.
///
/// Also NOT modeled: bus-level (external) arbitration against other
/// nodes. TxHandler already resolved the INTERNAL priority scan among
/// this node's own buffers; external, bit-by-bit arbitration against
/// other nodes on the wire is a distinct mechanism, deferred along
/// with timing.
class CanCore : public sc_core::sc_module {
public:
    /// @brief Bound to TxHandler's exported ITxMessageSource, or to a
    /// testbench stub. A port, not a raw pointer: this is one of the
    /// two boundaries CanCore must be verifiable independently across.
    sc_core::sc_port<ITxMessageSource> m_txMessageSourcePort;

    /// @brief Bound to ConfigRegisters' exported IProtocolConfig, or to
    /// a testbench stub. Same reasoning as m_txMessageSourcePort.
    sc_core::sc_port<IProtocolConfig> m_configPort;

    SC_CTOR(CanCore)
        : m_bus(nullptr),
        m_lastRejectReason(CanRejectReason::NONE),
        m_framesGenerated(0U),
        m_framesRejected(0U) {
        SC_METHOD(evaluateProcess);
    }

    /// @brief Wires this core to the bus it broadcasts on. Plain
    /// pointer binding, not a port: CanBus is not a boundary this phase
    /// needs to substitute a stub across.
    void bindBus(CanBus* bus) { m_bus = bus; }

    // --- diagnostics, for testbench introspection -----------------------
    const CanDataFrame& lastFrameGenerated() const { return m_lastFrameGenerated; }
    CanRejectReason lastRejectReason() const { return m_lastRejectReason; }
    unsigned framesGenerated() const { return m_framesGenerated; }
    unsigned framesRejected() const { return m_framesRejected; }

private:
    CanBus* m_bus;
    CanDataFrame    m_lastFrameGenerated;
    CanRejectReason m_lastRejectReason;
    unsigned        m_framesGenerated;
    unsigned        m_framesRejected;

    static constexpr std::uint32_t k_maxStandardIdValue = 0x7FFU;      ///< 11-bit ID
    static constexpr std::uint32_t k_maxExtendedIdValue = 0x1FFFFFFFU; ///< 29-bit ID
    static constexpr std::uint8_t  k_maxClassicDlcValue = 8U;          ///< classic CAN only, this phase

    /// @brief Stage 1 - legality gate: is transmission permitted right
    /// now, independent of the message's own contents?
    bool isTransmissionPermitted() const {
        return !m_configPort->initModeActive();
    }

    /// @brief Stage 2 - field validation: is the message itself
    /// well-formed? Returns CanRejectReason::NONE if valid. No
    /// payload/dlc cross-check is performed - dlc is trusted as-is.
    CanRejectReason validateMessage(const CanDataFrame& msg) const {
        const std::uint32_t maxId = msg.ide ? k_maxExtendedIdValue
            : k_maxStandardIdValue;
        if (msg.id > maxId) {
            return CanRejectReason::ID_OUT_OF_RANGE;
        }
        if (msg.dlc > k_maxClassicDlcValue) {
            return CanRejectReason::INVALID_DLC;
        }
        return CanRejectReason::NONE;
    }

    /// @brief The sole process in this module. An SC_METHOD, not an
    /// SC_THREAD: nothing here ever needs to wait() - next_trigger()
    /// re-arms the method against the same event rather than blocking
    /// mid-body, which makes "CanCore never introduces a delay" a
    /// property the compiler enforces rather than a convention to
    /// remember.
    void evaluateProcess() {
        if (!m_txMessageSourcePort->hasSelectedMessage()) {
            next_trigger(m_txMessageSourcePort->selectionReadyEvent());
            return;
        }

        const CanDataFrame msg = m_txMessageSourcePort->selectedMessage();
        CanRejectReason reason;

        if (!isTransmissionPermitted()) {
            reason = CanRejectReason::INIT_MODE;
        }
        else {
            reason = validateMessage(msg);
        }

        if (reason == CanRejectReason::NONE) {
            m_lastFrameGenerated = msg;
            m_bus->broadcast(msg);
            ++m_framesGenerated;
            m_txMessageSourcePort->reportTransmissionResult(true);

            std::ostringstream oss;
            oss << "transmitted " << msg;
            SC_REPORT_INFO("CanCore", oss.str().c_str());
        }
        else {
            m_lastRejectReason = reason;
            ++m_framesRejected;
            m_txMessageSourcePort->reportTransmissionResult(false);

            std::ostringstream oss;
            oss << "rejected message, reason=" << toString(reason);
            SC_REPORT_WARNING("CanCore", oss.str().c_str());
        }

        next_trigger(m_txMessageSourcePort->selectionReadyEvent());
    }
};

#endif // CAN_CORE_H