// message_store.cpp - Implementation of MessageStore for SQLite-backed chat persistence.

#include "message_store.h"

#include <sqlite3.h>
#include <cstdlib>
#include <cctype>
#include <algorithm>
#include <sstream>

MessageStore::MessageStore(std::string dbPath)
    : m_dbPath(std::move(dbPath)) {
}

MessageStore::~MessageStore() {
    close();
}

void MessageStore::finalizeStatements() {
    if (m_stmtInsert) {
        sqlite3_finalize(m_stmtInsert);
        m_stmtInsert = nullptr;
    }
}

void MessageStore::close() {
    std::lock_guard<std::mutex> lock(m_mutex);
    finalizeStatements();
    if (m_db) {
        sqlite3_close_v2(m_db);
        m_db = nullptr;
    }
}

int64_t MessageStore::getLastId() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_lastId;
}

int64_t MessageStore::getLastRoomInstanceId() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_lastRoomInstanceId;
}

std::string MessageStore::makeDirectPair(std::string_view u1, std::string_view u2) {
    std::string a, b;
    a.reserve(u1.size());
    for (char c : u1) a.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));

    b.reserve(u2.size());
    for (char c : u2) b.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));

    if (a < b) {
        return a + ":" + b;
    } else {
        return b + ":" + a;
    }
}

int64_t MessageStore::parseCursorId(std::string_view cursorStr) {
    if (cursorStr.empty()) {
        return 0;
    }
    if (cursorStr.rfind("msg-", 0) == 0) {
        cursorStr.remove_prefix(4);
    }
    try {
        return std::stoll(std::string(cursorStr));
    } catch (...) {
        return 0;
    }
}

bool MessageStore::executeSchema(std::string& outErr) {
    const char* ddl =
        "PRAGMA journal_mode=WAL;\n"
        "PRAGMA synchronous=NORMAL;\n"
        "PRAGMA foreign_keys=ON;\n"
        "CREATE TABLE IF NOT EXISTS messages (\n"
        "    id INTEGER PRIMARY KEY,\n"
        "    msg_id TEXT UNIQUE NOT NULL,\n"
        "    conv_type TEXT NOT NULL,\n"
        "    sender TEXT NOT NULL,\n"
        "    recipient TEXT,\n"
        "    direct_pair TEXT,\n"
        "    room_name TEXT,\n"
        "    room_instance_id INTEGER,\n"
        "    content TEXT NOT NULL,\n"
        "    created_at INTEGER NOT NULL\n"
        ");\n"
        "CREATE INDEX IF NOT EXISTS idx_messages_public ON messages(conv_type, id ASC);\n"
        "CREATE INDEX IF NOT EXISTS idx_messages_direct ON messages(conv_type, direct_pair, id ASC);\n"
        "CREATE INDEX IF NOT EXISTS idx_messages_room ON messages(conv_type, room_instance_id, id ASC);\n";

    char* errMsg = nullptr;
    int rc = sqlite3_exec(m_db, ddl, nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        outErr = (errMsg != nullptr) ? errMsg : "Failed to execute schema DDL";
        if (errMsg) sqlite3_free(errMsg);
        return false;
    }
    return true;
}

bool MessageStore::prepareStatements(std::string& outErr) {
    const char* sqlInsert =
        "INSERT INTO messages (id, msg_id, conv_type, sender, recipient, direct_pair, room_name, room_instance_id, content, created_at) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";

    int rc = sqlite3_prepare_v2(m_db, sqlInsert, -1, &m_stmtInsert, nullptr);
    if (rc != SQLITE_OK) {
        outErr = sqlite3_errmsg(m_db);
        return false;
    }
    return true;
}

bool MessageStore::init(std::string& outErr) {
    std::lock_guard<std::mutex> lock(m_mutex);

    int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX;
    int rc = sqlite3_open_v2(m_dbPath.c_str(), &m_db, flags, nullptr);
    if (rc != SQLITE_OK) {
        outErr = (m_db != nullptr) ? sqlite3_errmsg(m_db) : "Failed to open SQLite database";
        if (m_db) {
            sqlite3_close_v2(m_db);
            m_db = nullptr;
        }
        return false;
    }

    if (!executeSchema(outErr)) {
        sqlite3_close_v2(m_db);
        m_db = nullptr;
        return false;
    }

    // Retrieve high-water mark for message IDs
    const char* sqlMaxId = "SELECT COALESCE(MAX(id), 0) FROM messages;";
    sqlite3_stmt* stmtMax = nullptr;
    if (sqlite3_prepare_v2(m_db, sqlMaxId, -1, &stmtMax, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmtMax) == SQLITE_ROW) {
            m_lastId = sqlite3_column_int64(stmtMax, 0);
        }
        sqlite3_finalize(stmtMax);
    }

    // Retrieve high-water mark for room instance IDs
    const char* sqlMaxRoom = "SELECT COALESCE(MAX(room_instance_id), 0) FROM messages;";
    stmtMax = nullptr;
    if (sqlite3_prepare_v2(m_db, sqlMaxRoom, -1, &stmtMax, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmtMax) == SQLITE_ROW) {
            m_lastRoomInstanceId = sqlite3_column_int64(stmtMax, 0);
        }
        sqlite3_finalize(stmtMax);
    }

    if (!prepareStatements(outErr)) {
        finalizeStatements();
        sqlite3_close_v2(m_db);
        m_db = nullptr;
        return false;
    }

    return true;
}

