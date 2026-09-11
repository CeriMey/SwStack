#pragma once
#include "SwTableDbTypes.h"
#include "SwByteArray.h"
#include <map>
#include <memory>
#include <vector>

// A detached, validated descriptor. Copies share immutable metadata only;
// neither rows nor a database snapshot are retained. The source schema may be
// mutated/destroyed without changing this handle or invalidating its lookups.
class SwTableDbPreparedSchema {
public:
    bool isValid() const { return static_cast<bool>(data_); }
private:
    friend class SwTableDb;
    enum class Type { String, Integer, Number, Boolean, Json };
    struct Column { Type type; bool required; bool nullable; };
    struct Index { SwString column, type, name; };
    struct Data {
        SwTableSchema schema;
        SwByteArray rowPrefix;
        std::map<SwString, Column> columns;
        std::vector<Index> indexes;
    };
    std::shared_ptr<const Data> data_;
};
