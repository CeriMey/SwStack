#pragma once

inline SwDbStatus SwTableDb::prepareSchema(const SwTableSchema& schema, SwTableDbPreparedSchema* preparedOut) {
    if (!preparedOut) return SwDbStatus(SwDbStatus::InvalidArgument, "Missing prepared schema output");
    const auto status = validateSchema_(schema);
    if (!status.ok()) return status;
    auto prepared = std::make_shared<PreparedSchema_>();
    prepared->schema = schema;
    prepared->rowPrefix = rowPrefix_(schema.tableId);
    using Type = SwTableDbPreparedSchema::Type;
    for (const auto& column : schema.columns) {
        const auto type = swTableDbDetail::normalizedType_(column.type);
        const auto kind = type == "integer" ? Type::Integer : type == "number" ? Type::Number :
                          type == "boolean" ? Type::Boolean : type == "json" ? Type::Json : Type::String;
        prepared->columns.emplace(column.columnId,
            SwTableDbPreparedSchema::Column{kind, column.required || !column.nullable, column.nullable});
    }
    for (const auto& index : schema.indexes) {
        const auto* column = findColumn_(schema, index.columnId);
        prepared->indexes.push_back({index.columnId, swTableDbDetail::normalizedType_(column->type),
                                     swTableDbDetail::indexNameForField_(schema, index.columnId)});
    }
    preparedOut->data_ = std::move(prepared);
    return SwDbStatus::success();
}

inline SwDbStatus SwTableDb::validateExactRow_(const PreparedSchema_& prepared, const SwJsonObject& input) {
    if (!swTableDbDetail::finiteJson_(input))
        return SwDbStatus(SwDbStatus::InvalidArgument, "Exact rows require finite JSON with at most 64 nesting levels");
    using Type = SwTableDbPreparedSchema::Type;
    for (const auto& field : input.dataRef()) {
        const auto found = prepared.columns.find(field.first);
        bool match = false;
        if (found != prepared.columns.end()) {
            const auto& column = found->second;
            const auto& value = field.second;
            if (value.isNull()) match = column.nullable;
            else switch (column.type) {
                case Type::String: match = value.isString(); break;
                case Type::Integer: {
                    const double lower = static_cast<double>(std::numeric_limits<long long>::min());
                    match = value.type() == SwJsonValue::Type::Integer ||
                        (value.isDouble() && value.isInt() && value.toDouble() >= lower && value.toDouble() < -lower);
                    break;
                }
                case Type::Number: match = value.isDouble(); break;
                case Type::Boolean: match = value.isBool(); break;
                case Type::Json: match = true; break;
            }
        }
        if (!match) return SwDbStatus(SwDbStatus::InvalidArgument, "Unknown column or exact row type mismatch");
    }
    for (const auto& field : prepared.columns)
        if (field.second.required && !input.contains(field.first))
            return SwDbStatus(SwDbStatus::InvalidArgument, "Missing required column");
    const auto& key = input[prepared.schema.primaryKey];
    if (!key.isString() || key.toString().isEmpty() || key.toString().size() > 128)
        return SwDbStatus(SwDbStatus::InvalidArgument, "Exact row key must be a string of 1 to 128 bytes");
    return SwDbStatus::success();
}

inline SwMap<SwString, SwList<SwByteArray>> SwTableDb::secondaryKeysForRow_(const PreparedSchema_& prepared,
                                                                         const SwJsonObject& row) {
    SwMap<SwString, SwList<SwByteArray>> secondary;
    const auto rowSuffix = swTableDbDetail::rowSuffix_(row.value(prepared.schema.primaryKey).toString());
    for (const auto& index : prepared.indexes) {
        bool ok = false;
        const auto encoded = encodeIndexValue_(index.type, row.value(index.column), &ok);
        if (!ok) continue;
        SwList<SwByteArray> keys; keys.append(encoded + SwByteArray("\x1f") + rowSuffix);
        secondary.insert(index.name, keys);
    }
    return secondary;
}

inline SwDbStatus SwTableDb::getRow(const SwTableDbPreparedSchema& prepared, const SwString& rowId,
                                     SwJsonObject* rowOut) const {
    if (!prepared.data_) return SwDbStatus(SwDbStatus::InvalidArgument, "Invalid prepared schema");
    return getRow_(prepared.data_->schema, rowId, rowOut, prepared.data_.get());
}
inline SwDbStatus SwTableDb::getRowRecord(const SwTableDbPreparedSchema& prepared, const SwString& rowId,
                                           SwDbJsonRecord* out) const {
    if (!prepared.data_) return SwDbStatus(SwDbStatus::InvalidArgument, "Invalid prepared schema");
    return getRowRecord_(prepared.data_->schema, rowId, out, prepared.data_.get());
}
inline SwDbStatus SwTableDb::writeRowsOwned_(const SwTableDbPreparedSchema& prepared, SwJsonArray&& rows,
                                            bool replace, const SwList<SwString>& eraseKeys) {
    if (!prepared.data_) return SwDbStatus(SwDbStatus::InvalidArgument, "Invalid prepared schema");
    return writeRows_(prepared.data_->schema, rows, replace, eraseKeys, prepared.data_.get(), &rows);
}
inline SwDbStatus SwTableDb::readRows(const SwTableDbPreparedSchema& prepared, SwJsonArray* rowsOut) const {
    if (!prepared.data_) return SwDbStatus(SwDbStatus::InvalidArgument, "Invalid prepared schema");
    return readRows_(prepared.data_->schema, rowsOut, prepared.data_.get());
}
inline SwDbStatus SwTableDb::queryRows(const SwTableDbPreparedSchema& prepared, const SwTableQuery& query,
                                       SwTableQueryResult* outResult) const {
    if (!prepared.data_) return SwDbStatus(SwDbStatus::InvalidArgument, "Invalid prepared schema");
    return queryRows_(prepared.data_->schema, query, outResult, prepared.data_.get());
}
inline SwDbStatus SwTableDb::replaceRows(const SwTableDbPreparedSchema& prepared, const SwJsonArray& rows) {
    if (!prepared.data_) return SwDbStatus(SwDbStatus::InvalidArgument, "Invalid prepared schema");
    return writeRows_(prepared.data_->schema, rows, true, {}, prepared.data_.get());
}
inline SwDbStatus SwTableDb::upsertRows(const SwTableDbPreparedSchema& prepared, const SwJsonArray& rows) {
    if (!prepared.data_) return SwDbStatus(SwDbStatus::InvalidArgument, "Invalid prepared schema");
    return writeRows_(prepared.data_->schema, rows, false, {}, prepared.data_.get());
}
inline SwDbStatus SwTableDb::applyRows(const SwTableDbPreparedSchema& prepared, const SwJsonArray& rows,
                                       const SwList<SwString>& eraseKeys) {
    if (!prepared.data_) return SwDbStatus(SwDbStatus::InvalidArgument, "Invalid prepared schema");
    return writeRows_(prepared.data_->schema, rows, false, eraseKeys, prepared.data_.get());
}
