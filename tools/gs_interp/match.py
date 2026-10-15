"""Primitive matching between two frames, within the same drawing-context run.

A context run is a maximal run of consecutive primitives with the same key
(kind, TEX0, FRAME, ZBUF of the active context). The k-th run of a key in frame A is paired
with the k-th run of that key in frame B; inside a paired run, primitives are paired by their
position, and only when kind and vertex count agree. Strips and fans share vertices between
primitives; they are matched (and counted) but flagged `substitutable` False.
"""

from __future__ import annotations

from dataclasses import dataclass, field


@dataclass
class MatchResult:
    pairs: dict = field(default_factory=dict)  # index in B -> index in A (substitutable or not)
    count_a: int = 0
    count_b: int = 0
    matched: int = 0  # pairs found, including shared primitives
    substitutable: int = 0  # pairs that the interpolation may rewrite
    shared_matched: int = 0
    unmatched_b_by_kind: dict = field(default_factory=dict)

    @property
    def rate_b(self) -> float:
        return self.matched / self.count_b if self.count_b else 0.0

    @property
    def rate_a(self) -> float:
        return self.matched / self.count_a if self.count_a else 0.0

    @property
    def substitutable_rate_b(self) -> float:
        return self.substitutable / self.count_b if self.count_b else 0.0


def literal_key(primitive) -> tuple:
    """(kind, TEX0, FRAME, ZBUF) of the active context, as the TASK-031 rule states it."""
    return primitive.key


def relaxed_key(primitive) -> tuple:
    """(kind, TEX0): the page (FRAME, ZBUF) is left out.

    The menu alternates two draw pages from frame to frame, so the literal key never matches
    frames of different parity. The synthesis sets the page from the frame of the target's
    parity, so the relaxed key is the one that pairs geometry.
    """
    return (primitive.key[0], primitive.key[1])


def context_runs(primitives: list, key=literal_key) -> dict:
    """key -> list of runs, each run a list of primitive indices, in frame order."""
    runs: dict = {}
    previous_key = None
    for index, primitive in enumerate(primitives):
        current = key(primitive)
        if current != previous_key or not runs.get(current):
            runs.setdefault(current, []).append([])
        runs[current][-1].append(index)
        previous_key = current
    return runs


def match_primitives(prims_a: list, prims_b: list, key=literal_key) -> MatchResult:
    result = MatchResult(count_a=len(prims_a), count_b=len(prims_b))
    runs_a = context_runs(prims_a, key)
    runs_b = context_runs(prims_b, key)
    for key, b_runs in runs_b.items():
        a_runs = runs_a.get(key, [])
        for run_index, b_run in enumerate(b_runs):
            if run_index >= len(a_runs):
                break
            a_run = a_runs[run_index]
            for position, index_b in enumerate(b_run):
                if position >= len(a_run):
                    break
                index_a = a_run[position]
                pa, pb = prims_a[index_a], prims_b[index_b]
                if pa.kind != pb.kind or len(pa.vertices) != len(pb.vertices):
                    continue
                result.pairs[index_b] = index_a
                result.matched += 1
                if pb.shared:
                    result.shared_matched += 1
                else:
                    result.substitutable += 1
    for index_b, primitive in enumerate(prims_b):
        if index_b not in result.pairs:
            name = primitive.name
            result.unmatched_b_by_kind[name] = result.unmatched_b_by_kind.get(name, 0) + 1
    return result
