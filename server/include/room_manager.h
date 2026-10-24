// room_manager.h - Thread-safe registry and membership tracking for chat rooms.
// Supports creating, joining, leaving, querying, and messaging within rooms.

#pragma once
#ifndef ROOM_MANAGER_H
#define ROOM_MANAGER_H

#include "net_compat.h"
#include <string>
#include <vector>
#include <mutex>
#include <memory>
#include <atomic>
#include <algorithm>
#include "protocol.h"
#include "protocol_message.h"

struct RoomMember {
    SOCKET socket{INVALID_SOCKET};
    std::string username;
    std::shared_ptr<std::mutex> sendMutex;
    std::shared_ptr<std::atomic<bool>> isClosed;
    protocol::ProtocolVersion protocolVersion{protocol::ProtocolVersion::STRUCTURED_V1};
};

struct Room {
    std::string name;          // Preserves creator's casing for display
    std::string creator;       // Username of creator
    int64_t createdAt{0};      // Timestamp
    std::vector<RoomMember> members;
};

struct RoomLeaveEvent {
    std::string roomName;
    std::vector<RoomMember> remainingMembers;
};

class RoomManager {
public:
    RoomManager() = default;
    ~RoomManager() = default;

    RoomManager(const RoomManager&) = delete;
    RoomManager& operator=(const RoomManager&) = delete;

    // Create a new room with creator auto-joined as first member
    bool createRoom(const std::string& roomName, const std::string& creatorUsername,
                    const RoomMember& creatorMember, int64_t timestamp,
                    std::string& outErrCode, std::string& outErrMsg);

    // Join an existing room
    bool joinRoom(const std::string& roomName, const RoomMember& member,
                  std::vector<RoomMember>& outPeerTargets, std::string& outCanonicalRoom,
                  std::string& outErrCode, std::string& outErrMsg);

    // Leave a room
    bool leaveRoom(const std::string& roomName, SOCKET socket,
                   std::vector<RoomMember>& outRemainingTargets, std::string& outCanonicalRoom,
                   std::string& outErrCode, std::string& outErrMsg);

    // Get room members for messaging (excluding the sender)
    bool getRoomPeers(const std::string& roomName, SOCKET senderSocket,
                      std::vector<RoomMember>& outPeerTargets, std::string& outCanonicalRoom,
                      std::string& outErrCode, std::string& outErrMsg) const;

    // Get list of active rooms
    std::vector<std::string> getRoomNames() const;

    // Get member usernames of a room (requester must be an authorized member)
    bool getRoomMemberNames(const std::string& roomName, SOCKET requesterSocket,
                            std::vector<std::string>& outMembers, std::string& outCanonicalRoom,
                            std::string& outErrCode, std::string& outErrMsg) const;

    // Remove client from all rooms on disconnect (empty rooms automatically deleted)
    void removeClientFromAllRooms(SOCKET socket, std::vector<RoomLeaveEvent>& outEvents);

    // Check if room exists
    bool hasRoom(const std::string& roomName) const;

    // Query active room count
    size_t getRoomCount() const;

private:
    mutable std::mutex m_mutex;
    std::vector<Room> m_rooms;

    std::vector<Room>::iterator findRoomIter(std::string_view roomName);
    std::vector<Room>::const_iterator findRoomIter(std::string_view roomName) const;
};

#endif // ROOM_MANAGER_H
