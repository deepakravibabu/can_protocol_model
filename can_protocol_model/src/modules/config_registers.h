#ifndef CONFIG_REGISTERS_H
#define CONFIG_REGISTERS_H

#include <cstdint>
#include <systemc.h>
#include "i_protocol_config.h"

/// @brief Bit positions within FDCAN_CCCR (RM0433 56.5.6, offset
/// 0x0018) that this model currently tracks. CCCR has other bits (ASM,
/// MON, DAR, TEST, PXHD, EFBI, TXP, NISO, ...) - they are not modeled
/// yet. Add a bit here only once something actually reads it.
namespace CccrBit {
    constexpr std::uint32_t INIT = 0U; ///< Initialization
    constexpr std::uint32_t CCE = 1U; ///< Configuration change enable
    constexpr std::uint32_t FDOE = 8U; ///< FD operation enable
    constexpr std::uint32_t BRSE = 9U; ///< Bit rate switching enable
}

/// @brief Models the subset of FDCAN_CCCR currently needed: a real
/// 32-bit register (not independent booleans), with the actual
/// INIT/CCE write-protection rule from RM0433 56.5.6 / 56.4:
///   - INIT and CCE are always software-writable.
///   - FDOE and BRSE only take effect if, immediately before the
///     write, INIT and CCE were BOTH already 1.
///   - Clearing INIT auto-clears CCE (documented hardware side effect).
///
/// Also implements IProtocolConfig, exported so CanCore's
/// sc_port<IProtocolConfig> can bind directly to this module.
class ConfigRegisters : public sc_core::sc_module, public IProtocolConfig {
public:
    /// @brief Reset value (RM0433 56.5.6): INIT=1, everything else 0.
    static constexpr std::uint32_t kResetValue = 0x00000001U;

    sc_core::sc_export<IProtocolConfig> m_configExport;

    explicit ConfigRegisters(const sc_core::sc_module_name& name)
        : sc_core::sc_module(name),
        m_configExport("configExport"),
        m_cccr(kResetValue) {
            m_configExport(*this);
        }

    /// @brief Full 32-bit write to FDCAN_CCCR, gated by the protection
    /// rule described above.
    void writeCccr(std::uint32_t rawValue) {
        // check if bit[0] INIT and bit[1] CCE are both '1'
        const bool protectedWriteAllowed =
            bitSet(m_cccr, CccrBit::INIT) && bitSet(m_cccr, CccrBit::CCE);

        std::uint32_t next = m_cccr;
        assignBit(next, CccrBit::INIT, bitSet(rawValue, CccrBit::INIT));
        assignBit(next, CccrBit::CCE, bitSet(rawValue, CccrBit::CCE));

        if (protectedWriteAllowed) {
            assignBit(next, CccrBit::FDOE, bitSet(rawValue, CccrBit::FDOE));
            assignBit(next, CccrBit::BRSE, bitSet(rawValue, CccrBit::BRSE));
        }

        if (bitSet(m_cccr, CccrBit::INIT) && !bitSet(next, CccrBit::INIT)) {
            assignBit(next, CccrBit::CCE, false);
        }

        m_cccr = next;
    }

    /// @brief Raw register value, for diagnostics/testbench use.
    std::uint32_t cccr() const { return m_cccr; }

    // --- Convenience single-bit read-modify-write setters -------------
    // Each goes through writeCccr(), so its protection logic is the
    // single source of truth - these do not bypass it.
    void setInit(bool value) { writeSingleBit(CccrBit::INIT, value); }
    void setCce(bool value) { writeSingleBit(CccrBit::CCE, value); }
    void setFdOperationEnabled(bool value) { writeSingleBit(CccrBit::FDOE, value); }
    void setBitRateSwitchEnabled(bool value) { writeSingleBit(CccrBit::BRSE, value); }

    // --- Read accessors -------------------------------------------------
    bool init() const { return bitSet(m_cccr, CccrBit::INIT); }
    bool cce() const { return bitSet(m_cccr, CccrBit::CCE); }
    bool fdOperationEnabled() const override { return bitSet(m_cccr, CccrBit::FDOE); }
    bool bitRateSwitchEnabled() const override { return bitSet(m_cccr, CccrBit::BRSE); }

    // --- IProtocolConfig -------------------------------------------------
    bool initModeActive() const override { return init(); }

private:
    std::uint32_t m_cccr;

    static bool bitSet(std::uint32_t value, std::uint32_t pos) {
        return ((value >> pos) & 0x1U) != 0U;
    }
    static void assignBit(std::uint32_t& value, std::uint32_t pos, bool set) {
        const std::uint32_t mask = (1U << pos);
        value = set ? (value | mask) : (value & ~mask);
    }
    void writeSingleBit(std::uint32_t pos, bool value) {
        std::uint32_t raw = m_cccr;
        assignBit(raw, pos, value);
        writeCccr(raw);
    }
};

#endif // CONFIG_REGISTERS_H