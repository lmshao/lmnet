/**
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2025 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef LMSHAO_LMNET_PACKET_ORDERER_H
#define LMSHAO_LMNET_PACKET_ORDERER_H

#include <WinSock2.h>
#include <lmcore/data_buffer.h>

#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <mutex>

namespace lmshao::lmnet {

using lmshao::lmcore::DataBuffer;

/**
 * @brief Packet reordering buffer for Windows IOCP UDP implementation
 *
 * IOCP completion order is non-deterministic when multiple concurrent WSARecvFrom
 * operations are posted. This class ensures packets are delivered in the order they
 * were submitted for receiving, based on an internal sequence number assigned at
 * submission time.
 *
 * @note This is specific to Windows IOCP and not needed for Linux epoll which has
 *       single-threaded event loop guaranteeing order naturally.
 */
class PacketOrderer {
public:
    using DeliveryCallback = std::function<void(std::shared_ptr<DataBuffer>, const sockaddr_storage &, int)>;

    /**
     * @brief Construct a packet orderer
     * @param callback Callback to invoke when packets are ready in order
     * @param maxBufferSize Maximum number of out-of-order packets to buffer (default: 64)
     * @param gapTimeout Timeout for waiting on missing packets (default: 100ms)
     */
    explicit PacketOrderer(DeliveryCallback callback, size_t maxBufferSize = 64,
                           std::chrono::milliseconds gapTimeout = std::chrono::milliseconds(100))
        : callback_(std::move(callback)), maxBufferSize_(maxBufferSize), gapTimeout_(gapTimeout), nextExpectedSeq_(0)
    {
    }

    ~PacketOrderer() = default;

    /**
     * @brief Submit a received packet for ordering
     * @param seq Internal sequence number (assigned at WSARecvFrom submission time)
     * @param buffer Packet data
     * @param addr Source address
     *
     * This method is thread-safe and can be called from multiple IOCP worker threads.
     */
    void SubmitPacket(uint64_t seq, std::shared_ptr<DataBuffer> buffer, const sockaddr_storage &addr, int addrLen)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        // Store the packet with metadata
        pendingPackets_[seq] = PacketInfo{buffer, addr, addrLen, std::chrono::steady_clock::now()};

        // Try to deliver consecutive packets
        DeliverConsecutivePackets();

        // Handle timeout and buffer overflow
        EnforceConstraints();
    }

    /**
     * @brief Get statistics about buffered packets
     */
    size_t GetBufferedCount() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return pendingPackets_.size();
    }

    /**
     * @brief Get the next expected sequence number
     */
    uint64_t GetNextExpectedSeq() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return nextExpectedSeq_;
    }

private:
    struct PacketInfo {
        std::shared_ptr<DataBuffer> buffer;
        sockaddr_storage addr;
        int addrLen{sizeof(sockaddr_storage)};
        std::chrono::steady_clock::time_point arrivalTime;
    };

    /**
     * @brief Deliver all consecutive packets starting from nextExpectedSeq_
     */
    void DeliverConsecutivePackets()
    {
        while (!pendingPackets_.empty()) {
            auto it = pendingPackets_.find(nextExpectedSeq_);
            if (it == pendingPackets_.end()) {
                // Gap detected - missing packet, wait for it
                break;
            }

            // Deliver this packet in order
            if (callback_) {
                callback_(it->second.buffer, it->second.addr, it->second.addrLen);
            }

            pendingPackets_.erase(it);
            nextExpectedSeq_++;
        }
    }

    /**
     * @brief Handle timeout and buffer size constraints
     */
    void EnforceConstraints()
    {
        auto now = std::chrono::steady_clock::now();

        // 1. Check for timeout: if oldest buffered packet is too old, skip the gap
        if (!pendingPackets_.empty()) {
            auto oldestIt = pendingPackets_.begin();
            auto age = now - oldestIt->second.arrivalTime;

            if (age > gapTimeout_) {
                // Timeout - skip to the oldest packet we have
                nextExpectedSeq_ = oldestIt->first;
                DeliverConsecutivePackets();
            }
        }

        // 2. Check buffer size: if too many packets buffered, force delivery
        while (pendingPackets_.size() > maxBufferSize_) {
            // Force skip to oldest packet
            auto oldestIt = pendingPackets_.begin();
            nextExpectedSeq_ = oldestIt->first;
            DeliverConsecutivePackets();
        }
    }

    DeliveryCallback callback_;
    size_t maxBufferSize_;
    std::chrono::milliseconds gapTimeout_;

    mutable std::mutex mutex_;
    std::map<uint64_t, PacketInfo> pendingPackets_;
    uint64_t nextExpectedSeq_;
};

} // namespace lmshao::lmnet

#endif // LMSHAO_LMNET_PACKET_ORDERER_H
