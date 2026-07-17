#include <iostream>
#include <systemc.h>
#include "../src/modules/can_types.h"
#include "../src/modules/message_ram.h"
#include "../src/modules/tx_handler.h"

/// @brief Drives MessageRam (buffer content) and TxHandler (TXBAR
/// writes) directly, standing in for CPU firmware. This is the
/// testbench that actually exercises the priority scan: multiple
/// buffers pending at once, ID-based winner selection, tie-breaking by
/// buffer index, and the rescan-on-completion behavior.
class Testbench : public sc_core::sc_module {
public:
    bool m_pass;

    SC_HAS_PROCESS(Testbench);
    explicit Testbench(const sc_core::sc_module_name& name)
        : sc_core::sc_module(name), m_pass(true), m_ram(nullptr), m_txHandler(nullptr) {}

    void bind(MessageRam* ram, TxHandler* txHandler) {
        m_ram = ram;
        m_txHandler = txHandler;
        SC_THREAD(run);
    }

private:
    MessageRam* m_ram;
    TxHandler* m_txHandler;

    void check(bool condition, const char* caseLabel) {
        std::cout << "[" << (condition ? "PASS" : "FAIL") << "] "
            << caseLabel << std::endl;
        if (!condition) {
            m_pass = false;
        }
    }

