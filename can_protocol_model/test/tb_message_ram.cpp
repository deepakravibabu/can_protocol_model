#include <iostream>
#include <systemc.h>
#include "../src/modules/can_types.h"
#include "../src/modules/message_ram.h"

/// @brief MessageRam is now pure content storage - no events, no
/// request state. Every case here is synchronous; nothing to wait()
/// on. (Contrast with tb_tx_handler.cpp, which now owns all the
/// request/scan/event behavior previously tested here.)
class Testbench : public sc_core::sc_module {
public:
    bool m_pass;

    SC_HAS_PROCESS(Testbench);
    explicit Testbench(const sc_core::sc_module_name& name)
        : sc_core::sc_module(name), m_pass(true), m_ram(nullptr) {}

    void bind(MessageRam* ram) {
        m_ram = ram;
        SC_THREAD(run);
    }

private:
    MessageRam* m_ram;

    void check(bool condition, const char* caseLabel) {
        std::cout << "[" << (condition ? "PASS" : "FAIL") << "] "
            << caseLabel << std::endl;
        if (!condition) {
            m_pass = false;
        }
    }

    void run() {
        // Case A: writing and reading buffer 0.
        std::uint8_t payloadA[8] = { 0xDE, 0xAD, 0xBE, 0xEF, 0, 0, 0, 0 };
        m_ram->writeTxBuffer(0U, 0x123, false, false, 4U, payloadA);
        const CanDataFrame f0 = m_ram->readTxBuffer(0U);
        check((f0.id == 0x123U) && (f0.dlc == 4U) && (f0.data[0] == 0xDE) &&
            (f0.data[3] == 0xEF), "case A: buffer 0 write/read round-trips");

        // Case B: boundary buffer index 31.
        std::uint8_t payloadB[8] = { 0x01, 0, 0, 0, 0, 0, 0, 0 };
        m_ram->writeTxBuffer(31U, 0x456, false, false, 1U, payloadB);
        const CanDataFrame f31 = m_ram->readTxBuffer(31U);
        check((f31.id == 0x456U) && (f31.dlc == 1U) && (f31.data[0] == 0x01),
            "case B: buffer 31 (last valid index) write/read round-trips");

        // Case C: out-of-range index is rejected safely, not a crash.
        std::uint8_t payloadC[8] = { 0xFF, 0, 0, 0, 0, 0, 0, 0 };
        m_ram->writeTxBuffer(32U, 0x001, false, false, 1U, payloadC); // expect SC_REPORT_WARNING
        const CanDataFrame fInvalid = m_ram->readTxBuffer(32U);        // expect SC_REPORT_WARNING
        check(fInvalid.id == 0U, "case C: out-of-range read returns a default frame, not garbage");

        // Case D: buffers are independent - writing one does not
        // disturb another.
        std::uint8_t payloadD1[8] = { 0xAA, 0, 0, 0, 0, 0, 0, 0 };
        std::uint8_t payloadD2[8] = { 0xBB, 0xCC, 0, 0, 0, 0, 0, 0 };
        m_ram->writeTxBuffer(2U, 0x200, false, false, 1U, payloadD1);
        m_ram->writeTxBuffer(5U, 0x500, false, false, 2U, payloadD2);
        const CanDataFrame f2 = m_ram->readTxBuffer(2U);
        const CanDataFrame f5 = m_ram->readTxBuffer(5U);
        check((f2.id == 0x200U) && (f2.dlc == 1U), "case D: buffer 2 unaffected by buffer 5's write");
        check((f5.id == 0x500U) && (f5.dlc == 2U), "case D: buffer 5 unaffected by buffer 2's write");

        // Case E: re-writing the same buffer simply overwrites it - no
        // request/pending awareness exists at this layer to warn about
        // it (see class comment - that check now belongs, if anywhere,
        // to whoever sequences MessageRam and TxHandler calls together).
        std::uint8_t payloadE1[8] = { 0x01, 0, 0, 0, 0, 0, 0, 0 };
        std::uint8_t payloadE2[8] = { 0x02, 0x03, 0, 0, 0, 0, 0, 0 };
        m_ram->writeTxBuffer(9U, 0x400, false, false, 1U, payloadE1);
        m_ram->writeTxBuffer(9U, 0x500, false, false, 2U, payloadE2);
        const CanDataFrame f9 = m_ram->readTxBuffer(9U);
        check((f9.id == 0x500U) && (f9.dlc == 2U), "case E: second write overwrites the first, no error");

        // Case F: extended (29-bit) ID passes through unvalidated -
        // MessageRam does not check ID range; that remains CanCore's
        // job later.
        std::uint8_t payloadF[8] = { 0x01, 0x02, 0, 0, 0, 0, 0, 0 };
        m_ram->writeTxBuffer(15U, 0x1FFFFFFFU, true, false, 2U, payloadF);
        const CanDataFrame f15 = m_ram->readTxBuffer(15U);
        check((f15.id == 0x1FFFFFFFU) && f15.ide,
            "case F: extended ID stored exactly, no range validation performed here");

        std::cout << std::endl << "TEST RESULT: " << (m_pass ? "PASS" : "FAIL")
            << std::endl;
        sc_core::sc_stop();
    }
};

int sc_main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    MessageRam ram("ram");
    Testbench  tb("tb");

    tb.bind(&ram);

    sc_core::sc_start();

    return tb.m_pass ? 0 : 1;
}