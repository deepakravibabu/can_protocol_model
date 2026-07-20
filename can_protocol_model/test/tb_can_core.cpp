#include <iostream>
#include <systemc.h>
#include "../src/modules/can_bus.h"
#include "../src/modules/can_core.h"
#include "../src/modules/can_types.h"
#include "../src/modules/i_protocol_config.h"
#include "../src/modules/i_tx_message_source.h"

/// @brief Hand-written stub standing in for TxHandler. Lets this
/// testbench drive CanCore with specific messages directly - no real
/// buffer-scan/priority logic involved, matching the point of the
/// ITxMessageSource abstraction (CanCore verified independently of its
/// real upstream neighbor).
class TxMessageSourceStub : public ITxMessageSource {
public:
    sc_core::sc_event m_readyEvent;

    TxMessageSourceStub()
        : m_hasMessage(false), m_lastAccepted(false), m_reportCount(0U) {
    }

    void setSelectedMessage(const CanDataFrame& msg) {
        m_message = msg;
        m_hasMessage = true;
        m_readyEvent.notify(sc_core::SC_ZERO_TIME);
    }

    const sc_core::sc_event& selectionReadyEvent() const override { return m_readyEvent; }
    bool hasSelectedMessage() const override { return m_hasMessage; }
    CanDataFrame selectedMessage() const override { return m_message; }
    void reportTransmissionResult(bool accepted) override {
        m_hasMessage = false;
        m_lastAccepted = accepted;
        ++m_reportCount;
    }

    bool lastAccepted() const { return m_lastAccepted; }
    unsigned reportCount() const { return m_reportCount; }

private:
    CanDataFrame m_message;
    bool         m_hasMessage;
    bool         m_lastAccepted;
    unsigned     m_reportCount;
};

/// @brief Hand-written stub standing in for ConfigRegisters.
class ProtocolConfigStub : public IProtocolConfig {
public:
    ProtocolConfigStub() : m_init(false), m_fdoe(false), m_brse(false) {}

    void setInitModeActive(bool value) { m_init = value; }

    bool initModeActive() const override { return m_init; }
    bool fdOperationEnabled() const override { return m_fdoe; }
    bool bitRateSwitchEnabled() const override { return m_brse; }

private:
    bool m_init;
    bool m_fdoe;
    bool m_brse;
};

class Testbench : public sc_core::sc_module {
public:
    bool m_pass;

    SC_HAS_PROCESS(Testbench);
    explicit Testbench(const sc_core::sc_module_name& name)
        : sc_core::sc_module(name), m_pass(true),
        m_core(nullptr), m_txSource(nullptr), m_cfg(nullptr), m_bus(nullptr) {
    }

    void bind(CanCore* core, TxMessageSourceStub* txSource,
        ProtocolConfigStub* cfg, CanBus* bus) {
        m_core = core;
        m_txSource = txSource;
        m_cfg = cfg;
        m_bus = bus;
        SC_THREAD(run);
    }

private:
    CanCore* m_core;
    TxMessageSourceStub* m_txSource;
    ProtocolConfigStub* m_cfg;
    CanBus* m_bus;

    void check(bool condition, const char* caseLabel) {
        std::cout << "[" << (condition ? "PASS" : "FAIL") << "] "
            << caseLabel << std::endl;
        if (!condition) {
            m_pass = false;
        }
    }

    static CanDataFrame makeStdFrame(std::uint32_t id, std::uint8_t dlc,
        std::initializer_list<std::uint8_t> bytes) {
        CanDataFrame f;
        f.id = id; f.ide = false; f.rtr = false; f.dlc = dlc;
        unsigned i = 0U;
        for (std::uint8_t b : bytes) { f.data[i++] = b; }
        return f;
    }

