"""Offline checks for the paraLLEl-GS fetch and verification (no network).

A fake upstream made of local git repositories stands in for paraLLEl-GS, Granite,
Vulkan-Headers, volk and an unused Granite submodule.
"""
import copy
import hashlib
import json
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'scripts'))
import fetch_parallel_gs as fetcher

ALLOW_LOCAL_TRANSPORT = ['-c', 'protocol.file.allow=always']
IDENTITY = ['-c', 'user.name=fixture', '-c', 'user.email=fixture@example.invalid', '-c', 'commit.gpgsign=false']


def local_git(args, cwd=None):
    return fetcher.run_git(ALLOW_LOCAL_TRANSPORT + IDENTITY + list(args), cwd)


def make_repo(path, files, gitlinks=(), gitmodules=''):
    """Create a repository with plain files, gitlinks [(path, commit)] and a .gitmodules text."""
    path.mkdir(parents=True)
    local_git(['init', '-q', '-b', 'main'], path)
    for name, data in files.items():
        target = path / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
    if gitmodules:
        (path / '.gitmodules').write_text(gitmodules, encoding='utf-8')
    local_git(['add', '-A'], path)
    # Gitlinks are staged after `add -A`, which would drop index entries whose worktree path is absent.
    for gitlink_path, commit in gitlinks:
        local_git(['update-index', '--add', '--cacheinfo', f'160000,{commit},{gitlink_path}'], path)
    local_git(['commit', '-q', '-m', 'fixture'], path)
    return local_git(['rev-parse', 'HEAD'], path)


def submodule_entry(name, url):
    return f'[submodule "{name}"]\n\tpath = {name}\n\turl = {url.as_posix()}\n'


class FakeUpstream:
    """Builds the fake paraLLEl-GS tree and returns a manifest that pins it."""

    def __init__(self, root):
        upstream = root / 'upstream'
        self.vulkan_headers = make_repo(upstream / 'vulkan-headers', {'include/vulkan/vulkan.h': b'// fake\n'})
        self.volk = make_repo(upstream / 'volk', {'volk.c': b'/* fake */\n'})
        self.unused = make_repo(upstream / 'unused', {'unused.txt': b'not needed\n'})
        granite_url = upstream / 'Granite'
        vulkan_url = upstream / 'vulkan-headers'
        volk_url = upstream / 'volk'
        unused_url = upstream / 'unused'
        granite_modules = ''.join([
            '[submodule "third_party/khronos/vulkan-headers"]\n\tpath = third_party/khronos/vulkan-headers\n'
            f'\turl = {vulkan_url.as_posix()}\n',
            '[submodule "third_party/volk"]\n\tpath = third_party/volk\n'
            f'\turl = {volk_url.as_posix()}\n',
            '[submodule "third_party/unused"]\n\tpath = third_party/unused\n'
            f'\turl = {unused_url.as_posix()}\n',
        ])
        self.granite = make_repo(
            granite_url, {'README.md': b'fake granite\n'},
            gitlinks=[('third_party/khronos/vulkan-headers', self.vulkan_headers),
                      ('third_party/volk', self.volk), ('third_party/unused', self.unused)],
            gitmodules=granite_modules)
        self.shader = b'// fake prebuilt SPIR-V header\n'
        self.parallel_url = upstream / 'parallel-gs'
        self.commit = make_repo(
            self.parallel_url,
            {'gs/shaders/slangmosh.hpp': self.shader, 'README.md': b'fake\n'},
            gitlinks=[('Granite', self.granite)],
            gitmodules=submodule_entry('Granite', granite_url))
        self.manifest = {
            'repository': self.parallel_url.as_posix(),
            'commit': self.commit,
            'license': 'LGPL-3.0-or-later',
            'shader_header': {'path': 'gs/shaders/slangmosh.hpp',
                              'sha256': hashlib.sha256(self.shader).hexdigest()},
            'submodules': [
                {'parent': '', 'path': 'Granite', 'url': granite_url.as_posix(), 'commit': self.granite,
                 'license': 'MIT'},
                {'parent': 'Granite', 'path': 'third_party/khronos/vulkan-headers',
                 'url': vulkan_url.as_posix(), 'commit': self.vulkan_headers, 'license': 'Apache-2.0 OR MIT'},
                {'parent': 'Granite', 'path': 'third_party/volk', 'url': volk_url.as_posix(),
                 'commit': self.volk, 'license': 'MIT'},
            ],
        }

    def pin(self):
        return fetcher.parse_pin(self.manifest, allow_local_urls=True)


