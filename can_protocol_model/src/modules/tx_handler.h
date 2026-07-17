#ifndef TX_HANDLER_H
#define TX_HANDLER_H

#include <cstdint>
#include <string>
#include <systemc.h>
#include "can_types.h"
#include "message_ram.h"

/// @brief Owns the FDCAN_TXBAR / FDCAN_TXBRP register pair (RM0433
/// 56.5.37 / 56.5.38) and performs the Tx-priority scan those registers
/// exist to drive (RM0433 56.4, "Tx prioritization" / "Tx priority
/// setting").
///
/// IMPORTANT, confirmed directly from the spec: the scan performed here
/// is entirely INTERNAL to this node. RM0433 56.4 states it exactly:
/// "The requested messages arbitrate internally with messages from an
/// optional Tx FIFO or Tx queue and externally with messages on the
/// CAN bus." This module implements only the internal half - comparing
/// THIS node's own pending buffers' message IDs against each other,
/// using data already sitting in Message RAM, before anything reaches
/// the wire. The external half (bit-by-bit arbitration against other
/// nodes, on the bus) is CAN Core's job, not modeled here.
class TxHandler : public sc_core::sc_module {
public:
    /// @brief Notified whenever the scan selects a new highest-priority
    /// message. Whatever is downstream (CanCore, once it exists again)
    /// reacts to this.
    sc_core::sc_event m_selectionReadyEvent;

    SC_HAS_PROCESS(TxHandler);
    explicit TxHandler(const sc_core::sc_module_name& name)
        : sc_core::sc_module(name),
        m_ram(nullptr),
        m_txbar(0U),
        m_txbrp(0U),
        m_hasSelection(false),
        m_selectedIndex(0U),
        m_lastAccepted(false),
        m_lastCompletedIndex(0U) {
        SC_METHOD(scanProcess);
        sensitive << m_txbrpUpdatedEvent;
    }

    /// @brief Wires this handler to the Message RAM it reads pending
    /// buffers' contents from during a scan. Plain pointer binding, not
    /// a port: MessageRam is an internal neighbor here, not one of the
    /// boundaries reserved for port-based stub verification (that
    /// treatment is for CanCore's inputs, once CanCore is
    /// reintroduced).
    void bind(MessageRam* ram) { m_ram = ram; }

    /// @brief Models a CPU write to FDCAN_TXBAR (RM0433 56.5.38): for
    /// every bit set in mask, requests that buffer's transmission -
    /// unless its FDCAN_TXBRP bit is already set, in which case that
    /// bit's request is silently ignored (spec note under 56.5.38).
    /// Bits clear in mask are left untouched ("writing a 0 has no
    /// impact"). This is also how the real register lets one CPU write
    /// request several buffers at once.
    ///
    /// @note Simplification, flagged explicitly: real hardware holds a
    /// TXBAR bit set until an in-progress Tx scan completes, if a write
    /// arrives mid-scan. Since this model's scan (scanProcess())
    /// completes in zero simulated time, that stall condition never
    /// arises here - txbar() always reads back 0 immediately after
    /// this call returns. This is the one place this model diverges
    /// from exact register timing; revisit if/when scan latency is
    /// added.
    void writeTxbar(std::uint32_t mask) {
        std::uint32_t newlyAccepted = 0U;
        for (unsigned i = 0U; i < MessageRam::kNumTxBuffers; ++i) {
            if (!bitSet(mask, i)) {
                continue; // writing 0 has no effect
            }
            if (bitSet(m_txbrp, i)) {
                continue; // ignored: request already pending (56.5.38 note)
            }
            newlyAccepted |= (1U << i);
        }
        if (newlyAccepted == 0U) {
            return; // nothing new added; no re-scan needed
        }

        m_txbar = newlyAccepted;  // transiently "set"...
        m_txbrp |= newlyAccepted;  // ...immediately latched into TXBRP...
        m_txbar = 0U;             // ...and TXBAR auto-clears (see note above)

        m_txbrpUpdatedEvent.notify(sc_core::SC_ZERO_TIME);
    }

    /// @brief Convenience single-buffer form of writeTxbar().
    void requestTransmit(unsigned index) {
        if (index >= MessageRam::kNumTxBuffers) {
            SC_REPORT_WARNING("TxHandler", "requestTransmit() out-of-range buffer index");
            return;
        }
        writeTxbar(1U << index);
    }

