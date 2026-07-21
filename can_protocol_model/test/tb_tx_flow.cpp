#include <iostream>
#include <vector>
#include <systemc.h>
#include "../src/modules/can_bus.h"
#include "../src/modules/can_core.h"
#include "../src/modules/can_types.h"
#include "../src/modules/config_registers.h"
#include "../src/modules/message_ram.h"
#include "../src/modules/tx_handler.h"

/// @brief End-to-end integration testbench for the full Tx execution
/// flow described in the README, using the REAL modules throughout -
/// no ITxMessageSource/IProtocolConfig stubs. Every other testbench in
/// this project verifies one module (optionally with its one real
/// neighbor); this one wires the entire chain together and drives it
/// exactly the way a CPU would:
///
///   1. CPU configures the controller registers.      (ConfigRegisters)
///   2. CPU writes payload/ID/RTR/DLC into a Tx buffer. (MessageRam)
///   3. CPU writes TXBAR to request transmission.       (TxHandler)
///   4/5. TxHandler scans, selects the highest-priority
///        pending message, forwards it.                 (TxHandler)
///   6. CanCore validates and transmits the frame.       (CanCore -> CanBus)
///
/// A passive BusMonitor observes CanBus, standing in for "the rest of
/// the network" - the same role tb_can_core.cpp's testbench played
/// directly, now driven by the real chain instead of a stub.
class BusMonitor : public sc_core::sc_module {
public:
    std::vector<CanDataFrame> m_received;

    SC_HAS_PROCESS(BusMonitor);
    explicit BusMonitor(const sc_core::sc_module_name& name)
        : sc_core::sc_module(name), m_bus(nullptr) {}

    void bind(CanBus* bus) {
        m_bus = bus;
        SC_METHOD(observeProcess);
        sensitive << m_bus->m_frameValidEvent;
        dont_initialize();
    }

private:
    CanBus* m_bus;

    void observeProcess() {
        m_received.push_back(m_bus->lastFrame());
    }
};

class Testbench : public sc_core::sc_module {
public:
    bool m_pass;

    SC_HAS_PROCESS(Testbench);
    explicit Testbench(const sc_core::sc_module_name& name)
        : sc_core::sc_module(name), m_pass(true),
        m_cfg(nullptr), m_ram(nullptr), m_txHandler(nullptr),
        m_core(nullptr), m_bus(nullptr), m_monitor(nullptr) {
    }

    void bind(ConfigRegisters* cfg, MessageRam* ram, TxHandler* txHandler,
        CanCore* core, CanBus* bus, BusMonitor* monitor) {
        m_cfg = cfg;
        m_ram = ram;
        m_txHandler = txHandler;
        m_core = core;
        m_bus = bus;
        m_monitor = monitor;
        SC_THREAD(run);
    }

private:
    ConfigRegisters* m_cfg;
    MessageRam* m_ram;
    TxHandler* m_txHandler;
    CanCore* m_core;
    CanBus* m_bus;
    BusMonitor* m_monitor;

    void check(bool condition, const char* caseLabel) {
        std::cout << "[" << (condition ? "PASS" : "FAIL") << "] "
            << caseLabel << std::endl;
        if (!condition) {
            m_pass = false;
        }
    }

    void settle() { wait(1, sc_core::SC_NS); }