class PinParsingTests(unittest.TestCase):
    def test_repository_manifest_is_valid(self):
        pin = fetcher.load_pin(fetcher.DEFAULT_MANIFEST)
        self.assertEqual(pin.commit, 'cc6184af7e0c03da603045ca371ffa5dae9b0655')
        self.assertEqual(pin.license, 'LGPL-3.0-or-later')
        self.assertEqual([s.location for s in pin.submodules],
                         ['Granite', 'Granite/third_party/khronos/vulkan-headers', 'Granite/third_party/volk'])
        self.assertEqual(pin.submodules[0].commit, '16e7395f6a4858c1783dbf6f521f90b9d5f82ac5')
        self.assertEqual(pin.submodules[1].commit, '11d6898377797e07dbd543aaaa367e4465074597')
        self.assertEqual(pin.submodules[2].commit, '47cddf7ed97b94118a08aacb548a411188e016cc')

    def test_short_commit_and_plain_http_are_refused(self):
        good = json.loads(fetcher.DEFAULT_MANIFEST.read_text(encoding='utf-8'))
        short = dict(good, commit='cc6184a')
        with self.assertRaises(fetcher.FetchError):
            fetcher.parse_pin(short)
        plain = dict(good, repository='http://github.com/Arntzen-Software/parallel-gs')
        with self.assertRaises(fetcher.FetchError):
            fetcher.parse_pin(plain)

    def test_submodule_parent_must_come_first_and_stay_inside_tree(self):
        good = json.loads(fetcher.DEFAULT_MANIFEST.read_text(encoding='utf-8'))
        reordered = dict(good, submodules=list(reversed(good['submodules'])))
        with self.assertRaises(fetcher.FetchError):
            fetcher.parse_pin(reordered)
        escaping = json.loads(json.dumps(good))
        escaping['submodules'][0]['path'] = '../outside'
        with self.assertRaises(fetcher.FetchError):
            fetcher.parse_pin(escaping)


class FetchTests(unittest.TestCase):
    """Clones are the slow part on Windows, so one fetched tree is shared and copied for the mutating checks."""

    @classmethod
    def setUpClass(cls):
        cls.folder = Path(tempfile.mkdtemp(prefix='pgs-fetch-test-'))
        cls.upstream = FakeUpstream(cls.folder)
        cls.pin = cls.upstream.pin()
        cls.shared = cls.folder / 'shared' / 'parallel-gs'
        if fetcher.fetch_pin(cls.pin, cls.shared, local_git) != 'fetched':
            raise AssertionError('the shared fixture checkout was not fetched')

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.folder, ignore_errors=True)

    def copy_of_shared(self, name):
        """A private copy of the verified checkout; git metadata uses relative gitdir links, so copies verify."""
        destination = self.folder / name
        shutil.copytree(self.shared, destination)
        return destination

    def test_fresh_fetch_initialises_only_the_needed_submodules(self):
        dest = self.folder / 'fresh' / 'parallel-gs'
        # fetch_pin verifies the tree before returning 'fetched'; verification costs about 4 s per run here.
        self.assertEqual(fetcher.fetch_pin(self.pin, dest, local_git), 'fetched')
        self.assertTrue((dest / 'Granite/third_party/volk/volk.c').is_file())
        unused = dest / 'Granite/third_party/unused'
        self.assertFalse(unused.is_dir() and any(unused.iterdir()))

    def test_verified_existing_checkout_is_reused(self):
        self.assertEqual(fetcher.fetch_pin(self.pin, self.shared, local_git), 'verified')

    def test_tampered_shader_is_refused_and_kept(self):
        dest = self.copy_of_shared('tampered')
        (dest / 'gs/shaders/slangmosh.hpp').write_bytes(b'// edited\n')
        with self.assertRaises(fetcher.FetchError) as context:
            fetcher.fetch_pin(self.pin, dest, local_git)
        self.assertIn('refusing to touch it', str(context.exception))
        self.assertTrue((dest / 'gs/shaders/slangmosh.hpp').is_file())

    def test_unrelated_directory_is_refused(self):
        dest = self.folder / 'unrelated'
        dest.mkdir()
        (dest / 'notes.txt').write_text('owner data\n', encoding='utf-8')
        with self.assertRaises(fetcher.FetchError):
            fetcher.fetch_pin(self.pin, dest, local_git)
        self.assertTrue((dest / 'notes.txt').is_file())

    def test_wrong_granite_pin_is_detected_and_fresh_clone_removed(self):
        manifest = copy.deepcopy(self.upstream.manifest)
        manifest['submodules'][0]['commit'] = '0' * 40
        bad_pin = fetcher.parse_pin(manifest, allow_local_urls=True)
        dest = self.folder / 'wrong' / 'parallel-gs'
        with self.assertRaises(fetcher.FetchError):
            fetcher.fetch_pin(bad_pin, dest, local_git)
        self.assertFalse(dest.exists())

    def test_verify_reports_each_drift_kind(self):
        dest = self.copy_of_shared('drift')
        (dest / 'gs/shaders/slangmosh.hpp').write_bytes(b'// edited\n')
        problems = fetcher.verify_checkout(dest, self.pin, local_git)
        self.assertTrue(any('sha256' in p for p in problems))
        self.assertTrue(any('local changes' in p for p in problems))
        manifest = copy.deepcopy(self.upstream.manifest)
        manifest['shader_header']['sha256'] = '0' * 64
        other_pin = fetcher.parse_pin(manifest, allow_local_urls=True)
        self.assertTrue(any('sha256' in p for p in fetcher.verify_checkout(self.shared, other_pin, local_git)))

    def test_pin_listing_an_uninitialised_submodule_is_detected(self):
        manifest = copy.deepcopy(self.upstream.manifest)
        manifest['submodules'].append({'parent': 'Granite', 'path': 'third_party/unused', 'url': 'unused',
                                       'commit': self.upstream.unused, 'license': 'MIT'})
        widened = fetcher.parse_pin(manifest, allow_local_urls=True)
        problems = fetcher.verify_checkout(self.shared, widened, local_git)
        self.assertTrue(any('not initialised' in p for p in problems))


if __name__ == '__main__':
    unittest.main()
