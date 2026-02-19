#include "engines/lua/lua_csv.h"
#include "csv/csv_parser.h"
#include "csv/csv_escape.h"
#include "utils/log_config.h"
#include <LittleFS.h>
#include <sstream>
#include <algorithm>

namespace LuaCSV {

static const char* TAG = "LuaCSV";

// ============================================================================
// CSV Object — holds parsed data in memory
// ============================================================================

struct CSVObject {
    std::vector<std::string> headers;
    std::vector<std::vector<std::string>> rows;
    size_t loadedRowCount = 0;
    std::string filename;

    std::vector<std::vector<std::string>> getLastRows(int count) const {
        if (count < 0 || (size_t)count >= rows.size()) return rows;
        auto start = rows.end() - count;
        return std::vector<std::vector<std::string>>(start, rows.end());
    }

    std::vector<std::vector<std::string>> getNewRows() const {
        if (loadedRowCount >= rows.size()) return {};
        auto start = rows.begin() + loadedRowCount;
        return std::vector<std::vector<std::string>>(start, rows.end());
    }
};

// ============================================================================
// Helpers
// ============================================================================

static CSVObject* checkCSV(lua_State* L, int index) {
    void* ud = luaL_checkudata(L, index, "CSV");
    luaL_argcheck(L, ud != nullptr, index, "'CSV' expected");
    return static_cast<CSVObject*>(ud);
}

static bool parseCSVText(const std::string& text, CSVObject* csv, std::string& error) {
    std::istringstream stream(text);
    std::string line;

    if (!std::getline(stream, line)) {
        error = "Empty CSV file";
        return false;
    }

    csv->headers = CSV::parseLine(line);
    if (csv->headers.empty()) {
        error = "Invalid CSV format: no headers found";
        return false;
    }

    int lineNum = 1;
    while (std::getline(stream, line)) {
        lineNum++;
        if (line.empty()) continue;

        auto fields = CSV::parseLine(line);
        if (fields.size() != csv->headers.size()) {
            LOG_W(Log::LUA, "CSV line %d: field count mismatch (expected %d, got %d)",
                  lineNum, (int)csv->headers.size(), (int)fields.size());
        }
        csv->rows.push_back(fields);
    }

    csv->loadedRowCount = csv->rows.size();
    return true;
}

static std::string serializeCSV(const CSVObject* csv, bool onlyNew) {
    std::ostringstream result;

    for (size_t i = 0; i < csv->headers.size(); i++) {
        if (i > 0) result << CSV::DELIMITER;
        result << csv->headers[i];
    }
    result << "\n";

    auto rowsToWrite = onlyNew ? csv->getNewRows() : csv->rows;
    for (const auto& row : rowsToWrite) {
        for (size_t i = 0; i < row.size(); i++) {
            if (i > 0) result << CSV::DELIMITER;
            result << CSV::escape(row[i]);
        }
        result << "\n";
    }

    return result.str();
}

// ============================================================================
// CSV.loadText(text)
// ============================================================================

static int csv_loadText(lua_State* L) {
    size_t len;
    const char* text = luaL_checklstring(L, 1, &len);

    CSVObject* csv = static_cast<CSVObject*>(lua_newuserdata(L, sizeof(CSVObject)));
    new (csv) CSVObject();

    std::string error;
    if (!parseCSVText(std::string(text, len), csv, error)) {
        csv->~CSVObject();
        lua_pushnil(L);
        lua_pushstring(L, error.c_str());
        return 2;
    }

    luaL_getmetatable(L, "CSV");
    lua_setmetatable(L, -2);
    return 1;
}

// ============================================================================
// CSV.load(filename)
// ============================================================================

static int csv_load(lua_State* L) {
    const char* filename = luaL_checkstring(L, 1);

    File file = LittleFS.open(filename, "r");
    if (!file) {
        lua_pushnil(L);
        lua_pushfstring(L, "File not found: %s", filename);
        return 2;
    }

    size_t sz = file.size();
    std::string text(sz, '\0');
    file.readBytes(&text[0], sz);
    file.close();

    CSVObject* csv = static_cast<CSVObject*>(lua_newuserdata(L, sizeof(CSVObject)));
    new (csv) CSVObject();
    csv->filename = filename;

    std::string error;
    if (!parseCSVText(text, csv, error)) {
        csv->~CSVObject();
        lua_pushnil(L);
        lua_pushstring(L, error.c_str());
        return 2;
    }

    luaL_getmetatable(L, "CSV");
    lua_setmetatable(L, -2);
    return 1;
}

// ============================================================================
// csv:records(count?)
// ============================================================================

static int csv_records(lua_State* L) {
    CSVObject* csv = checkCSV(L, 1);
    int count = luaL_optinteger(L, 2, -1);
    auto rows = csv->getLastRows(count);

    lua_createtable(L, rows.size(), 0);
    for (size_t i = 0; i < rows.size(); i++) {
        const auto& row = rows[i];
        lua_createtable(L, 0, csv->headers.size());
        for (size_t j = 0; j < csv->headers.size() && j < row.size(); j++) {
            lua_pushstring(L, csv->headers[j].c_str());
            lua_pushstring(L, row[j].c_str());
            lua_settable(L, -3);
        }
        lua_rawseti(L, -2, i + 1);
    }
    return 1;
}

// ============================================================================
// csv:rows(count?)
// ============================================================================

static int csv_rows(lua_State* L) {
    CSVObject* csv = checkCSV(L, 1);
    int count = luaL_optinteger(L, 2, -1);
    auto rows = csv->getLastRows(count);

    lua_createtable(L, rows.size(), 0);
    for (size_t i = 0; i < rows.size(); i++) {
        const auto& row = rows[i];
        lua_createtable(L, row.size(), 0);
        for (size_t j = 0; j < row.size(); j++) {
            lua_pushstring(L, row[j].c_str());
            lua_rawseti(L, -2, j + 1);
        }
        lua_rawseti(L, -2, i + 1);
    }
    return 1;
}

// ============================================================================
// csv:rawText()
// ============================================================================

static int csv_rawText(lua_State* L) {
    CSVObject* csv = checkCSV(L, 1);
    std::string text = serializeCSV(csv, false);
    lua_pushlstring(L, text.c_str(), text.length());
    return 1;
}

// ============================================================================
// csv:add(record)
// ============================================================================

static int csv_add(lua_State* L) {
    CSVObject* csv = checkCSV(L, 1);
    luaL_checktype(L, 2, LUA_TTABLE);

    std::vector<std::string> row;
    row.resize(csv->headers.size());

    // Detect dict vs array
    lua_pushnil(L);
    bool isDict = false;
    if (lua_next(L, 2) != 0) {
        isDict = (lua_type(L, -2) == LUA_TSTRING);
        lua_pop(L, 2);
    }

    if (isDict) {
        for (size_t i = 0; i < csv->headers.size(); i++) {
            lua_pushstring(L, csv->headers[i].c_str());
            lua_gettable(L, 2);
            if (lua_isstring(L, -1)) {
                row[i] = lua_tostring(L, -1);
            } else if (lua_isnumber(L, -1)) {
                row[i] = lua_tostring(L, -1);
            }
            lua_pop(L, 1);
        }
    } else {
        size_t arraySize = lua_rawlen(L, 2);
        if (arraySize != csv->headers.size()) {
            return luaL_error(L, "Field count mismatch: expected %d, got %d",
                            (int)csv->headers.size(), (int)arraySize);
        }
        for (size_t i = 0; i < arraySize; i++) {
            lua_rawgeti(L, 2, i + 1);
            if (lua_isstring(L, -1) || lua_isnumber(L, -1)) {
                row[i] = lua_tostring(L, -1);
            }
            lua_pop(L, 1);
        }
    }

    csv->rows.push_back(row);
    return 0;
}

// ============================================================================
// csv:save(onlyNew?)
// ============================================================================

static int csv_save(lua_State* L) {
    CSVObject* csv = checkCSV(L, 1);
    bool onlyNew = lua_toboolean(L, 2);

    if (csv->filename.empty()) {
        lua_pushboolean(L, 0);
        lua_pushstring(L, "No filename (use CSV.load)");
        return 2;
    }

    bool ok = false;

    if (onlyNew && csv->loadedRowCount < csv->rows.size()) {
        bool fileExists = LittleFS.exists(csv->filename.c_str());

        if (fileExists) {
            // Append only new rows
            std::string data;
            auto newRows = csv->getNewRows();
            for (const auto& row : newRows) {
                for (size_t i = 0; i < row.size(); i++) {
                    if (i > 0) data += CSV::DELIMITER;
                    data += CSV::escape(row[i]);
                }
                data += "\n";
            }
            File f = LittleFS.open(csv->filename.c_str(), "a");
            if (f) {
                f.write((const uint8_t*)data.c_str(), data.size());
                f.close();
                ok = true;
            }
        } else {
            // New file — write everything
            std::string data = serializeCSV(csv, false);
            File f = LittleFS.open(csv->filename.c_str(), "w");
            if (f) {
                f.write((const uint8_t*)data.c_str(), data.size());
                f.close();
                ok = true;
            }
        }
    } else {
        // Full rewrite
        std::string data = serializeCSV(csv, false);
        File f = LittleFS.open(csv->filename.c_str(), "w");
        if (f) {
            f.write((const uint8_t*)data.c_str(), data.size());
            f.close();
            ok = true;
        }
    }

    if (!ok) {
        lua_pushboolean(L, 0);
        lua_pushfstring(L, "Cannot write: %s", csv->filename.c_str());
        return 2;
    }

    csv->loadedRowCount = csv->rows.size();
    lua_pushboolean(L, 1);
    return 1;
}

// ============================================================================
// __gc
// ============================================================================

static int csv_gc(lua_State* L) {
    CSVObject* csv = checkCSV(L, 1);
    csv->~CSVObject();
    return 0;
}

// ============================================================================
// Registration
// ============================================================================

void registerAll(lua_State* L) {
    // Metatable for CSV objects
    luaL_newmetatable(L, "CSV");

    lua_pushstring(L, "__index");
    lua_newtable(L);

    lua_pushstring(L, "records");  lua_pushcfunction(L, csv_records);  lua_settable(L, -3);
    lua_pushstring(L, "rows");     lua_pushcfunction(L, csv_rows);     lua_settable(L, -3);
    lua_pushstring(L, "rawText");  lua_pushcfunction(L, csv_rawText);  lua_settable(L, -3);
    lua_pushstring(L, "add");      lua_pushcfunction(L, csv_add);      lua_settable(L, -3);
    lua_pushstring(L, "save");     lua_pushcfunction(L, csv_save);     lua_settable(L, -3);

    lua_settable(L, -3);  // metatable.__index = methods

    lua_pushstring(L, "__gc");
    lua_pushcfunction(L, csv_gc);
    lua_settable(L, -3);

    lua_pop(L, 1);  // pop metatable

    // Global CSV table
    lua_newtable(L);
    lua_pushstring(L, "load");     lua_pushcfunction(L, csv_load);     lua_settable(L, -3);
    lua_pushstring(L, "loadText"); lua_pushcfunction(L, csv_loadText); lua_settable(L, -3);
    lua_setglobal(L, "CSV");

    LOG_I(Log::LUA, "Registered: CSV.load/loadText + methods");
}

} // namespace LuaCSV
