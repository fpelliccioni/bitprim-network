/**
 * Copyright (c) 2011-2017 libbitcoin developers (see AUTHORS)
 *
 * This file is part of libbitcoin.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <bitcoin/network/sessions/session_outbound.hpp>

#include <cstddef>
#include <functional>
#include <bitcoin/bitcoin.hpp>
#include <bitcoin/network/p2p.hpp>
#include <bitcoin/network/protocols/protocol_address_31402.hpp>
#include <bitcoin/network/protocols/protocol_ping_31402.hpp>
#include <bitcoin/network/protocols/protocol_ping_60001.hpp>
#include <bitcoin/network/protocols/protocol_reject_70002.hpp>

namespace libbitcoin {
namespace network {

#define CLASS session_outbound

using namespace std::placeholders;

session_outbound::session_outbound(p2p& network, bool notify_on_connect)
  : session_batch(network, notify_on_connect),
    CONSTRUCT_TRACK(session_outbound)
{
}

// Start sequence.
// ----------------------------------------------------------------------------

void session_outbound::start(result_handler handler)
{
    if (settings_.outbound_connections == 0)
    {
        LOG_INFO(LOG_NETWORK)
            << "Not configured for generating outbound connections.";
        handler(error::success);
        return;
    }

    session::start(CONCURRENT_DELEGATE2(handle_started, _1, handler));
}

void session_outbound::handle_started(const code& ec, result_handler handler)
{
    if (ec)
    {
        handler(ec);
        return;
    }

    for (size_t peer = 0; peer < settings_.outbound_connections; ++peer)
        new_connection();

    // This is the end of the start sequence.
    handler(error::success);
}

// Connnect cycle.
// ----------------------------------------------------------------------------

void session_outbound::new_connection()
{
    if (stopped())
    {
        LOG_DEBUG(LOG_NETWORK)
            << "Suspended outbound connection.";
        return;
    }

    session_batch::connect(BIND2(handle_connect, _1, _2));
}

void session_outbound::handle_connect(const code& ec, channel::ptr channel)
{
    if (ec)
    {
        LOG_DEBUG(LOG_NETWORK)
            << "Failure connecting outbound: " << ec.message();

        if (ec != error::service_stopped)
            new_connection();

        return;
    }

    register_channel(channel,
        BIND2(handle_channel_start, _1, channel),
        BIND2(handle_channel_stop, _1, channel));
}

void session_outbound::handle_channel_start(const code& ec,
    channel::ptr channel)
{
    // The start failure is also caught by handle_channel_stop.
    if (ec)
    {
        LOG_DEBUG(LOG_NETWORK)
            << "Outbound channel failed to start ["
            << channel->authority() << "] " << ec.message();
        return;
    }

    LOG_INFO(LOG_NETWORK)
        << "Connected outbound channel [" << channel->authority() << "] ("
        << connection_count() << ")";

    attach_protocols(channel);
};

void session_outbound::attach_protocols(channel::ptr channel)
{
    const auto version = channel->negotiated_version();

    if (version >= message::version::level::bip31)
        attach<protocol_ping_60001>(channel)->start();
    else
        attach<protocol_ping_31402>(channel)->start();

    if (version >= message::version::level::bip61)
        attach<protocol_reject_70002>(channel)->start();

    attach<protocol_address_31402>(channel)->start();
}

void session_outbound::handle_channel_stop(const code& ec,
    channel::ptr channel)
{
    LOG_DEBUG(LOG_NETWORK)
        << "Outbound channel stopped [" << channel->authority() << "] "
        << ec.message();

    new_connection();
}

// Channel start sequence.
// ----------------------------------------------------------------------------
// Pend outgoing connections so we can detect connection to self.

void session_outbound::start_channel(channel::ptr channel,
    result_handler handle_started)
{
    const result_handler unpend_handler =
        BIND3(do_unpend, _1, channel, handle_started);

    const auto ec = pend(channel);

    if (ec)
    {
        unpend_handler(ec);
        return;
    }

    session::start_channel(channel, unpend_handler);
}

void session_outbound::do_unpend(const code& ec, channel::ptr channel,
    result_handler handle_started)
{
    unpend(channel);
    handle_started(ec);
}

} // namespace network
} // namespace libbitcoin