    void run() {
        // ------------------------------------------------------------
        // Case A: at reset, CCCR.INIT = 1 (RM0433 56.5.6 reset value).
        // Step 1 of the execution flow ("CPU configures the controller
        // registers") hasn't happened yet - so a Tx attempt right now
        // must be rejected end-to-end, purely because ConfigRegisters'
        // reset state propagates all the way through TxHandler and
        // CanCore. This exercises the exact software-init dependency
        // called out when config_registers.h was first modeled.
        // ------------------------------------------------------------
        check(m_cfg->init(), "case A: CCCR.INIT is 1 at reset (software has not initialized yet)");

        std::uint8_t payloadA[8] = { 0xAA, 0, 0, 0, 0, 0, 0, 0 };
        m_ram->writeTxBuffer(0U, 0x050, false, false, 1U, payloadA); // step 2
        m_txHandler->requestTransmit(0U);                            // step 3
        settle();

        check(m_core->framesRejected() == 1U, "case A: CanCore rejected the message (INIT still active)");
        check(m_core->lastRejectReason() == CanRejectReason::INIT_MODE,
            "case A: rejection reason is INIT_MODE");
        check(m_monitor->m_received.empty(), "case A: nothing reached the bus");
        check(!m_txHandler->hasPendingRequest(0U),
            "case A: TXBRP still clears for buffer 0 even on rejection (buffer is free again)");

        // ------------------------------------------------------------
        // Step 1, for real this time: CPU brings the controller out of
        // initialization mode. Matches the documented software-init
        // sequence: INIT and CCE are always writable; here we only
        // need to clear INIT (no protected bits are being touched in
        // this test scenario).
        // ------------------------------------------------------------
        m_cfg->setInit(false);
        check(!m_cfg->init(), "case A: CCCR.INIT cleared - controller now out of init mode");

        // ------------------------------------------------------------
        // Case B: the nominal end-to-end path, steps 2-6 in full, one
        // buffer, one message.
        // ------------------------------------------------------------
        std::uint8_t payloadB[8] = { 0xDE, 0xAD, 0xBE, 0xEF, 0, 0, 0, 0 };
        m_ram->writeTxBuffer(0U, 0x123, false, false, 4U, payloadB); // step 2
        m_txHandler->requestTransmit(0U);                            // step 3
        settle();                                                    // steps 4-6 resolve

        check(m_core->framesGenerated() == 1U, "case B: CanCore generated/transmitted one frame");
        check(m_monitor->m_received.size() == 1U, "case B: exactly one frame reached the bus");
        const CanDataFrame& onBus1 = m_monitor->m_received.back();
        check((onBus1.id == 0x123U) && (onBus1.dlc == 4U) &&
            (onBus1.data[0] == 0xDE) && (onBus1.data[3] == 0xEF),
            "case B: bus frame matches exactly what the CPU wrote into the Tx buffer");
        check(!m_txHandler->hasPendingRequest(0U), "case B: TXBRP cleared for buffer 0 after transmission");

        // ------------------------------------------------------------
        // Case C: end-to-end priority arbitration. Two buffers loaded
        // and requested together (one TXBAR write, per RM0433 - "one
        // write can request several buffers at once"); the FULL chain,
        // including CanCore actually consuming each selection in turn,
        // must transmit the lower-ID message first, then the other.
        // ------------------------------------------------------------
        std::uint8_t payloadC5[8] = { 0x05, 0, 0, 0, 0, 0, 0, 0 };
        std::uint8_t payloadC2[8] = { 0x02, 0, 0, 0, 0, 0, 0, 0 };
        m_ram->writeTxBuffer(5U, 0x300, false, false, 1U, payloadC5); // lower priority
        m_ram->writeTxBuffer(2U, 0x100, false, false, 1U, payloadC2); // higher priority (lower ID)
        m_txHandler->writeTxbar((1U << 5U) | (1U << 2U));
        settle();

        check(m_core->framesGenerated() == 3U, "case C: both messages eventually generated (2 more since case B)");
        check(m_monitor->m_received.size() == 3U, "case C: both frames reached the bus");
        check(m_monitor->m_received[1].id == 0x100U,
            "case C: buffer 2 (lower ID) transmitted BEFORE buffer 5, despite being requested together");
        check(m_monitor->m_received[2].id == 0x300U,
            "case C: buffer 5 transmitted second, via the automatic rescan-on-completion");

        // ------------------------------------------------------------
        // Case D: an invalid standard ID rejected end-to-end.
        //
        // Caveat worth being honest about: in real hardware, a
        // standard-frame ID field is physically 11 bits wide in the Tx
        // buffer element - software cannot even construct a value like
        // 0x800 in that field; it would be truncated at the point of
        // writing. This model stores id as a plain uint32_t with no
        // field-width masking, so it CAN represent this out-of-range
        // case - CanCore's ID_OUT_OF_RANGE check is therefore partly a
        // model-level safety net rather than something reachable via
        // real register writes. Still worth testing, since it's the
        // path that would matter if field-width masking is ever added
        // to MessageRam later.
        // ------------------------------------------------------------
        std::uint8_t payloadD[8] = { 0x01, 0, 0, 0, 0, 0, 0, 0 };
        m_ram->writeTxBuffer(10U, 0x800, false, false, 1U, payloadD);
        m_txHandler->requestTransmit(10U);
        settle();

        check(m_core->framesRejected() == 2U, "case D: rejected end-to-end (1 more since case A)");
        check(m_core->lastRejectReason() == CanRejectReason::ID_OUT_OF_RANGE,
            "case D: rejection reason is ID_OUT_OF_RANGE");
        check(m_monitor->m_received.size() == 3U, "case D: no new frame reached the bus");
        check(!m_txHandler->hasPendingRequest(10U), "case D: buffer 10 freed up again despite rejection");

        // ------------------------------------------------------------
        // Case E: DLC too large for classic CAN, rejected end-to-end.
        // ------------------------------------------------------------
        std::uint8_t payloadE[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
        m_ram->writeTxBuffer(11U, 0x060, false, false, 9U, payloadE);
        m_txHandler->requestTransmit(11U);
        settle();

        check(m_core->framesRejected() == 3U, "case E: rejected end-to-end (1 more since case D)");
        check(m_core->lastRejectReason() == CanRejectReason::INVALID_DLC,
            "case E: rejection reason is INVALID_DLC");
        check(m_monitor->m_received.size() == 3U, "case E: still no new frame reached the bus");

        // ------------------------------------------------------------
        // Case F: extended-ID nominal frame, full chain again, to
        // confirm the extended-ID path works end-to-end too, not just
        // in CanCore's isolated stub test.
        // ------------------------------------------------------------
        std::uint8_t payloadF[8] = { 0x11, 0x22, 0, 0, 0, 0, 0, 0 };
        m_ram->writeTxBuffer(20U, 0x1FFFFFFFU, true, false, 2U, payloadF);
        m_txHandler->requestTransmit(20U);
        settle();

        check(m_core->framesGenerated() == 4U, "case F: extended-ID message generated end-to-end");
        check(m_monitor->m_received.size() == 4U, "case F: frame reached the bus");
        check((m_monitor->m_received.back().id == 0x1FFFFFFFU) && m_monitor->m_received.back().ide,
            "case F: extended ID/IDE preserved through the full chain");

        std::cout << std::endl << "TEST RESULT: " << (m_pass ? "PASS" : "FAIL")
            << std::endl;
        sc_core::sc_stop();
    }
};

int sc_main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    ConfigRegisters cfg("cfg");
    MessageRam      ram("ram");
    TxHandler       txHandler("txHandler");
    CanCore         core("core");
    CanBus          bus("bus");
    BusMonitor      monitor("monitor");
    Testbench       tb("tb");

    // --- The actual wiring of the whole chain ---
    txHandler.bind(&ram);
    core.bindBus(&bus);
    core.m_txMessageSourcePort(txHandler.m_messageSourceExport);
    core.m_configPort(cfg.m_configExport);
    monitor.bind(&bus);
    tb.bind(&cfg, &ram, &txHandler, &core, &bus, &monitor);

    sc_core::sc_start();

    return tb.m_pass ? 0 : 1;
}