    void run() {
        // Case A: nothing pending, nothing selected, initially.
        check(m_txHandler->txbar() == 0U, "case A: TXBAR reads 0 initially");
        check(m_txHandler->txbrp() == 0U, "case A: TXBRP reads 0 initially");
        check(!m_txHandler->hasSelectedMessage(), "case A: no selection initially");

        // Case B: single buffer requested via requestTransmit() (the
        // single-bit convenience form of a TXBAR write). Confirms the
        // whole chain: buffer write -> TXBAR write -> TXBRP set ->
        // scan -> selection.
        std::uint8_t payloadB[8] = { 0xDE, 0xAD, 0xBE, 0xEF, 0, 0, 0, 0 };
        m_ram->writeTxBuffer(10U, 0x300, false, false, 4U, payloadB);
        m_txHandler->requestTransmit(10U);

        // Per the documented simplification, TXBAR has already
        // auto-cleared by the time this call returns - no wait()
        // needed to observe that.
        check(m_txHandler->txbar() == 0U,
            "case B: TXBAR reads back 0 immediately (auto-clear simplification)");
        check(m_txHandler->txbrp() == (1U << 10U),
            "case B: TXBRP shows buffer 10 pending");

        wait(m_txHandler->m_selectionReadyEvent);
        check(m_txHandler->hasSelectedMessage(), "case B: scan produced a selection");
        check(m_txHandler->selectedBufferIndex() == 10U, "case B: buffer 10 selected (only one pending)");
        const CanDataFrame f1 = m_txHandler->selectedMessage();
        check((f1.id == 0x300U) && (f1.dlc == 4U) && (f1.data[0] == 0xDE),
            "case B: selected message matches what was written");

        // Case C: completing the request clears TXBRP for that buffer
        // and the selection.
        m_txHandler->reportTransmissionResult(true);
        check(m_txHandler->txbrp() == 0U, "case C: TXBRP cleared after completion");
        check(!m_txHandler->hasSelectedMessage(), "case C: selection cleared after completion");
        check(m_txHandler->lastAccepted(), "case C: accepted outcome recorded");
        check(m_txHandler->lastCompletedIndex() == 10U, "case C: correct buffer index recorded");

        // Case D: THE PRIORITY TEST. Two buffers requested in a single
        // TXBAR write (buffer 2 with the numerically higher/lower-
        // priority ID, buffer 7 with the lower/higher-priority ID) -
        // exercising the "one write requests several buffers at once"
        // feature explicitly, and confirming the scan picks the lowest
        // ID, NOT the lowest buffer index.
        std::uint8_t payloadD2[8] = { 0x01, 0, 0, 0, 0, 0, 0, 0 };
        std::uint8_t payloadD7[8] = { 0x02, 0, 0, 0, 0, 0, 0, 0 };
        m_ram->writeTxBuffer(2U, 0x500, false, false, 1U, payloadD2); // higher id -> lower priority
        m_ram->writeTxBuffer(7U, 0x100, false, false, 1U, payloadD7); // lower id -> higher priority
        m_txHandler->writeTxbar((1U << 2U) | (1U << 7U));

        check(m_txHandler->txbrp() == ((1U << 2U) | (1U << 7U)),
            "case D: TXBRP shows both buffers 2 and 7 pending");

        wait(m_txHandler->m_selectionReadyEvent);
        check(m_txHandler->selectedBufferIndex() == 7U,
            "case D: buffer 7 selected (lower ID wins, despite higher buffer index)");
        check(m_txHandler->selectedMessage().id == 0x100U, "case D: selected message is the lower-ID one");

        // Case E: completing buffer 7 (the winner) should immediately
        // re-trigger a scan and select buffer 2 next, with no new
        // stimulus write needed - this is the "any TXBRP update
        // re-triggers a scan" behavior from RM0433 56.4.
        m_txHandler->reportTransmissionResult(true);
        wait(m_txHandler->m_selectionReadyEvent);
        check(m_txHandler->hasSelectedMessage(),
            "case E: rescan automatically selects the remaining pending buffer");
        check(m_txHandler->selectedBufferIndex() == 2U, "case E: buffer 2 selected (the only one left)");

        // Case F: TIE-BREAK TEST. Two buffers with the SAME message ID
        // - the lower buffer index must win.
        m_txHandler->reportTransmissionResult(true); // clear buffer 2 from case E first
        std::uint8_t payloadF3[8] = { 0x03, 0, 0, 0, 0, 0, 0, 0 };
        std::uint8_t payloadF20[8] = { 0x14, 0, 0, 0, 0, 0, 0, 0 };
        m_ram->writeTxBuffer(20U, 0x123, false, false, 1U, payloadF20);
        m_ram->writeTxBuffer(3U, 0x123, false, false, 1U, payloadF3);
        m_txHandler->writeTxbar((1U << 20U) | (1U << 3U));
        wait(m_txHandler->m_selectionReadyEvent);
        check(m_txHandler->selectedBufferIndex() == 3U,
            "case F: equal IDs -> lower buffer index (3, not 20) wins the tie");

        // Case G: the "ignored if already pending" rule (RM0433
        // 56.5.38 note). Buffer 3 is still pending/selected from case
        // F; requesting it again must not disturb anything.
        m_txHandler->requestTransmit(3U); // should be a silent no-op
        check(m_txHandler->txbrp() == ((1U << 20U) | (1U << 3U)),
            "case G: redundant requestTransmit() on an already-pending buffer changes nothing");
        m_txHandler->reportTransmissionResult(true); // clear buffer 3
        wait(m_txHandler->m_selectionReadyEvent);
        check(m_txHandler->selectedBufferIndex() == 20U,
            "case G: buffer 20 selected next, unaffected by the redundant request in between");
        m_txHandler->reportTransmissionResult(true); // clear buffer 20; nothing left pending

        // Let the final (no-selection) scan's delta cycle settle before
        // checking final state - there is no further selection event
        // to wait() on here, since nothing remains pending.
        wait(1, sc_core::SC_NS);
        check(m_txHandler->txbrp() == 0U, "case G: TXBRP empty after all buffers completed");
        check(!m_txHandler->hasSelectedMessage(), "case G: no selection remains");

        // Case H: out-of-range buffer index is rejected safely.
        m_txHandler->requestTransmit(32U); // expect SC_REPORT_WARNING
        check(m_txHandler->txbrp() == 0U, "case H: out-of-range request has no effect on TXBRP");

        std::cout << std::endl << "TEST RESULT: " << (m_pass ? "PASS" : "FAIL")
            << std::endl;
        sc_core::sc_stop();
    }
};

int sc_main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    MessageRam ram("ram");
    TxHandler  txHandler("txHandler");
    Testbench  tb("tb");

    txHandler.bind(&ram);
    tb.bind(&ram, &txHandler);

    sc_core::sc_start();

    return tb.m_pass ? 0 : 1;
}