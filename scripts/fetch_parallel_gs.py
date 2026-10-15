"""Fetch and verify the pinned paraLLEl-GS source tree (TASK-012 step 1, slice 162).

The pin lives in docs/inputs/parallel-gs-pin.json. Only the paraLLEl-GS repository,
its Granite submodule and the two Granite submodules needed by the library build
(Vulkan-Headers and volk) are initialised; every other submodule stays empty.

Usage:
  python scripts/fetch_parallel_gs.py --dest C:/Antigravity/gt4-staticrecomp/private/references/parallel-gs
  python scripts/fetch_parallel_gs.py --dest <path> --verify-only

An existing destination is reused only when it verifies against the pin; otherwise
the script refuses and leaves it untouched. A fresh clone that fails part way is
removed again, because this script created it.
"""
from __future__ import annotations

import argparse
import dataclasses
import hashlib
import json
import re
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Callable, Sequence

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_MANIFEST = ROOT / 'docs/inputs/parallel-gs-pin.json'
SHA1_PATTERN = re.compile(r'^[0-9a-f]{40}$')
SHA256_PATTERN = re.compile(r'^[0-9a-f]{64}$')
GITLINK_PATTERN = re.compile(r'^160000 commit ([0-9a-f]{40})\t(.+)$')

GitRunner = Callable[[Sequence[str], 'Path | None'], str]


class FetchError(RuntimeError):
    """A pin, checkout or verification failure with a message for the owner."""


@dataclasses.dataclass(frozen=True)
class Submodule:
    parent: str  # '' for the top-level paraLLEl-GS repository, else a submodule location
    path: str  # relative to the parent repository
    url: str
    commit: str
    license: str

    @property
    def location(self) -> str:
        return f'{self.parent}/{self.path}' if self.parent else self.path


@dataclasses.dataclass(frozen=True)
class Pin:
    repository: str
    commit: str
    license: str
    shader_header_path: str
    shader_header_sha256: str
    submodules: tuple[Submodule, ...]


def parse_pin(data: dict, *, allow_local_urls: bool = False) -> Pin:
    """Validate the manifest mapping; tests may allow local repository URLs."""
    repository = data.get('repository', '')
    if not (repository.startswith('https://') or (allow_local_urls and repository)):
        raise FetchError(f'repository must be an https URL: {repository!r}')
    commit = data.get('commit', '')
    if not SHA1_PATTERN.match(commit):
        raise FetchError(f'paraLLEl-GS commit must be a full 40-hex SHA-1: {commit!r}')
    shader = data.get('shader_header', {})
    shader_path = shader.get('path', '')
    if not shader_path or shader_path.startswith('/') or '..' in shader_path.split('/'):
        raise FetchError(f'shader header path must stay inside the tree: {shader_path!r}')
    if not SHA256_PATTERN.match(shader.get('sha256', '')):
        raise FetchError('shader header sha256 must be 64 lowercase hex digits')
    submodules: list[Submodule] = []
    seen: set[str] = set()
    for entry in data.get('submodules', []):
        submodule = Submodule(entry.get('parent', ''), entry.get('path', ''), entry.get('url', ''),
                              entry.get('commit', ''), entry.get('license', ''))
        if not submodule.path or submodule.path.startswith('/') or '..' in submodule.path.split('/'):
            raise FetchError(f'submodule path must stay inside its parent: {submodule.path!r}')
        if submodule.parent and submodule.parent not in seen:
            raise FetchError(f'submodule parent must be listed earlier: {submodule.parent!r}')
        if submodule.location in seen:
            raise FetchError(f'duplicate submodule: {submodule.location!r}')
        if not SHA1_PATTERN.match(submodule.commit):
            raise FetchError(f'submodule commit must be a full SHA-1: {submodule.location!r}')
        if not (submodule.url.startswith('https://') or (allow_local_urls and submodule.url)):
            raise FetchError(f'submodule URL must be https: {submodule.location!r}')
        if not submodule.license:
            raise FetchError(f'submodule license is required: {submodule.location!r}')
        seen.add(submodule.location)
        submodules.append(submodule)
    if not submodules:
        raise FetchError('the pin must list the Granite submodules')
    return Pin(repository, commit, data.get('license', ''), shader_path, shader.get('sha256', ''),
               tuple(submodules))


def load_pin(path: Path, *, allow_local_urls: bool = False) -> Pin:
    try:
        data = json.loads(path.read_text(encoding='utf-8'))
    except (OSError, json.JSONDecodeError) as error:
        raise FetchError(f'cannot read manifest {path}: {error}') from error
    return parse_pin(data, allow_local_urls=allow_local_urls)


def run_git(args: Sequence[str], cwd: Path | None) -> str:
    """Run git and return stripped stdout; any failure raises FetchError.

    core.autocrlf is forced off so checked-out bytes equal the pinned blobs on every
    machine (a Windows autocrlf checkout changes the shader header's sha256).
    """
    command = ['git', '-c', 'core.autocrlf=false', *args]
    completed = subprocess.run(command, cwd=cwd, capture_output=True, text=True,
                               encoding='utf-8', check=False)
    if completed.returncode != 0:
        where = str(cwd) if cwd else '(no directory)'
        raise FetchError(f'git {" ".join(args)} failed in {where}: {completed.stderr.strip()}')
    return completed.stdout.strip()


