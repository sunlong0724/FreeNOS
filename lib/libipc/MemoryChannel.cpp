/*
 * Copyright (C) 2015 Niek Linnenbank
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <Log.h>
#include <FreeNOS/System.h>
#include "MemoryChannel.h"

MemoryChannel::MemoryChannel(const Channel::Mode mode, const Size messageSize)
    : Channel(mode, messageSize)
    , m_maximumMessages((PAGESIZE / messageSize) - 1U)
{
    assert(messageSize >= sizeof(RingHead));
    assert(messageSize < (PAGESIZE / 2));

    MemoryBlock::set(&m_head, 0, sizeof(m_head));
}

MemoryChannel::~MemoryChannel()
{
}

MemoryChannel::Result MemoryChannel::setVirtual(const Address data, const Address feedback)
{
    m_data.setBase(data);
    m_feedback.setBase(feedback);
    return Success;
}

MemoryChannel::Result MemoryChannel::setPhysical(const Address data, const Address feedback)
{
    IO::Result result = m_data.map(data, PAGESIZE);
    if (result != IO::Success)
    {
        ERROR("failed to map data physical address " << (void*)data << ": " << (int)result);
        return IOError;
    }

    result = m_feedback.map(feedback, PAGESIZE);
    if (result != IO::Success)
    {
        ERROR("failed to map feedback physical address " << (void*)feedback << ": " << (int)result);
        return IOError;
    }

    return Success;
}

MemoryChannel::Result MemoryChannel::read(void *buffer)
{
    RingHead head;

    // Read the current ring head
    m_data.read(0, sizeof(head), &head);

    // Check if a message is present
    if (head.index == m_head.index)
        return NotFound;

    // Read one message
    m_data.read((m_head.index+1) * m_messageSize, m_messageSize, buffer);

    // Increment head index
    m_head.index = (m_head.index + 1) % m_maximumMessages;

    // Update read index
    m_feedback.write(0, sizeof(m_head), &m_head);
    return Success;
}

MemoryChannel::Result MemoryChannel::write(const void *buffer)
{
    RingHead reader;

    // Read current ring head
    m_feedback.read(0, sizeof(RingHead), &reader);

    // Check if buffer space is available for the message
    if (((m_head.index + 1) % m_maximumMessages) == reader.index)
        return ChannelFull;

    // write the message
    m_data.write((m_head.index+1) * m_messageSize, m_messageSize, buffer);

    // Increment write index
    m_head.index = (m_head.index + 1) % m_maximumMessages;
    m_data.write(0, sizeof(m_head), &m_head);
    return Success;
}

MemoryChannel::Result MemoryChannel::flush()
{
    // Cannot flush caches in usermode. All usermode code
    // should memory map without caching.
    if (!isKernel)
        return IOError;

    // Clean both pages from the cache
    Arch::Cache cache;
    cache.cleanData(m_data.getBase());
    cache.cleanData(m_feedback.getBase());
    return Success;
}
