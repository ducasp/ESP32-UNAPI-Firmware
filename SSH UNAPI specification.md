# SSH UNAPI specification

## Index

[1. Introduction](#1-introduction)

[1.1. Design goals](#11-design-goals)

[1.2. Specification scope](#12-specification-scope)

[1.3. Modularity](#13-modularity)

[2. API identifier and version](#2-api-identifier-and-version)

[3. Error codes](#3-error-codes)

[4. API routines](#4-api-routines)

[4.1. Information gathering routines](#41-information-gathering-routines)

[4.1.1. UNAPI_GET_INFO: Obtain the implementation name and version](#411-unapi_get_info-obtain-the-implementation-name-and-version)

[4.1.2. SSH_GET_CAPAB: Get information about the SSH capabilities and features](#412-ssh_get_capab-get-information-about-the-ssh-capabilities-and-features)

[4.2. SSH common routines](#42-ssh-common-routines)

[4.2.1. SSH_OPEN: Open an SSH client connection](#421-ssh_open-open-an-ssh-client-connection)

[4.2.2. SSH_CLOSE: Close an SSH connection](#422-ssh_close-close-an-ssh-connection)

[4.2.3. SSH_STATE: Get the state of an SSH connection](#423-ssh_state-get-the-state-of-an-ssh-connection)

[4.2.4. SSH_SEND: Send data to an SSH connection](#424-ssh_send-send-data-to-an-ssh-connection)

[4.2.5. SSH_RCV: Receive data from an SSH connection](#425-ssh_rcv-receive-data-from-an-ssh-connection)

[4.3. SSH PTY specific routines](#43-ssh-pty-specific-routines)

[4.3.1. SSH_TERM_TYPE: Get/set the SSH terminal type](#431-ssh_term_type-getset-the-ssh-terminal-type)

[4.3.2. SSH_WIN_SIZE: Get/set the SSH window size](#432-ssh_win_size-getset-the-ssh-window-size)

[4.4. SSH SFTP specific routines](#44-ssh-sftp-specific-routines)

[4.5. SSH SCP specific routines](#45-ssh-scp-specific-routines)

## 1. Introduction

MSX-UNAPI is a standard procedure for defining, discovering and using new APIs
(Application Program Interfaces) for MSX computers. The MSX-UNAPI specification is
described [MSX%20UNAPI%20specification%201.1.md](./MSX%20UNAPI%20specification%201.1.md).

This document describes an UNAPI compliant API intended for software that implements
SSH protocol and subsystems, that is, software that provides a secure network protocol.
The functionality provided by this API is focused mainly on communicating with other
computers by using PTY subsystem. This specification might be expanded later to allow
other subsystems like SFTP.

The intended client software applications for this API are networking related applications
such as SSH terminal. This document is targeted at both developers of SSH UNAPI implementations,
and developers of client applications for these implementations.

### 1.1. Design goals

There were two main goals when designing this specification:

- *Simplicity*. This specification's intent is to provide the simplest API that will allow
to develop useful SSH applications for MSX computers.

- *Modularity*. Most of the capabilities provided by this API are optional, and there
are means to get information about which capabilities are supported by a given
implementation. This allows to create from minimal to complete
implementations, as well as providing a clean way to develop an implementation
in an incremental way.

### 1.2. Specification scope

In order to achieve the simplicity goal, this specification deals with the most basic
capabilities required in order to develop SSH networking applications. These capabilities
are:

- Openning a SSH connections.
- Communicating via PTY, SFTP, SCP subsystems.
- RAW Communication without any subsystem.

### 1.3. Modularity

In order to achieve the modularity goal, most of the capabilities defined in this
specification are optional; implementations may choose to implement the full
specification, or only a subset of it. Of course, the less capabilities are implemented by a
given implementation, the greater are the chances that a particular client application will
not work with it, especially the most basic capabilities. For example, an implementation
not providing any support for PTY subsystem will not be very useful; on the other hand,
an implementation that supports PTY but does not support public key authentication
will probably still work fine with most client applications.

The modularity feature is implemented in two ways:

1. There is a routine, [SSH_GET_CAPAB](#412-ssh_get_capab-get-information-about-the-ssh-capabilities-and-features), that returns a
"capabilities vector". This vector holds one bit for each capability that is defined
in this specification; when the bit is set it means that the capability is
implemented.
2. All routines defined in this specification return an error code in register A. One of
these codes is "Not implemented", and is returned whenever a routine related to
an unimplemented capability is invoked (certain routines return this error or not
depending on the input parameters).

For more details on which routines can be invoked (and how) depending on the
supported capabilities, see the routines descriptions themselves.

## 2. API identifier and version

The API identifier for the specification described in this document is: "SSH" (without
the quotes). Remember that per the UNAPI specification, API identifiers are caseinsensitive.
The SSH API version described in this document is 1.0. This is the API specification
version that the mandatory implementation information routine must return in DE (see
[UNAPI_GET_INFO](#411-unapi_get_info-obtain-the-implementation-name-and-version)).

## 3. Error codes

All routines defined in this specification return an error code in register A. This section
lists all the possible error codes; the numeric value, a mnemonic and a short description
is provided for each one. Each routine description has an errors section which explains
with detail which error codes can be returned for that routine, and for which reasons is
each one returned. Return codes are shared with TCP/IP UNAPI specification since SSH is a
network protocol on top of TCP/IP. Any future SSH specific error codes will use the range
of 127 to 255.

| Code | Mnemonic | Description |
| --- | --- | --- |
| 0 | ERR_OK | Operation completed successfully |
| 1 | ERR_NOT_IMP | Capability not implemented |
| 2 | ERR_NO_NETWORK | No network connection available |
| 3 | ERR_NO_DATA | No incoming data available |
| 4 | ERR_INV_PARAM | Invalid input parameter |
| 6 | ERR_INV_IP | Invalid IP address |
| 9 | ERR_NO_FREE_CONN | No free connections available |
| 10 | ERR_CONN_EXISTS | Connection already exists |
| 11 | ERR_NO_CONN | Connection does not exists |
| 12 | ERR_CONN_STATE | Invalid connection state |
| 13 | ERR_BUFFER | Insufficient output buffer space |
| 15 | ERR_INV_OPER | Invalid operation |
| 127 | SSH_ERR_NO_RSS | Not enough memory to create a new session |
| 128 | SSH_ERR_INV_KEY | The key used as password is invalid format |
| 129 | SSH_ERR_PWD | The key or password used to authenticate session was not accepted. |
| 130 | SSH_ERR_PTY_REQ | An error occurred while requesting a shell for PTY |

## 4. API routines

This version of the SSH API consists of routines that enables usage of PTY, SFTP, SCP
and RAW subsystem routines, which are described below. API implementations may define
their own additional implementation-specific routines, as described in the MSX-UNAPI
specification.

Routines are grouped in subsections by related behavior. Useful information concerning
all the routines on a given subsection is provided at the beginning of each subsection.
Some routines exchange data with the client application by using a memory buffer. Per
the UNAPI specification, implementations may not allow the destination address to be a
page 1 address (in the range 4000h-7FFFh). Client software should not use this range as
destination address when invoking these routines, in order to correctly interoperate with
such implementations.

### 4.1. Information gathering routines

These routines allow to obtain various information about the implementation capabilities,
working parameters, and current state.

### 4.1.1. UNAPI_GET_INFO: Obtain the implementation name and version

- Input:
  - A = 0

- Output:
  - A = Error code
  - HL = Address of the implementation name string
  - DE = API specification version supported. D=primary, E=secondary.
  - BC = API implementation version. B=primary, C=secondary.

This routine is mandatory for all implementations of all UNAPI compliant APIs. It returns
basic information about the implementation itself: the implementation version, the
supported API version, and a pointer to the implementation description string.

The implementation name string must be placed in the same slot or segment of the
implementation code (or in page 3), must be zero terminated, must consist of printable
characters, and must be at most 63 characters long (not including the terminating zero).
Refer to the MSX-UNAPI specification for more details.

**ERROR CODES**

This routine never fails. ERR_OK is always returned.

### 4.1.2. SSH_GET_CAPAB: Get information about the SSH capabilities and features

- Input:
  - A = 1
  - B = Index of information block to retrieve:
    - 1: Capabilities information block 1

- Output:
    -  A = Error code

    When information block 1 requested:

    - HL = Capabilities flags
    - B = Maximum simultaneous SSH connections supported
    - C = Free SSH connections currently available

    Other information blocks are not currently supported in this specification. There is
    a provision for multiple blocks just in case those are needed in future.

As explained in ["Modularity"](#13-modularity), the SSH UNAPI specification is modular, meaning that
implementators may choose to include only a certain funcionality subset in the
developed implementations. This is the routine that gives information about the
capabilities actually available in the implementation in which it is invoked. It also
provides information about other implementation working parameters that may be useful
for client applications.

The **capabilities flags** is the most important piece of information, and should be
retrieved by all client applications at startup time, before trying to actually perform any
SSH related operation. It consists of a bitfield in which each bit is associated to one of
the capabilities provided by the routines described in this specification. When the bit is
one, the capability is available, and client applications can safely invoke the routines that
provide the capability. When the bit is zero, the capability is not implemented, and trying
to invoke any of the associated routines will result in the routine returning a
ERR_NOT_IMP error code. (Some routines depend on a given capability or not depending
on the input parameters; more details are given on each routine description).

The set of capabilities flags returned when information block 1 is requested is as follows.
Bit 0 is LSB of register L, bit 8 is LSB of register H.

- Bit 0: Supports PTY subsystem
- Bit 1: Supports SFTP subsystem
- Bit 2: Supports SCP subsystem
- Bit 3: Supports RAW connection
- Bit 4: Always set to 0
- Bit 5: Always set to 0
- Bit 6: Always set to 0
- Bit 7: Always set to 0
- Bit 8: Built-in TCP/IP
- Bit 9: Connection pool shared with built-in TCP/IP
- Bit 10: Supports Public Key Authentication
- Bit 11: Always set to 0
- Bit 12: Always set to 0
- Bit 13: Always set to 0
- Bit 14: Always set to 0
- Bit 15: Always set to 0

Note that SSH might be implemented on the same device that already supports TCP/IP UNAPI. In such
case capabilities bit 8 will be set. If not set, you need a TCP/IP UNAPI device so the SSH solution
can connect and communicate over network. If built-in, and bit 9 is set, this means that when opening
a SSH connection the count of TCP/IP connections available in the UNAPI TCP/IP adapter will decrease
and that the number of available SSH connections will decrease when opening a TCP/IP connection.

**ERROR CODES**

- ERR_OK

The requested information block has been returned.

- ERR_INV_PAR

Invalid information block index specified.

### 4.2. SSH common routines

Those are routines that are either used for all subsystems (OPEN and CLOSE) or
relevant for more than one subsystem (STATE, SEND and RCV are used for PTY and RAW).

#### 4.2.1. SSH_OPEN: Open an SSH client connection

- Input:
  - A = 2
  - HL = Address of the parameters block

- Output:
  - A = Error code
  - B = Connection number

This routine opens a new SSH client connection to a remote server using the information
specified in the parameters block. The format of this block is:

- +0 (4): Remote IP address
- +4 (2): Remote port
- +6 (1): Subsystem:
  - 0: PTY
  - 1: SFTP
  - 2: SCP
  - 3: RAW
- +7 (1): Flags:
  - bit 0: Authentication method (0 = password, 1 = public key)
  - bit 1: Anonymous Authentication
  - bits 1-7: Unused, must be zero
  +8 (variable, use two 00's if Anonymous Auth):
    - 0 to X: username, zero terminated
    - X to end: password or public key, zero terminated

This routine initiates the SSH connection process. The implementation must establish a
TCP connection to the specified remote IP address and port, perform the SSH handshake,
and authenticate using the provided credentials.
Once the SSH session is established and a channel is open, all operations needed for the
requested subsystem are performed, i.e.: for PTY, a pseudo-terminal (PTY) is requested
with the terminal type and window size previously set via SSH_TERM_TYPE and SSH_WIN_SIZE
(or the defaults), and then a remote shell is started.
Data can then be exchanged using SSH_SEND and SSH_RCV for PTY and RAW connections.
For SFTP and SCP subsystems, use the dedicated functions described in their respective sections.

Only one SSH connection can exist per connection number. If the connection is opened
successfully, the returned connection number must be used in all subsequent SSH-related
routine calls.

**ERROR CODES**

- ERR_OK

The SSH connection has been initiated. The returned connection number is valid.

- ERR_NOT_IMP

The implementation does not support the requested subsystem, or, public key authentication
was requested but the implementation does not support it (as indicated by the appropriate
capability flag).

- ERR_INV_PARAM

  - An invalid parameter was specified.
  - An unused flag was set.
  - Zero was specified as the remote IP address.

- ERR_NO_NETWORK

No network connectivity is currently available.

- ERR_NO_FREE_CONN

There are no free connections available.

- ERR_NO_CONN

The TCP connection to the remote server could not be established, or the SSH handshake
or authentication failed.

- SSH_ERR_NO_RSS

There is not enough memory on the device handling SSH connections to create a new session.

- SSH_ERR_INV_KEY

The key to be used as password is not in a valid format.

- SSH_ERR_PWD

The key or password used to authenticate session was not accepted.

- SSH_ERR_PTY_REQ

An error occurred while requesting a shell for PTY.

---

#### 4.2.2. SSH_CLOSE: Close an SSH connection

- Input:
  - A = 3
  - B = Connection number

- Output:
  - A = Error code

This routine closes the specified SSH connection. The SSH session is terminated, and
the connection number is freed.

**ERROR CODES**

- ERR_OK

The SSH connection has been closed.

- ERR_NO_CONN

There is no SSH connection open with the specified number.

---

#### 4.2.3. SSH_STATE: Get the state of an SSH connection

- Input:
  - A = 4
  - B = Connection number

- Output:
  - A = Error code
  - B = Connection state
  - HL = Number of total available incoming bytes

This routine returns information about the current state of an open SSH connection.

The **connection state** value encodes the current state of the SSH session. It will be
one of the following values:

- 0: Closed — No SSH session exists for this connection number
- 1: Connecting — The underlying TCP connection is being established
- 2: Authenticating — TCP connected, SSH handshake and authentication in progress
- 3: Connected — SSH session established, channel open, data can be exchanged
- 4: Error — An error occurred during connection, authentication, or data exchange

The **Number of total available incoming bytes** value indicates how many bytes have
been received from the SSH channel and can be retrieved by using the SSH_RCV routine.

**ERROR CODES**

- ERR_OK

The requested information has been retrieved.

- ERR_NO_CONN

There is no SSH connection open with the specified number.

---

#### 4.2.4. SSH_SEND: Send data to an SSH connection

- Input:
  - A = 5
  - B = Connection number
  - DE = Address of the data to send
  - HL = Data length

- Output:
  - A = Error code

This routine sends data to the SSH channel associated with the specified connection.
This works for PTY and RAW subsystems. For other subsystems use only the functions
designated for it.

**ERROR CODES**

- ERR_OK

The data has been sent to the SSH channel.

- ERR_NO_CONN

There is no SSH connection open with the specified number.

- ERR_CONN_STATE

The SSH connection is not in the "Connected" state or failure while sending.

- ERR_BUFFER

Insufficient buffer space to queue the data for sending.

---

#### 4.2.5. SSH_RCV: Receive data from an SSH connection

- Input:
  - A = 6
  - B = Connection number
  - DE = Address for the received data
  - HL = Maximum data size to retrieve

- Output:
  - A = Error code
  - BC = Number of bytes actually retrieved

This routine retrieves incoming data from the SSH channel associated with the specified
connection. This works for PTY and RAW subsystems. For other subsystems use only the
functions designated for it.

The maximum number of bytes that can be retrieved is specified in HL. The actual number
of bytes retrieved is returned in BC. If no data is available, ERR_NO_DATA is returned.

**ERROR CODES**

- ERR_OK

Data has been retrieved and copied to the specified address.

- ERR_NO_CONN

There is no SSH connection open with the specified number.

- ERR_CONN_STATE

The SSH connection is not in the "Connected" state or failure while reading.

- ERR_NO_DATA

No incoming data is available.

---

### 4.3. SSH PTY specific routines

#### 4.3.1. SSH_TERM_TYPE: Get/set the SSH terminal type

- Input:
  - A = 7
  - C = Operation:
    - 0: Get the current terminal type
    - 1: Set the terminal type
  - D = Terminal type (only when C=1)

- Output (when C=0):
  - A = Error code
  - D = Terminal type

- Output (when C=1):
  - A = Error code

This routine gets or sets the terminal type string that is sent to the remote SSH server
when requesting a pseudo-terminal (PTY). The terminal type tells the remote server how
to format the output and which control sequences the client terminal understands. The
application calling this function is responsible for actually emulating this terminal type;
the implementation only stores the value and sends it to the remote server.

The following terminal types are defined:

| Value | Description |
| --- | --- |
| 0 | VT-52 |
| 1 | ANSI (16 colors) |
| 2 | xterm |
| 3–255 | Reserved for future terminal types |

When a new SSH connection is opened, the terminal type is set to VT-52 (0) by default,
unless it has been explicitly changed before opening the connection. This setting DOES NOT
apply to currently open PTY sessions, only for connections created after setting it. Get
DOES NOT read the currently open PTY session value, but the value set to be used for the
next connection created.

**ERROR CODES**

- ERR_OK

The operation completed successfully.

- ERR_NOT_IMP

There is no PTY subsystem capability.

- ERR_INV_PARAM

  - An invalid value was specified for the operation (C).
  - An invalid terminal type was specified.

---

#### 4.3.2. SSH_WIN_SIZE: Get/set the SSH window size

- Input:
  - A = 8
  - C = Operation:
    - 0: Get the current window size
    - 1: Set the window size
  - H = Number of rows (only when C=1)
  - L = Number of columns (only when C=1)

- Output (when C=0):
  - A = Error code
  - H = Number of rows
  - L = Number of columns

- Output (when C=1):
  - A = Error code

This routine gets or sets the terminal window size (number of rows and columns) that is
sent to the remote SSH server. The server may use this information to format its output
(for example, pagination or line wrapping).

When a new SSH connection is opened, the window size is set to 24 rows and 40 columns
by default, unless it has been explicitly changed before opening the connection. This
setting DOES NOT apply to currently open PTY sessions, only for connections created
after setting it. Get DOES NOT read the currently open PTY session value, but the value
set to be used for the next connection created.

**ERROR CODES**

- ERR_OK

The operation completed successfully.

- ERR_NOT_IMP

There is no PTY subsystem capability.

- ERR_INV_PARAM

  - An invalid value was specified for the operation (C).
  - Zero was specified for rows or columns.

### 4.4. SSH SFTP specific routines

To be defined in a future revision

### 4.5. SSH SCP specific routines

To be defined in a future revision

## 5. Change log

This section lists the changes introduced in all the existing versions of the specification.

### Version 1.0

- First version