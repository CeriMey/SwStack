#include "StoreState.hpp"
#include <stdexcept>

SwJsonObject SwRealtimeDb::State::writeView(Table& view, const SwJsonObject& request, bool erase,
                                          MutationOutcome* outcome, const SwJsonObject* batchEnvelope) {
    if (view.encodeScript.isEmpty()) throw std::runtime_error("views are read-only without encode");
    if (erase || request.contains("definition") || request["merge"].toBool() || request.contains("increments"))
        throw std::runtime_error("view writes require complete input rows; encode controls the target rows");
    const auto& credentials = batchEnvelope ? *batchEnvelope : request;
    const auto actor = session(credentials).actor;
    if (!ownerAlive(view)) throw std::runtime_error("view owner is unavailable or must register again");
    if (actor != view.owner && !view.writers.count(actor)) throw std::runtime_error("actor is not an authorized view writer");
    // The target's own writer policy is checked with the original credentials.
    // A view is a format adapter, never an elevation to its owner's identity.
    const auto target = tables.find(view.writeTarget);
    if (target == tables.end()) throw std::runtime_error("missing write target: " + view.writeTarget);
    if (target->second.view) throw std::runtime_error("write_target must be a stored table");
    if (!ownerAlive(target->second) || !target->second.valid)
        throw std::runtime_error("write target owner must publish a fresh snapshot");
    if (actor != target->second.owner && !target->second.writers.count(actor))
        throw std::runtime_error("actor is not an authorized target writer");
    if (!request["rows"].isArray()) throw std::runtime_error("rows must be an array");
    SwJsonArray keys;
    auto input = validateRows(view, request["rows"].toArray(), &keys);
    // Preserve caller order, including where the destination uses retention.
    SwJsonArray rows;
    for (const auto& key : keys) rows.append(std::move(input.at(key.toString())));
    SwJsonObject arguments; arguments["rows"] = std::move(rows);
    // Stable target snapshot from this same serialized mutation, for reversible
    // formatting such as updating one packed bit without losing the other bits.
    arguments["current"] = swRealtimeDbDetail::rowArray(readRows(target->second));
    ++view.encodeEvaluations;
    auto encoded = evaluateView(view.encodeScript, view.encodeProgram, arguments);
    SwJsonObject forwarded;
    forwarded["op"] = "write"; forwarded["table"] = view.writeTarget;
    forwarded["rows"] = std::move(encoded);
    if (view.encodeMerge) forwarded["merge"] = true;
    if (!batchEnvelope) forwarded["session"] = credentials["session"];
    forwarded["include_rows"] = false;
    auto result = write(forwarded, false, outcome, batchEnvelope);
    // A receipt identifies both the view and the authoritative table. Reading
    // the view remains lazy; committing a command does not run decode for a reply.
    result["view"] = view.name;
    return result;
}
