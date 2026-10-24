// room_manager.cpp - Implementation of the RoomManager class.
// Thread-safe registry and membership tracking for chat rooms.

#include "room_manager.h"

std::vector<Room>::iterator RoomManager::findRoomIter(std::string_view roomName) {
    return std::find_if(m_rooms.begin(), m_rooms.end(), [roomName](const Room& r) {
        return protocol::equalsIgnoreCase(r.name, roomName);
    });
}

std::vector<Room>::const_iterator RoomManager::findRoomIter(std::string_view roomName) const {
    return std::find_if(m_rooms.begin(), m_rooms.end(), [roomName](const Room& r) {
        return protocol::equalsIgnoreCase(r.name, roomName);
    });
}

bool RoomManager::createRoom(const std::string& roomName, const std::string& creatorUsername,
                             const RoomMember& creatorMember, int64_t timestamp,
                             std::string& outErrCode, std::string& outErrMsg) {
    if (!protocol::isValidRoomName(roomName)) {
        outErrCode = std::string(protocol::ERR_INVALID_ROOM_NAME);
        outErrMsg = "Invalid room name format";
        return false;
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    if (findRoomIter(roomName) != m_rooms.end()) {
        outErrCode = std::string(protocol::ERR_ROOM_ALREADY_EXISTS);
        outErrMsg = "Room '" + roomName + "' already exists";
        return false;
    }

    Room r;
    r.instanceId = m_nextInstanceId.fetch_add(1);
    r.name = roomName;
    r.creator = creatorUsername;
    r.createdAt = timestamp;
    r.members.push_back(creatorMember);
    m_rooms.push_back(std::move(r));

    return true;
}

bool RoomManager::joinRoom(const std::string& roomName, const RoomMember& member,
                           std::vector<RoomMember>& outPeerTargets, std::string& outCanonicalRoom,
                           std::string& outErrCode, std::string& outErrMsg) {
    if (!protocol::isValidRoomName(roomName)) {
        outErrCode = std::string(protocol::ERR_INVALID_ROOM_NAME);
        outErrMsg = "Invalid room name format";
        return false;
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = findRoomIter(roomName);
    if (it == m_rooms.end()) {
        outErrCode = std::string(protocol::ERR_ROOM_NOT_FOUND);
        outErrMsg = "Room '" + roomName + "' does not exist";
        return false;
    }

    for (const auto& m : it->members) {
        if (m.socket == member.socket) {
            outErrCode = std::string(protocol::ERR_ALREADY_IN_ROOM);
            outErrMsg = "Already a member of room '" + it->name + "'";
            return false;
        }
    }

    outCanonicalRoom = it->name;
    outPeerTargets = it->members; // Snapshot current peers before inserting new member
    it->members.push_back(member);

    return true;
}

bool RoomManager::leaveRoom(const std::string& roomName, SOCKET socket,
                            std::vector<RoomMember>& outRemainingTargets, std::string& outCanonicalRoom,
                            std::string& outErrCode, std::string& outErrMsg) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = findRoomIter(roomName);
    if (it == m_rooms.end()) {
        outErrCode = std::string(protocol::ERR_ROOM_NOT_FOUND);
        outErrMsg = "Room '" + roomName + "' does not exist";
        return false;
    }

    auto memIt = std::find_if(it->members.begin(), it->members.end(), [socket](const RoomMember& m) {
        return m.socket == socket;
    });

    if (memIt == it->members.end()) {
        outErrCode = std::string(protocol::ERR_NOT_IN_ROOM);
        outErrMsg = "You are not a member of room '" + it->name + "'";
        return false;
    }

    outCanonicalRoom = it->name;
    it->members.erase(memIt);

    if (it->members.empty()) {
        // Empty room auto-deletion policy
        m_rooms.erase(it);
        outRemainingTargets.clear();
    } else {
        outRemainingTargets = it->members;
    }

    return true;
}

bool RoomManager::getRoomPeers(const std::string& roomName, SOCKET senderSocket,
                               std::vector<RoomMember>& outPeerTargets, std::string& outCanonicalRoom,
                               int64_t& outInstanceId,
                               std::string& outErrCode, std::string& outErrMsg) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = findRoomIter(roomName);
    if (it == m_rooms.end()) {
        outErrCode = std::string(protocol::ERR_ROOM_NOT_FOUND);
        outErrMsg = "Room '" + roomName + "' does not exist";
        return false;
    }

    bool isMember = false;
    for (const auto& m : it->members) {
        if (m.socket == senderSocket) {
            isMember = true;
            break;
        }
    }

    if (!isMember) {
        outErrCode = std::string(protocol::ERR_NOT_IN_ROOM);
        outErrMsg = "You are not a member of room '" + it->name + "'";
        return false;
    }

    outInstanceId = it->instanceId;
    outCanonicalRoom = it->name;
    outPeerTargets.reserve(it->members.size());
    for (const auto& m : it->members) {
        if (m.socket != senderSocket) {
            outPeerTargets.push_back(m);
        }
    }

    return true;
}