bool MessageStore::storePublicMessage(const std::string& sender,
                                     const std::string& content,
                                     int64_t timestamp,
                                     std::string& outMsgId,
                                     std::string& outErr) {
    if (m_simulateFailure.load() || std::getenv("CHAT_SIMULATE_STORAGE_FAILURE") != nullptr) {
        outErr = "Simulated storage failure";
        return false;
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_db || !m_stmtInsert) {
        outErr = "Database not initialized";
        return false;
    }

    int64_t nextId = ++m_lastId;
    std::string msgId = "msg-" + std::to_string(nextId);

    sqlite3_reset(m_stmtInsert);
    sqlite3_clear_bindings(m_stmtInsert);

    sqlite3_bind_int64(m_stmtInsert, 1, nextId);
    sqlite3_bind_text(m_stmtInsert, 2, msgId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(m_stmtInsert, 3, "public", -1, SQLITE_STATIC);
    sqlite3_bind_text(m_stmtInsert, 4, sender.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_null(m_stmtInsert, 5); // recipient
    sqlite3_bind_null(m_stmtInsert, 6); // direct_pair
    sqlite3_bind_null(m_stmtInsert, 7); // room_name
    sqlite3_bind_int64(m_stmtInsert, 8, 0); // room_instance_id
    sqlite3_bind_text(m_stmtInsert, 9, content.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(m_stmtInsert, 10, timestamp);

    int rc = sqlite3_step(m_stmtInsert);
    if (rc != SQLITE_DONE) {
        outErr = sqlite3_errmsg(m_db);
        sqlite3_reset(m_stmtInsert);
        sqlite3_clear_bindings(m_stmtInsert);
        return false;
    }

    sqlite3_reset(m_stmtInsert);
    sqlite3_clear_bindings(m_stmtInsert);

    outMsgId = msgId;
    return true;
}

bool MessageStore::storeDirectMessage(const std::string& sender,
                                     const std::string& recipient,
                                     const std::string& content,
                                     int64_t timestamp,
                                     std::string& outMsgId,
                                     std::string& outErr) {
    if (m_simulateFailure.load() || std::getenv("CHAT_SIMULATE_STORAGE_FAILURE") != nullptr) {
        outErr = "Simulated storage failure";
        return false;
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_db || !m_stmtInsert) {
        outErr = "Database not initialized";
        return false;
    }

    int64_t nextId = ++m_lastId;
    std::string msgId = "msg-" + std::to_string(nextId);
    std::string pair = makeDirectPair(sender, recipient);

    sqlite3_reset(m_stmtInsert);
    sqlite3_clear_bindings(m_stmtInsert);

    sqlite3_bind_int64(m_stmtInsert, 1, nextId);
    sqlite3_bind_text(m_stmtInsert, 2, msgId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(m_stmtInsert, 3, "direct", -1, SQLITE_STATIC);
    sqlite3_bind_text(m_stmtInsert, 4, sender.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(m_stmtInsert, 5, recipient.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(m_stmtInsert, 6, pair.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_null(m_stmtInsert, 7); // room_name
    sqlite3_bind_int64(m_stmtInsert, 8, 0); // room_instance_id
    sqlite3_bind_text(m_stmtInsert, 9, content.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(m_stmtInsert, 10, timestamp);

    int rc = sqlite3_step(m_stmtInsert);
    if (rc != SQLITE_DONE) {
        outErr = sqlite3_errmsg(m_db);
        sqlite3_reset(m_stmtInsert);
        sqlite3_clear_bindings(m_stmtInsert);
        return false;
    }

    sqlite3_reset(m_stmtInsert);
    sqlite3_clear_bindings(m_stmtInsert);

    outMsgId = msgId;
    return true;
}

bool MessageStore::storeRoomMessage(const std::string& sender,
                                   const std::string& roomName,
                                   int64_t roomInstanceId,
                                   const std::string& content,
                                   int64_t timestamp,
                                   std::string& outMsgId,
                                   std::string& outErr) {
    if (m_simulateFailure.load() || std::getenv("CHAT_SIMULATE_STORAGE_FAILURE") != nullptr) {
        outErr = "Simulated storage failure";
        return false;
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_db || !m_stmtInsert) {
        outErr = "Database not initialized";
        return false;
    }

    int64_t nextId = ++m_lastId;
    std::string msgId = "msg-" + std::to_string(nextId);

    sqlite3_reset(m_stmtInsert);
    sqlite3_clear_bindings(m_stmtInsert);

    sqlite3_bind_int64(m_stmtInsert, 1, nextId);
    sqlite3_bind_text(m_stmtInsert, 2, msgId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(m_stmtInsert, 3, "room", -1, SQLITE_STATIC);
    sqlite3_bind_text(m_stmtInsert, 4, sender.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_null(m_stmtInsert, 5); // recipient
    sqlite3_bind_null(m_stmtInsert, 6); // direct_pair
    sqlite3_bind_text(m_stmtInsert, 7, roomName.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(m_stmtInsert, 8, roomInstanceId);
    sqlite3_bind_text(m_stmtInsert, 9, content.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(m_stmtInsert, 10, timestamp);

    int rc = sqlite3_step(m_stmtInsert);
    if (rc != SQLITE_DONE) {
        outErr = sqlite3_errmsg(m_db);
        sqlite3_reset(m_stmtInsert);
        sqlite3_clear_bindings(m_stmtInsert);
        return false;
    }

    sqlite3_reset(m_stmtInsert);
    sqlite3_clear_bindings(m_stmtInsert);

    outMsgId = msgId;
    return true;
}

bool MessageStore::getPublicHistory(int limit,
                                   std::string_view beforeId,
                                   std::string_view afterId,
                                   std::vector<protocol::HistoryItem>& outItems,
                                   bool& outHasMore,
                                   std::string& outErr) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_db) {
        outErr = "Database not initialized";
        return false;
    }

    int pageLimit = (limit > 0) ? limit : protocol::DEFAULT_HISTORY_LIMIT;
    if (pageLimit > protocol::MAX_HISTORY_LIMIT) pageLimit = protocol::MAX_HISTORY_LIMIT;
    int fetchLimit = pageLimit + 1;

    int64_t bId = parseCursorId(beforeId);
    int64_t aId = parseCursorId(afterId);

    std::ostringstream sql;
    sql << "SELECT msg_id, conv_type, sender, content, created_at, id FROM messages WHERE conv_type = 'public' ";
    bool isDesc = true;
    if (bId > 0) {
        sql << "AND id < ? ORDER BY id DESC LIMIT ?;";
        isDesc = true;
    } else if (aId > 0) {
        sql << "AND id > ? ORDER BY id ASC LIMIT ?;";
        isDesc = false;
    } else {
        sql << "ORDER BY id DESC LIMIT ?;";
        isDesc = true;
    }

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(m_db, sql.str().c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        outErr = sqlite3_errmsg(m_db);
        return false;
    }

    int bindIdx = 1;
    if (bId > 0) {
        sqlite3_bind_int64(stmt, bindIdx++, bId);
    } else if (aId > 0) {
        sqlite3_bind_int64(stmt, bindIdx++, aId);
    }
    sqlite3_bind_int(stmt, bindIdx++, fetchLimit);

    outItems.clear();
    outHasMore = false;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        protocol::HistoryItem item;
        item.msgId = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        item.type = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        item.from = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        item.content = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        item.timestamp = sqlite3_column_int64(stmt, 4);

        outItems.push_back(std::move(item));
    }
    sqlite3_finalize(stmt);

    if (static_cast<int>(outItems.size()) > pageLimit) {
        outHasMore = true;
        outItems.pop_back();
    }

    if (isDesc) {
        std::reverse(outItems.begin(), outItems.end());
    }

    return true;
}

bool MessageStore::getDirectHistory(const std::string& user1,
                                   const std::string& user2,
                                   int limit,
                                   std::string_view beforeId,
                                   std::string_view afterId,
                                   std::vector<protocol::HistoryItem>& outItems,
                                   bool& outHasMore,
                                   std::string& outErr) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_db) {
        outErr = "Database not initialized";
        return false;
    }

    int pageLimit = (limit > 0) ? limit : protocol::DEFAULT_HISTORY_LIMIT;
    if (pageLimit > protocol::MAX_HISTORY_LIMIT) pageLimit = protocol::MAX_HISTORY_LIMIT;
    int fetchLimit = pageLimit + 1;

    int64_t bId = parseCursorId(beforeId);
    int64_t aId = parseCursorId(afterId);
    std::string pair = makeDirectPair(user1, user2);

    std::ostringstream sql;
    sql << "SELECT msg_id, conv_type, sender, recipient, content, created_at, id FROM messages WHERE conv_type = 'direct' AND direct_pair = ? ";
    bool isDesc = true;
    if (bId > 0) {
        sql << "AND id < ? ORDER BY id DESC LIMIT ?;";
        isDesc = true;
    } else if (aId > 0) {
        sql << "AND id > ? ORDER BY id ASC LIMIT ?;";
        isDesc = false;
    } else {
        sql << "ORDER BY id DESC LIMIT ?;";
        isDesc = true;
    }

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(m_db, sql.str().c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        outErr = sqlite3_errmsg(m_db);
        return false;
    }

    int bindIdx = 1;
    sqlite3_bind_text(stmt, bindIdx++, pair.c_str(), -1, SQLITE_TRANSIENT);
    if (bId > 0) {
        sqlite3_bind_int64(stmt, bindIdx++, bId);
    } else if (aId > 0) {
        sqlite3_bind_int64(stmt, bindIdx++, aId);
    }
    sqlite3_bind_int(stmt, bindIdx++, fetchLimit);

    outItems.clear();
    outHasMore = false;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        protocol::HistoryItem item;
        item.msgId = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        item.type = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        item.from = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        const unsigned char* recText = sqlite3_column_text(stmt, 3);
        if (recText) item.to = reinterpret_cast<const char*>(recText);
        item.content = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        item.timestamp = sqlite3_column_int64(stmt, 5);

        outItems.push_back(std::move(item));
    }
    sqlite3_finalize(stmt);

    if (static_cast<int>(outItems.size()) > pageLimit) {
        outHasMore = true;
        outItems.pop_back();
    }

    if (isDesc) {
        std::reverse(outItems.begin(), outItems.end());
    }

    return true;
}

bool MessageStore::getRoomHistory(int64_t roomInstanceId,
                                 int limit,
                                 std::string_view beforeId,
                                 std::string_view afterId,
                                 std::vector<protocol::HistoryItem>& outItems,
                                 bool& outHasMore,
                                 std::string& outErr) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_db) {
        outErr = "Database not initialized";
        return false;
    }

    int pageLimit = (limit > 0) ? limit : protocol::DEFAULT_HISTORY_LIMIT;
    if (pageLimit > protocol::MAX_HISTORY_LIMIT) pageLimit = protocol::MAX_HISTORY_LIMIT;
    int fetchLimit = pageLimit + 1;

    int64_t bId = parseCursorId(beforeId);
    int64_t aId = parseCursorId(afterId);

    std::ostringstream sql;
    sql << "SELECT msg_id, conv_type, sender, room_name, content, created_at, id FROM messages WHERE conv_type = 'room' AND room_instance_id = ? ";
    bool isDesc = true;
    if (bId > 0) {
        sql << "AND id < ? ORDER BY id DESC LIMIT ?;";
        isDesc = true;
    } else if (aId > 0) {
        sql << "AND id > ? ORDER BY id ASC LIMIT ?;";
        isDesc = false;
    } else {
        sql << "ORDER BY id DESC LIMIT ?;";
        isDesc = true;
    }

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(m_db, sql.str().c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        outErr = sqlite3_errmsg(m_db);
        return false;
    }

    int bindIdx = 1;
    sqlite3_bind_int64(stmt, bindIdx++, roomInstanceId);
    if (bId > 0) {
        sqlite3_bind_int64(stmt, bindIdx++, bId);
    } else if (aId > 0) {
        sqlite3_bind_int64(stmt, bindIdx++, aId);
    }
    sqlite3_bind_int(stmt, bindIdx++, fetchLimit);

    outItems.clear();
    outHasMore = false;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        protocol::HistoryItem item;
        item.msgId = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        item.type = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        item.from = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        const unsigned char* rName = sqlite3_column_text(stmt, 3);
        if (rName) item.room = reinterpret_cast<const char*>(rName);
        item.content = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        item.timestamp = sqlite3_column_int64(stmt, 5);

        outItems.push_back(std::move(item));
    }
    sqlite3_finalize(stmt);

    if (static_cast<int>(outItems.size()) > pageLimit) {
        outHasMore = true;
        outItems.pop_back();
    }

    if (isDesc) {
        std::reverse(outItems.begin(), outItems.end());
    }

    return true;
}