def verify_checkout(root: Path, pin: Pin, git: GitRunner = run_git) -> list[str]:
    """Return a list of problems; an empty list means the tree matches the pin."""
    problems: list[str] = []
    try:
        head = git(['rev-parse', 'HEAD'], root)
        if head != pin.commit:
            problems.append(f'paraLLEl-GS HEAD is {head}, pinned {pin.commit}')
        if git(['status', '--porcelain', '--untracked-files=all'], root):
            problems.append('paraLLEl-GS checkout has local changes')
        shader = root / pin.shader_header_path
        if not shader.is_file():
            problems.append(f'missing {pin.shader_header_path}')
        elif hashlib.sha256(shader.read_bytes()).hexdigest() != pin.shader_header_sha256:
            problems.append(f'{pin.shader_header_path} differs from the pinned sha256')
        for submodule in pin.submodules:
            problems.extend(_verify_submodule(root, submodule, git))
        for parent in ['', *[s.location for s in pin.submodules]]:
            problems.extend(_verify_initialised(root, parent, pin, git))
    except FetchError as error:
        problems.append(str(error))
    return problems


def _verify_submodule(root: Path, submodule: Submodule, git: GitRunner) -> list[str]:
    location = submodule.location
    checkout = root / location
    parent_dir = root / submodule.parent if submodule.parent else root
    problems: list[str] = []
    if not checkout.is_dir() or not any(checkout.iterdir()):
        return [f'submodule {location} is not initialised']
    if git(['rev-parse', 'HEAD'], checkout) != submodule.commit:
        problems.append(f'submodule {location} is not at the pinned commit {submodule.commit}')
    if git(['status', '--porcelain', '--untracked-files=all'], checkout):
        problems.append(f'submodule {location} has local changes')
    listed = git(['ls-tree', 'HEAD', '--', submodule.path], parent_dir)
    match = GITLINK_PATTERN.match(listed)
    if not match or match.group(1) != submodule.commit:
        problems.append(f'parent of {location} does not record the pinned gitlink')
    return problems


def _verify_initialised(root: Path, parent: str, pin: Pin, git: GitRunner) -> list[str]:
    """Only the listed submodules may be initialised; others must remain empty."""
    parent_dir = root / parent if parent else root
    if not parent_dir.is_dir():
        return []
    expected = {s.path for s in pin.submodules if s.parent == parent}
    lines = git(['submodule', 'status'], parent_dir).splitlines()
    initialised = {line[1:].split()[1] for line in lines if line and line[0] != '-'}
    if initialised != expected:
        where = parent or 'paraLLEl-GS'
        return [f'{where} initialised submodules {sorted(initialised)}, expected {sorted(expected)}']
    return []


def remove_tree(path: Path) -> None:
    """Remove a clone this script created; git pack files are read-only on Windows."""
    def clear_read_only(function, failed_path, _exc_info):
        Path(failed_path).chmod(0o700)
        function(failed_path)

    shutil.rmtree(path, onerror=clear_read_only)


def fetch_pin(pin: Pin, dest: Path, git: GitRunner = run_git) -> str:
    """Clone and check out the pin, or verify an existing checkout. Returns the outcome."""
    if dest.exists():
        if not dest.is_dir():
            raise FetchError(f'{dest} exists and is not a directory')
        if any(dest.iterdir()):
            problems = verify_checkout(dest, pin, git)
            if problems:
                raise FetchError(f'{dest} exists and does not verify; refusing to touch it: '
                                 + '; '.join(problems))
            return 'verified'
        dest.rmdir()
    dest.parent.mkdir(parents=True, exist_ok=True)
    try:
        git(['clone', '--no-recurse-submodules', pin.repository, str(dest)], None)
        git(['checkout', '--detach', pin.commit], dest)
        for submodule in pin.submodules:
            parent_dir = dest / submodule.parent if submodule.parent else dest
            git(['submodule', 'update', '--init', '--', submodule.path], parent_dir)
        problems = verify_checkout(dest, pin, git)
        if problems:
            raise FetchError('fetched tree does not verify: ' + '; '.join(problems))
    except BaseException:
        remove_tree(dest)
        raise
    return 'fetched'


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--manifest', type=Path, default=DEFAULT_MANIFEST)
    parser.add_argument('--dest', type=Path, required=True,
                        help='fresh directory, e.g. the main checkout private/references/parallel-gs')
    parser.add_argument('--verify-only', action='store_true', help='check an existing checkout; never fetch')
    args = parser.parse_args(argv)
    try:
        pin = load_pin(args.manifest)
        if args.verify_only:
            problems = verify_checkout(args.dest, pin)
            if problems:
                print('FAILED: ' + '; '.join(problems), file=sys.stderr)
                return 1
            outcome = 'verified'
        else:
            outcome = fetch_pin(pin, args.dest)
    except FetchError as error:
        print(f'error: {error}', file=sys.stderr)
        return 1
    print(f'{outcome}: {args.dest} at paraLLEl-GS {pin.commit}')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
