#ifndef I_TX_MESSAGE_SOURCE_H
#define I_TX_MESSAGE_SOURCE_H

#include <systemc.h>
#include "can_types.h"

/// @brief Abstraction of "whatever supplies CanCore with the next
/// selected message to transmit", implemented for real by TxHandler.
///
/// CanCore depends on this interface, not on TxHandler directly, so a
/// CanCore-focused testbench can supply hand-picked messages through a
/// trivial stub, without needing a real MessageRam/TxHandler pair
/// (buffer scanning, priority resolution, etc.) behind it.
class ITxMessageSource : public sc_core::sc_interface {
public:
    /// @brief Event notified whenever a new selection becomes
    /// available, and again after CanCore reports a result (so CanCore
    /// always has something valid to next_trigger() on).
    virtual const sc_core::sc_event& selectionReadyEvent() const = 0;

    /// @brief True if a selected message is currently available.
    virtual bool hasSelectedMessage() const = 0;

    /// @brief The current selection. Only meaningful while
    /// hasSelectedMessage() is true.
    virtual CanDataFrame selectedMessage() const = 0;

    /// @brief Called by CanCore once it has decided the fate of the
    /// message previously returned by selectedMessage().
    /// @param accepted  true if the message was broadcast on the bus.
    virtual void reportTransmissionResult(bool accepted) = 0;

    ~ITxMessageSource() override = default;
};

#endif // I_TX_MESSAGE_SOURCE_H