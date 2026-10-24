// message_store.h - Declaration of MessageStore for SQLite-backed chat persistence.
// Provides thread-safe, RAII-managed storage and retrieval for public, direct, and room messages.

#pragma once
#ifndef MESSAGE_STORE_H
#define MESSAGE_STORE_H

#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include <cstdint>
#include <string_view>

#include "protocol_message.h"

struct sqlite3;
struct sqlite3_stmt;

class MessageStore {
public:
    explicit MessageStore(std::string dbPath = "chat_history.db");
    ~MessageStore();

    MessageStore(const MessageStore&) = delete;
    MessageStore& operator=(const MessageStore&) = delete;

    // Initializes database connection, executes schema setup, queries high-water marks,
    // and prepares SQL statements. Returns false and sets outErr on failure.
    bool init(std::string& outErr);

    // Closes database handle and finalizes statements
    void close();

    // High-water mark accessors to synchronize sequence generators on server restart
    int64_t getLastId() const;
    int64_t getLastRoomInstanceId() const;

    // Message storage operations (returns true on success, false on failure)
    bool storePublicMessage(const std::string& sender,
                            const std::string& content,
                            int64_t timestamp,
                            std::string& outMsgId,
                            std::string& outErr);

    bool storeDirectMessage(const std::string& sender,
                            const std::string& recipient,
                            const std::string& content,
                            int64_t timestamp,
                            std::string& outMsgId,
                            std::string& outErr);

    bool storeRoomMessage(const std::string& sender,
                          const std::string& roomName,
                          int64_t roomInstanceId,
                          const std::string& content,
                          int64_t timestamp,
                          std::string& outMsgId,
                          std::string& outErr);

    // History retrieval operations with deterministic cursor pagination and chronological ordering
    bool getPublicHistory(int limit,
                          std::string_view beforeId,
                          std::string_view afterId,
                          std::vector<protocol::HistoryItem>& outItems,
                          bool& outHasMore,
                          std::string& outErr);

    bool getDirectHistory(const std::string& user1,
                          const std::string& user2,
                          int limit,
                          std::string_view beforeId,
                          std::string_view afterId,
                          std::vector<protocol::HistoryItem>& outItems,
                          bool& outHasMore,
                          std::string& outErr);

    bool getRoomHistory(int64_t roomInstanceId,
                        int limit,
                        std::string_view beforeId,
                        std::string_view afterId,
                        std::vector<protocol::HistoryItem>& outItems,
                        bool& outHasMore,
                        std::string& outErr);

    // Diagnostic / testing hook to simulate write failures
    void setSimulateFailure(bool sim) { m_simulateFailure.store(sim); }

    // Static helper to normalize conversation pairs (e.g. "alice:bob")
    static std::string makeDirectPair(std::string_view u1, std::string_view u2);

    // Static helper to parse numeric message IDs from "msg-<id>" or "<id>"
    static int64_t parseCursorId(std::string_view cursorStr);

private:
    std::string m_dbPath;
    sqlite3* m_db{nullptr};
    mutable std::mutex m_mutex;

    int64_t m_lastId{0};
    int64_t m_lastRoomInstanceId{0};
    std::atomic<bool> m_simulateFailure{false};

    sqlite3_stmt* m_stmtInsert{nullptr};

    void finalizeStatements();
    bool executeSchema(std::string& outErr);
    bool prepareStatements(std::string& outErr);
};

#endif // MESSAGE_STORE_H
