#pragma once

/**
 * @file src/core/io/SwAbstractSocket.h
 * @ingroup core_io
 * @brief Declares the public interface exposed by SwAbstractSocket in the CoreSw IO layer.
 *
 * This header belongs to the CoreSw IO layer. It defines files, sockets, servers, descriptors,
 * processes, and network helpers that sit directly at operating-system boundaries.
 *
 * Within that layer, this file focuses on the abstract socket interface. The declarations exposed
 * here define the stable surface that adjacent code can rely on while the implementation remains
 * free to evolve behind the header.
 *
 * The main declarations in this header are SwAbstractSocket.
 *
 * Socket-oriented declarations here abstract OS-level descriptors and expose the read, write,
 * connection, and readiness semantics that higher layers build upon.
 *
 * IO-facing declarations here usually manage handles, readiness state, buffering, and error
 * propagation while presenting a portable framework API.
 *
 */

/***************************************************************************************************
 * This file is part of a project developed by Eymeric O'Neill.
 *
 * Copyright (C) 2025 Ariya Consulting
 * Author/Creator: Eymeric O'Neill
 * Contact: +33 6 52 83 83 31
 * Email: eymeric.oneill@gmail.com
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 ***************************************************************************************************/

#define _WINSOCKAPI_

#include "SwIODevice.h"
#include "SwString.h"

/**
 * @class SwAbstractSocket
 * @brief Abstract base class for socket-based communication.
 *
 * This class defines the interface and common behavior for socket operations,
 * including connecting to a host, reading and writing data, and handling socket state transitions.
 *
 * ### Key Features:
 * - Abstract methods for connecting, reading, writing, and waiting for socket events.
 * - Signal-based architecture to notify about important events such as connection establishment, data availability, and errors.
 * - Manages the socket's state with an internal `SocketState` enum.
 *
 * @note This class is intended to be subclassed to implement specific socket protocols (e.g., TCP, UDP).
 */
class SwAbstractSocket : public SwIODevice {
    SW_OBJECT(SwAbstractSocket, SwIODevice)
public:

    /**
     * @enum SocketState
     * @brief Represents the various states a socket can be in.
     *
     * - `UnconnectedState`: The socket is not connected.
     * - `HostLookupState`: The socket is resolving the host address.
     * - `ConnectingState`: The socket is attempting to connect to a host.
     * - `ConnectedState`: The socket is connected to a host.
     * - `BoundState`: The socket is bound to an address and port.
     * - `ListeningState`: The socket is in listening mode (server sockets).
     * - `ClosingState`: The socket is closing.
     */
    enum SocketState {
        UnconnectedState,
        HostLookupState,
        ConnectingState,
        ConnectedState,
        BoundState,
        ListeningState,
        ClosingState
    };

    /**
     * @brief Constructs an abstract socket object.
     *
     * @param parent A pointer to the parent SwObject. Defaults to `nullptr`.
     *
     * @details Initializes the socket state to `UnconnectedState`.
     */
    SwAbstractSocket(SwObject* parent = nullptr)
        : SwIODevice(parent), m_state(UnconnectedState)
    {
    }

    /**
     * @brief Virtual destructor for `SwAbstractSocket`.
     *
     * Ensures proper cleanup in derived classes.
     */
    virtual ~SwAbstractSocket() {}

    /**
     * @brief Attempts to connect to the specified host and port.
     *
     * @param host The hostname or IP address to connect to.
     * @param port The port number on the host.
     *
     * @return `true` if the connection is initiated successfully, `false` otherwise.
     *
     * @note This is a pure virtual method and must be implemented in derived classes.
     */
    virtual bool connectToHost(const SwString& host, uint16_t port) = 0;

    /**
     * @brief Waits for the socket to establish a connection within the specified timeout.
     *
     * @param msecs The maximum time to wait, in milliseconds. Defaults to 30,000 ms (30 seconds).
     *
     * @return `true` if the socket successfully connects within the timeout, `false` otherwise.
     *
     * @note This is a pure virtual method and must be implemented in derived classes.
     */
    virtual bool waitForConnected(int msecs = 30000) = 0;

    /**
     * @brief Waits for data in the write buffer to be sent within the specified timeout.
     *
     * @param msecs The maximum time to wait, in milliseconds. Defaults to 30,000 ms (30 seconds).
     *
     * @return `true` if the data is sent within the timeout, `false` otherwise.
     *
     * @note This is a pure virtual method and must be implemented in derived classes.
     */
    virtual bool waitForBytesWritten(int msecs = 30000) = 0;

    /**
     * @brief Closes the socket and releases associated resources.
     *
     * @note This is a pure virtual method and must be implemented in derived classes.
     */
    virtual void close() override = 0;

    /**
     * @brief Reads data from the socket.
     *
     * @param maxSize The maximum number of bytes to read. Defaults to `0`, which indicates no limit.
     *
     * @return A `SwString` containing the data read from the socket.
     *
     * @note This is a pure virtual method and must be implemented in derived classes.
     */
    virtual SwString read(int64_t maxSize = 0) override = 0;

    /**
     * @brief Writes data to the socket.
     *
     * @param data The data to write, provided as a `SwString`.
     *
     * @return `true` if the data was successfully queued for sending, `false` otherwise.
     *
     * @note This is a pure virtual method and must be implemented in derived classes.
     */
    virtual bool write(const SwString& data) override = 0;

    /**
     * @brief Checks if the socket is currently open.
     *
     * @return `true` if the socket is in the `ConnectedState`, `false` otherwise.
     */
    virtual bool isOpen() const override {
        return (m_state == ConnectedState);
    }

    /**
     * @brief Returns the current state of the socket.
     *
     * @return The current `SocketState` of the socket.
     */
    SocketState state() const {
        return m_state;
    }

signals:
    DECLARE_SIGNAL_VOID(connected)            ///< Emitted when the socket successfully establishes a connection.
    DECLARE_SIGNAL_VOID(disconnected)         ///< Emitted when the socket is disconnected.
    DECLARE_SIGNAL(errorOccurred, int)        ///< Emitted when a socket error occurs. The error code is passed as an argument.
    DECLARE_SIGNAL_VOID(writeFinished)        ///< Emitted when all data in the write buffer has been sent.

protected:
    /**
     * @brief Sets the socket's state.
     *
     * @param newState The new state to assign to the socket.
     *
     * @details This method is protected and is intended to be used by derived classes to update the socket's state.
     */
    void setState(SocketState newState) {
        m_state = newState;
    }

private:
    SocketState m_state; ///< Holds the current state of the socket, represented by the `SocketState` enum.
};