    void run() {
        // Case A: nominal standard frame -> accepted, appears on the
        // bus unchanged (header + payload only, no extra fields).
        const CanDataFrame f1 = makeStdFrame(0x123, 4U, { 0xDE, 0xAD, 0xBE, 0xEF });
        m_txSource->setSelectedMessage(f1);
        wait(m_bus->m_frameValidEvent);

        const CanDataFrame& b1 = m_bus->lastFrame();
        check((b1.id == 0x123U) && !b1.ide && (b1.dlc == 4U), "case A: header fields match");
        check((b1.data[0] == 0xDE) && (b1.data[3] == 0xEF), "case A: payload matches");
        check(b1 == f1, "case A: bus frame is identical to the selected message (no transformation)");
        check(m_core->framesGenerated() == 1U, "case A: framesGenerated() incremented");
        check(m_txSource->lastAccepted(), "case A: TxHandler-side stub sees an accepted result");

        // Case B: INIT mode active -> rejected, nothing reaches the bus.
        m_cfg->setInitModeActive(true);
        const CanDataFrame f2 = makeStdFrame(0x200, 1U, { 0xAA });
        m_txSource->setSelectedMessage(f2);
        wait(1, sc_core::SC_NS); // no bus event will fire; let the delta settle instead
        check(m_core->framesRejected() == 1U, "case B: framesRejected() incremented");
        check(m_core->lastRejectReason() == CanRejectReason::INIT_MODE, "case B: reject reason is INIT_MODE");
        check(!m_txSource->lastAccepted(), "case B: stub sees a rejected result");
        m_cfg->setInitModeActive(false);

        // Case C: standard ID exceeding the 11-bit range -> rejected.
        CanDataFrame f3; f3.id = 0x800; f3.ide = false; f3.dlc = 1U; f3.data[0] = 0x01;
        m_txSource->setSelectedMessage(f3);
        wait(1, sc_core::SC_NS);
        check(m_core->lastRejectReason() == CanRejectReason::ID_OUT_OF_RANGE,
            "case C: standard ID 0x800 rejected as ID_OUT_OF_RANGE");

        // Case D: DLC exceeding the classic-CAN maximum of 8 -> rejected.
        CanDataFrame f4; f4.id = 0x050; f4.ide = false; f4.dlc = 9U;
        m_txSource->setSelectedMessage(f4);
        wait(1, sc_core::SC_NS);
        check(m_core->lastRejectReason() == CanRejectReason::INVALID_DLC,
            "case D: dlc=9 rejected as INVALID_DLC");

        // Case E: nominal extended-ID frame -> accepted.
        CanDataFrame f5; f5.id = 0x1FFFFFFFU; f5.ide = true; f5.dlc = 2U;
        f5.data[0] = 0x01; f5.data[1] = 0x02;
        m_txSource->setSelectedMessage(f5);
        wait(m_bus->m_frameValidEvent);
        check((m_bus->lastFrame().id == 0x1FFFFFFFU) && m_bus->lastFrame().ide,
            "case E: extended ID/IDE preserved on the bus");

        // Case F: dlc=0 (empty payload) is legal and accepted.
        CanDataFrame f6; f6.id = 0x001; f6.ide = false; f6.dlc = 0U;
        m_txSource->setSelectedMessage(f6);
        wait(m_bus->m_frameValidEvent);
        check(m_bus->lastFrame().dlc == 0U, "case F: dlc=0 frame accepted and reaches the bus");

        std::cout << std::endl << "TEST RESULT: " << (m_pass ? "PASS" : "FAIL")
            << std::endl;
        sc_core::sc_stop();
    }
};

int sc_main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    CanBus              bus("bus");
    CanCore             core("core");
    TxMessageSourceStub txSource;
    ProtocolConfigStub  cfg;
    Testbench           tb("tb");

    core.bindBus(&bus);
    core.m_txMessageSourcePort(txSource);
    core.m_configPort(cfg);
    tb.bind(&core, &txSource, &cfg, &bus);

    sc_core::sc_start();

    return tb.m_pass ? 0 : 1;
}