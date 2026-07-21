
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


## Registers Modeled

| Register | Bit Field | Bits | Description | Module |
|----------|-----------|------|-------------|--------|
| `CCCR` | – | – | Controller Configuration Register | `config_registers.h` |
| | `INIT` | `[0]` | Initialization Mode | |
| | `CCE` | `[1]` | Configuration Change Enable | |
| | `FDOE` | `[8]` | FD Operation Enable | |
| | `BRSE` | `[9]` | Bit Rate Switching Enable | |
| `TXBAR` | – | – | Tx Buffer Add Request Register | `tx_handler.h` |
| `TXBRP` | – | – | Tx Buffer Request Pending Register | `tx_handler.h` |

## Message RAM

| Storage | Description | Module |
|---------|-------------|--------|
| `std::array<CanDataFrame,32> m_txBuffer` | Dedicated Tx Buffer memory | `message_ram.h` |

## Module Interfaces

| Module | Input | Output | Process | Description |
|--------|-------|--------|---------|-------------|
| `config_registers` | `cpu_write(CCCR)` | - | - | CPU configures controller registers. |
| `message_ram` | `writeTxBuffer(TxBufIdx, CanDataFrame)` | `readTxBuffer()` | - | Stores and retrieves CAN frames from Tx Buffer memory. |
| `tx_handler` | `requestTransmit(index)` | `selectionReadyEvent` | `SC_METHOD(scanProcess)` | Scans pending Tx buffers and selects the highest-priority frame. |
| `can_core` | `selectionReadyEvent` | `CanDataFrame` | `SC_METHOD(evaluateProcess)` | Validates the selected frame and forwards it to the CAN bus. |

## Execution Flow

1. CPU configures the controller registers.
2. CPU writes the message (payload, ID, RTR, DLC) into the Tx Buffer section of Message RAM.
3. CPU writes TXBAR to request transmission.
4. The Tx Handler scans the Tx Buffers, selects the highest-priority pending message, and sends to CAN Core.
5. The Tx Handler retrieves a complete CAN message from Message RAM and forwards it to the CAN Core. 
6. The CAN Core performs CAN protocol processing (SOF, arbitration, CRC, framing, serialization, ACK, EOF) and transmits the frame onto the CAN bus



# Components

| Module | Responsibility |
|---------|----------------|
| `can_types.h` | Shared protocol data structures (`CanDataFrame`, `CanRejectReason`, `RxFilterEntry`) |
| `config_registers.h` | Models CPU-visible configuration registers and controller operating mode |
| `message_ram.h` | Storage for Tx Buffers, Rx FIFO0 and Acceptance Filters. Contains no scheduling or filtering logic. |
| `tx_handler.h` | Owns `TXBAR/TXBRP`, scans pending Tx buffers and selects the highest-priority message (lowest CAN ID). |
| `can_core.h` | Validates the selected frame (controller mode, ID, DLC) and forwards it to the CAN bus. |
| `can_bus.h` | Broadcast communication medium connecting all CAN controller instances. |
| `rx_handler.h` | Performs acceptance filtering and stores accepted frames into Rx FIFO0. |
| `i_protocol_config.h` | Interface exposing controller mode to `CanCore`. |
| `i_tx_message_source.h` | Interface exposing the selected transmit frame to `CanCore`. |

---


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

---

# Test Cases

| Testbench | Coverage |
|-----------|----------|
| `tb_config_regs.cpp` | Register reset values, write-protection, INIT/CCE behavior |
| `tb_message_ram.cpp` | Tx buffer storage, overwrite, boundary conditions, invalid indices |
| `tb_tx_handler.cpp` | TXBAR/TXBRP behavior, internal arbitration, tie-breaking, rescan, pending requests |
| `tb_can_core.cpp` | Controller mode validation, ID validation, DLC validation, extended IDs, zero-length payload |
| `tb_rx_handler.cpp` | Acceptance filtering, Rx FIFO0 operation, FIFO overflow, FIFO ordering, IDE matching |

---

## Out of Scope

- Bit timing / synchronization
- CRC-15 implementation (placeholder only)
- Error Frames / Overload Frames

## Output

<details>
<summary><b>test for transmission flow</b></summary>

```text
$ ./tb_tx_flow

        SystemC 3.0.2-Accellera --- Jul 14 2026 13:07:36
        Copyright (c) 1996-2025 by all Contributors,
        ALL RIGHTS RESERVED
[PASS] case A: CCCR.INIT is 1 at reset (software has not initialized yet)

Warning: CanCore: rejected message, reason=INIT_MODE
In file: D:\Projects\sc\can_protocol_model\can_protocol_model\src\modules\can_core.h:126
In process: core.evaluateProcess @ 0 s
[PASS] case A: CanCore rejected the message (INIT still active)
[PASS] case A: rejection reason is INIT_MODE
[PASS] case A: nothing reached the bus
[PASS] case A: TXBRP still clears for buffer 0 even on rejection (buffer is free again)
[PASS] case A: CCCR.INIT cleared - controller now out of init mode

Info: CanCore: transmitted ID=0x123 IDE=0 RTR=0 DLC=4 DATA=[de ad be ef]
[PASS] case B: CanCore generated/transmitted one frame
[PASS] case B: exactly one frame reached the bus
[PASS] case B: bus frame matches exactly what the CPU wrote into the Tx buffer
[PASS] case B: TXBRP cleared for buffer 0 after transmission

Info: CanCore: transmitted ID=0x100 IDE=0 RTR=0 DLC=1 DATA=[2]

Info: CanCore: transmitted ID=0x300 IDE=0 RTR=0 DLC=1 DATA=[5]
[PASS] case C: both messages eventually generated (2 more since case B)
[PASS] case C: both frames reached the bus
[PASS] case C: buffer 2 (lower ID) transmitted BEFORE buffer 5, despite being requested together
[PASS] case C: buffer 5 transmitted second, via the automatic rescan-on-completion

Warning: CanCore: rejected message, reason=ID_OUT_OF_RANGE
In file: D:\Projects\sc\can_protocol_model\can_protocol_model\src\modules\can_core.h:126
In process: core.evaluateProcess @ 3 ns
[PASS] case D: rejected end-to-end (1 more since case A)
[PASS] case D: rejection reason is ID_OUT_OF_RANGE
[PASS] case D: no new frame reached the bus
[PASS] case D: buffer 10 freed up again despite rejection

Warning: CanCore: rejected message, reason=INVALID_DLC
In file: D:\Projects\sc\can_protocol_model\can_protocol_model\src\modules\can_core.h:126
In process: core.evaluateProcess @ 4 ns
[PASS] case E: rejected end-to-end (1 more since case D)
[PASS] case E: rejection reason is INVALID_DLC
[PASS] case E: still no new frame reached the bus

Info: CanCore: transmitted ID=0x1fffffff IDE=1 RTR=0 DLC=2 DATA=[11 22]
[PASS] case F: extended-ID message generated end-to-end
[PASS] case F: frame reached the bus
[PASS] case F: extended ID/IDE preserved through the full chain

TEST RESULT: PASS

Info: /OSCI/SystemC: Simulation stopped by user.

```
</details>

<details>
<summary><b>test for Config registers</b></summary>
</details>
