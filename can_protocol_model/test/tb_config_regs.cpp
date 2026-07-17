#include <iostream>
#include <systemc.h>
#include "../src/modules/config_registers.h"

/// @brief Same testbench pattern as before: no events/processes in
/// ConfigRegisters, so nothing here actually needs simulated time to
/// pass. Structured as an SC_THREAD purely to keep the pattern
/// consistent with later steps that will need to react to events.
class Testbench : public sc_core::sc_module {
public:
    bool m_pass;

    SC_HAS_PROCESS(Testbench);
    explicit Testbench(const sc_core::sc_module_name& name)
        : sc_core::sc_module(name), m_pass(true), m_cfg(nullptr) {}

    void bind(ConfigRegisters* cfg) {
        m_cfg = cfg;
        SC_THREAD(run);
    }

private:
    ConfigRegisters* m_cfg;

    void check(bool condition, const char* caseLabel) {
        std::cout << "[" << (condition ? "PASS" : "FAIL") << "] "
            << caseLabel << std::endl;
        if (!condition) {
            m_pass = false;
        }
    }

    void run() {
        // Case 1: reset value is exactly 0x00000001 (INIT=1, every
        // other bit clear) - RM0433 56.5.6.
        check(m_cfg->cccr() == 0x00000001U, "case 1: reset value is 0x00000001");
        check(m_cfg->init(), "case 1: init() true at reset");
        check(!m_cfg->cce(), "case 1: cce() false at reset");
        check(!m_cfg->fdOperationEnabled(), "case 1: fdOperationEnabled() false at reset");
        check(!m_cfg->bitRateSwitchEnabled(), "case 1: bitRateSwitchEnabled() false at reset");

        // Case 2: write-protection blocks FDOE while CCE is still 0,
        // even though INIT happens to be 1 at reset - both must be set
        // BEFORE the protected write for it to take effect.
        m_cfg->setFdOperationEnabled(true);
        check(!m_cfg->fdOperationEnabled(),
            "case 2: setFdOperationEnabled(true) has NO effect while CCE=0");

        // Case 3: open the gate (CCE is always writable on its own),
        // then the same write now succeeds.
        m_cfg->setCce(true);
        check(m_cfg->cce(), "case 3: setCce(true) takes effect (CCE always writable)");
        m_cfg->setFdOperationEnabled(true);
        check(m_cfg->fdOperationEnabled(),
            "case 3: setFdOperationEnabled(true) now takes effect (INIT=1, CCE=1)");

        m_cfg->setBitRateSwitchEnabled(true);
        check(m_cfg->bitRateSwitchEnabled(),
            "case 3: setBitRateSwitchEnabled(true) also takes effect");

        // Case 4: clearing INIT auto-clears CCE (documented hardware
        // side effect), but does NOT retroactively clear bits that
        // were already latched in while the gate was open.
        m_cfg->setInit(false);
        check(!m_cfg->init(), "case 4: setInit(false) takes effect (INIT always writable)");
        check(!m_cfg->cce(), "case 4: cce() auto-cleared when INIT cleared");
        check(m_cfg->fdOperationEnabled(),
            "case 4: fdOperationEnabled() still true (already-set bits are not erased)");
        check(m_cfg->bitRateSwitchEnabled(),
            "case 4: bitRateSwitchEnabled() still true (already-set bits are not erased)");

        // Case 5: with the gate closed again, a further protected
        // write is blocked once more.
        m_cfg->setBitRateSwitchEnabled(false);
        check(m_cfg->bitRateSwitchEnabled(),
            "case 5: setBitRateSwitchEnabled(false) has NO effect (CCE=0 again)");

        // Case 6: re-opening the gate (INIT=1, then CCE=1) allows the
        // same write to finally succeed.
        m_cfg->setInit(true);
        m_cfg->setCce(true);
        m_cfg->setBitRateSwitchEnabled(false);
        check(!m_cfg->bitRateSwitchEnabled(),
            "case 6: setBitRateSwitchEnabled(false) takes effect once gate reopened");

        std::cout << std::endl << "TEST RESULT: " << (m_pass ? "PASS" : "FAIL")
            << std::endl;
        sc_core::sc_stop();
    }
};

int sc_main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    ConfigRegisters cfg("cfg");
    Testbench       tb("tb");

    tb.bind(&cfg);

    sc_core::sc_start();

    return tb.m_pass ? 0 : 1;
}