bool RoomManager::getRoomInstanceId(const std::string& roomName, SOCKET requesterSocket,
                                   int64_t& outInstanceId, std::string& outCanonicalRoom,
                                   std::string& outErrCode, std::string& outErrMsg) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = findRoomIter(roomName);
    if (it == m_rooms.end()) {
        outErrCode = std::string(protocol::ERR_ROOM_NOT_FOUND);
        outErrMsg = "Room '" + roomName + "' does not exist";
        return false;
    }

    bool isMember = false;
    for (const auto& m : it->members) {
        if (m.socket == requesterSocket) {
            isMember = true;
            break;
        }
    }

    if (!isMember) {
        outErrCode = std::string(protocol::ERR_NOT_IN_ROOM);
        outErrMsg = "You are not a member of room '" + it->name + "'";
        return false;
    }

    outInstanceId = it->instanceId;
    outCanonicalRoom = it->name;
    return true;
}

std::vector<std::string> RoomManager::getRoomNames() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<std::string> names;
    names.reserve(m_rooms.size());
    for (const auto& r : m_rooms) {
        names.push_back(r.name);
    }
    return names;
}

bool RoomManager::getRoomMemberNames(const std::string& roomName, SOCKET requesterSocket,
                                     std::vector<std::string>& outMembers, std::string& outCanonicalRoom,
                                     std::string& outErrCode, std::string& outErrMsg) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = findRoomIter(roomName);
    if (it == m_rooms.end()) {
        outErrCode = std::string(protocol::ERR_ROOM_NOT_FOUND);
        outErrMsg = "Room '" + roomName + "' does not exist";
        return false;
    }

    bool isMember = false;
    for (const auto& m : it->members) {
        if (m.socket == requesterSocket) {
            isMember = true;
            break;
        }
    }

    if (!isMember) {
        outErrCode = std::string(protocol::ERR_NOT_IN_ROOM);
        outErrMsg = "You are not a member of room '" + it->name + "'";
        return false;
    }

    outCanonicalRoom = it->name;
    outMembers.reserve(it->members.size());
    for (const auto& m : it->members) {
        outMembers.push_back(m.username);
    }

    return true;
}

void RoomManager::removeClientFromAllRooms(SOCKET socket, std::vector<RoomLeaveEvent>& outEvents) {
    std::lock_guard<std::mutex> lock(m_mutex);

    auto it = m_rooms.begin();
    while (it != m_rooms.end()) {
        auto memIt = std::find_if(it->members.begin(), it->members.end(), [socket](const RoomMember& m) {
            return m.socket == socket;
        });

        if (memIt != it->members.end()) {
            it->members.erase(memIt);
            if (it->members.empty()) {
                // Empty room auto-deletion
                it = m_rooms.erase(it);
                continue;
            } else {
                outEvents.push_back({it->name, it->members});
            }
        }
        ++it;
    }
}

bool RoomManager::hasRoom(const std::string& roomName) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return findRoomIter(roomName) != m_rooms.end();
}

size_t RoomManager::getRoomCount() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_rooms.size();
}
