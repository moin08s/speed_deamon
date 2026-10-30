# Track D: Traverse

The task, the input files and the exact output format are in the [spec](https://github.com/hackathon-raptors/speed-demon-2026/blob/main/specs/d-traverse.md).

## Approach

1. **Dual Adjacency Graph Representation**: Builds forward and backward adjacency structures directly during line-by-line ASCII ingestion.
2. **Bidirectional Dijkstra Search**: Simultaneously expands Dijkstra searches from the source node ($s$) and target node ($t$), alternating the smaller heap.
3. **Rigorous Early Termination**: Halts immediately when $\min(\text{heap}_f) + \min(\text{heap}_b) \ge \text{best\_distance}$, cutting search space exponentially compared to standard unidirectional search on road networks.
4. **Boundary & Edge-Case Safety**: Handles disconnected components (returning `-1`), self-loops, parallel edges, weight 0 edges, and values $> 2^{32}-1$.
5. **Zero Third-Party Dependencies**: Standard library only ($D = 1$).

## Dependencies

None. Standard library only.

## With another 24 hours

1. Flat array / compact CSR (Compressed Sparse Row) representation to maximize CPU cache locality.
2. Multiprocessing pool to distribute the 2,000 independent shortest-path queries across all 12 hardware threads.
