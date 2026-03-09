#pragma once

/**
 * @file src/core/gui/SwizioNodes/internal/Export.hpp
 * @ingroup core_swizio_nodes
 * @brief Declares the public interface exposed by Export in the CoreSw node-editor layer.
 *
 * This header belongs to the CoreSw node-editor layer. It contains the graph, geometry, style,
 * and scene infrastructure used by the embedded node editor.
 *
 * Within that layer, this file focuses on the export interface. The declarations exposed here
 * define the stable surface that adjacent code can rely on while the implementation remains free
 * to evolve behind the header.
 *
 * This header mainly contributes module-level utilities, helper declarations, or namespaced types
 * that are consumed by the surrounding subsystem.
 *
 * The declarations in this header are intended to make the subsystem boundary explicit: callers
 * interact with stable types and functions, while implementation details remain confined to
 * source files and private helpers.
 *
 * Most declarations here are extension points or internal contracts that coordinate graph
 * editing, visualization, and interaction.
 *
 */


// Symbol visibility helper kept for parity with the original layout.
// This repo mostly builds static targets, so we keep it empty by default.
#ifndef SWIZIO_NODES_PUBLIC
#define SWIZIO_NODES_PUBLIC
#endif
