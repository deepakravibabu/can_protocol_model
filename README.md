# can_protocol_model

# SystemC Classic CAN Controller Model

A **SystemC Transaction-Level Model (TLM)** of a **Classical CAN Controller**, inspired by the Bosch **M_CAN / STM32 FDCAN** architecture. The model focuses on **functional correctness** and **protocol compliance**, while abstracting away bit timing and physical-layer behavior.

---

### CAN Controller Block Diagram
![Alt Text](can_protocol_model/images/can_controller_model.png "CAN Controller Block Diagram")


## Components

| Module | Responsibility |
|---------|----------------|
| **config_registers.h** | Models CPU-visible CAN controller configuration registers (currently CCCR). |
| **can_types.h** | Defines common CAN data structures (`CanDataFrame`, `CanProtocolFrame`). |
| **message_ram.h** | Stores Tx/Rx CAN messages and acceptance filters. |
| **tx_handler.h** | Performs local Tx arbitration and manages TXBAR/TXBRP registers. |
| **can_core.h** | Builds a complete CAN protocol frame (SOF, Arbitration, CRC, ACK, EOF, etc.). |
| **can_bus.h** *(Planned)* | Models CAN bus communication and inter-node arbitration. |

---

## Registers Modeled

| Register | Description |
|----------|-------------|
| CCCR | Controller Configuration Register |
| TXBAR | Tx Buffer Add Request Register |
| TXBRP | Tx Buffer Request Pending Register |


## Features

- Register-level CPU interface
- Dedicated Tx/Rx Buffers
- `CanDataFrame` abstraction

---

## Out of Scope

- Bit timing / synchronization
- CRC-15 implementation (placeholder only)
- Error Frames / Overload Frames