    /// @brief Raw FDCAN_TXBAR value. Per the note on writeTxbar(), this
    /// reads 0 immediately after any write completes in this model -
    /// kept for completeness/diagnostics and as the seam where real
    /// scan timing would later show a transient 1.
    std::uint32_t txbar() const { return m_txbar; }

    /// @brief Raw FDCAN_TXBRP value - which buffers currently have an
    /// unserviced transmission request.
    std::uint32_t txbrp() const { return m_txbrp; }

    bool hasPendingRequest(unsigned index) const {
        return (index < MessageRam::kNumTxBuffers) && bitSet(m_txbrp, index);
    }

    /// @brief True if the most recent scan has a winner selected.
    bool hasSelectedMessage() const { return m_hasSelection; }

    /// @brief The winning message from the most recent scan. Only
    /// meaningful while hasSelectedMessage() is true.
    CanDataFrame selectedMessage() const { return m_selectedMessage; }

    /// @brief Which Tx buffer index won the most recent scan.
    unsigned selectedBufferIndex() const { return m_selectedIndex; }

    /// @brief Called once the selected message has been handled
    /// downstream (by CanCore, once it exists) - clears
    /// FDCAN_TXBRP.TRPn for the selected buffer (RM0433 56.5.37: "The
    /// bits are reset after a requested transmission has completed"),
    /// and re-triggers a scan for whatever is pending next (RM0433
    /// 56.4: "...or when a transmission has been started" - a
    /// completion likewise re-triggers the scan in this model).
    /// @param accepted  Recorded purely for diagnostics - no
    /// TXBTO/TXBCF (transmission-occurred / cancel-finished) status is
    /// modeled yet.
    void reportTransmissionResult(bool accepted) {
        m_txbrp &= ~(1U << m_selectedIndex);
        m_lastAccepted = accepted;
        m_lastCompletedIndex = m_selectedIndex;
        m_hasSelection = false;

        m_txbrpUpdatedEvent.notify(sc_core::SC_ZERO_TIME);
    }

    // --- diagnostics, for testbench introspection -----------------------
    bool lastAccepted() const { return m_lastAccepted; }
    unsigned lastCompletedIndex() const { return m_lastCompletedIndex; }

private:
    MessageRam* m_ram;
    sc_core::sc_event m_txbrpUpdatedEvent;

    std::uint32_t m_txbar;
    std::uint32_t m_txbrp;

    bool         m_hasSelection;
    unsigned     m_selectedIndex;
    CanDataFrame m_selectedMessage;

    bool     m_lastAccepted;
    unsigned m_lastCompletedIndex;

    static bool bitSet(std::uint32_t value, unsigned pos) {
        return ((value >> pos) & 0x1U) != 0U;
    }

    /// @brief The internal Tx scan (RM0433 56.4 / 56.5.37): among all
    /// buffers with FDCAN_TXBRP set, find the one whose message ID is
    /// lowest; ties broken by lowest buffer index (RM0433, "Dedicated
    /// Tx buffers": "In case that multiple Tx buffers are configured
    /// with the same message ID, the Tx buffer with the lowest buffer
    /// number is transmitted first"). No bus access, no comparison
    /// against other nodes - purely this node's own pending buffers.
    void scanProcess() {
        if (m_ram == nullptr) {
            SC_REPORT_WARNING("TxHandler", "scanProcess() ran before bind() was called");
            return;
        }
        if (m_txbrp == 0U) {
            m_hasSelection = false;
            return;
        }

        int winnerIndex = -1;
        CanDataFrame winnerFrame;
        for (unsigned i = 0U; i < MessageRam::kNumTxBuffers; ++i) {
            if (!bitSet(m_txbrp, i)) {
                continue;
            }
            const CanDataFrame candidateFrame = m_ram->readTxBuffer(i);
            // Scanning low-to-high index and only replacing on a
            // STRICTLY lower id means an equal id at a higher index
            // never displaces the current winner - this is what
            // implements the "lowest buffer number wins ties" rule.
            if ((winnerIndex < 0) || (candidateFrame.id < winnerFrame.id)) {
                winnerIndex = static_cast<int>(i);
                winnerFrame = candidateFrame;
            }
        }

        m_selectedIndex = static_cast<unsigned>(winnerIndex);
        m_selectedMessage = winnerFrame;
        m_hasSelection = true;
        m_selectionReadyEvent.notify(sc_core::SC_ZERO_TIME);
    }
};

#endif // TX_HANDLER_H