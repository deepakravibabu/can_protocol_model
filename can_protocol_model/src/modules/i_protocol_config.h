#ifndef I_PROTOCOL_CONFIG_H
#define I_PROTOCOL_CONFIG_H

#include <systemc.h>

/// @brief Abstraction of the protocol-relevant subset of the FDCAN
/// configuration/control registers, as seen by CanCore.
///
/// CanCore depends on this interface rather than on a concrete
/// ConfigRegisters type so it can be driven, in isolation, by a
/// hand-written testbench stub - without requiring a full register
/// model to exist first.
class IProtocolConfig : public sc_core::sc_interface {
public:
    /// @brief Mirrors FDCAN_CCCR.INIT. While true, CanCore must not
    /// generate or transmit any frame.
    virtual bool initModeActive() const = 0;

    /// @brief Mirrors FDCAN_CCCR.FDOE (CAN FD operation enable). Not
    /// yet consulted by CanCore's validation logic (classic CAN only
    /// so far) - reserved for when FD DLC coding is added.
    virtual bool fdOperationEnabled() const = 0;

    /// @brief Mirrors FDCAN_CCCR.BRSE (bit rate switch enable).
    /// Reserved for the same reason as fdOperationEnabled().
    virtual bool bitRateSwitchEnabled() const = 0;

    ~IProtocolConfig() override = default;
};

#endif // I_PROTOCOL_CONFIG_H