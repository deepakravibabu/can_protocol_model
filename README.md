
# SystemC Classic CAN Controller Model

A **SystemC Transaction-Level Model (TLM)** of a **Classical CAN Controller**, inspired by the Bosch **M_CAN / STM32 FDCAN** architecture. The model focuses on **functional correctness** and **protocol compliance**, while abstracting away bit timing and physical-layer behavior.

---

### CAN Controller Block Diagram
![Alt Text](can_protocol_model/images/can_controller_model.png "CAN Controller Block Diagram")


## Features
1. Node-to-node communication
2. Priority ID-based arbitration
3. Error detection and retransmission
4. Frame types: DATA


## Test Scenario
1. Two CAN Nodes connected through a CAN bus
	- Configure CAN block
	- With given payload, CAN data frame generation and transmission
	- CAN bus arbitration
	- CAN data frame read and retrieve message by another node




## Test Plan
1. Unit Test
	1. Register Test 
	- Read/ Write
	- Access Policy

	2. Controller Test
	- TX Handler to CAN Frame 
	- CAN Frame to RX Handler 

	3. Bus Test
	- Arbitration based on ID

2. Integration Test
- Communication between node1-node2

3. Robustness Tests
- Invalid Register values
- Controller reset during operation

4. Corner Cases
- Same ID from two transmitters
- Empty Payload

5. Protocol Compliance
- Lowest CAN ID wins
- Broadcast reach all nodes
- Payload integrity

## Execution Flow

1. CPU configures the controller registers.
2. CPU writes the message (payload, ID, RTR, DLC) into the Tx Buffer section of Message RAM.
3. CPU writes TXBAR to request transmission.
4. The Tx Handler scans the Tx Buffers, selects the highest-priority pending message, and sends to CAN Core.
5. The Tx Handler retrieves a complete CAN message from Message RAM and forwards it to the CAN Core. 
6. The CAN Core performs CAN protocol processing (SOF, arbitration, CRC, framing, serialization, ACK, EOF) and transmits the frame onto the CAN bus



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

