"""Derive the crash-safe memory card flush from the pinned MIT donor IOP SIO2 source (TASK-025, Part A).

The donor rewrites the whole card in place (ofstream truncate) once 50 VBlanks pass without a
card write, and it never flushes on exit. This derives runtime/src/iop/sio2.cpp in which:
  * flush_cards() writes through native/memcard_store.cpp (temporary file, flush, rename), so a
    crash mid-write leaves the previous card intact;
  * flush_cards_now() flushes a pending save on the exit path (called by native/window.cpp when
    GT4_MEMCARD_FLUSH_OVERLAY is defined).

Overlay sections are anchored on unique donor text. Removing them must restore the normalized
input, as scripts/prepare_perf_runtime.py checks for its overlays.
"""

import argparse
import hashlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DONOR_SIO2 = 'runtime/src/iop/sio2.cpp'
DONOR_SIO2_SHA256 = 'add0dcff27ddb3d497e6138c3bd6f77272eeb3a0a33d02920eece215255057ea'

INCLUDE_ANCHOR = '#include "sio2.hpp"\n'
INCLUDE_SECTION = '\n#include "memcard_store.hpp"\n'

FLUSH_START = 'void flush_cards() {\n'
FLUSH_END = 'std::uint32_t read(std::uint32_t a) {\n'
FLUSH_SECTION = '''// TASK-025 Part A: crash-safe card write. The image goes to a temporary file that is renamed
// over the card, so a crash leaves either the previous or the new image.
void write_card_image() {
    Card &c = g_card;
    if (gt4::memcard::write_card_atomically(g_options.memcard_path, c.data)) {
        c.dirty = false;
        if (g_options.on_memcard_saved) g_options.on_memcard_saved();
        return;
    }
    // Keep the card dirty and retry after another quiet period.
    c.quiet = 0;
    std::fprintf(stderr, "[mc] cannot write %s; will retry\\n", g_options.memcard_path.c_str());
}

void flush_cards() {
    Card &c = g_card;
    if (!c.dirty || ++c.quiet < 50) return;
    write_card_image();
}

// Exit path: a save made less than 50 VBlanks before the window closed is written now.
void flush_cards_now() {
    if (g_card.present && g_card.dirty) write_card_image();
}

'''


def replace_once(source, anchor, replacement, label):
    if source.count(anchor) != 1:
        raise ValueError(f'{label} anchor must appear exactly once in the donor source')
    return source.replace(anchor, replacement, 1)


def section_span(source, start, end, label):
    first = source.index(start)
    if source.count(start) != 1:
        raise ValueError(f'{label} start anchor must appear exactly once')
    last = source.index(end, first)
    return first, last


def apply_overlay(source):
    """Returns the derived sio2.cpp text (LF line endings)."""
    source = replace_once(source, INCLUDE_ANCHOR, INCLUDE_ANCHOR + INCLUDE_SECTION, 'include')
    first, last = section_span(source, FLUSH_START, FLUSH_END, 'flush_cards')
    return source[:first] + FLUSH_SECTION + source[last:]


def remove_overlay(source):
    """Reverses apply_overlay so the input can be checked byte for byte."""
    if source.count(INCLUDE_ANCHOR + INCLUDE_SECTION) != 1:
        raise ValueError('derived source lacks the memcard include section')
    source = source.replace(INCLUDE_ANCHOR + INCLUDE_SECTION, INCLUDE_ANCHOR, 1)
    first = source.index(FLUSH_SECTION_MARKER)
    last = source.index(FLUSH_END, first)
    return source[:first] + DONOR_FLUSH + source[last:]


FLUSH_SECTION_MARKER = '// TASK-025 Part A: crash-safe card write.'

# The donor flush_cards() text, restored by remove_overlay(). Kept verbatim (LF) from the pin.
DONOR_FLUSH = '''void flush_cards() {
    Card &c = g_card;
    if (!c.dirty || ++c.quiet < 50) return;
    c.dirty = false;
    std::ofstream out(g_options.memcard_path, std::ios::binary | std::ios::trunc);
    if (out) out.write(reinterpret_cast<const char *>(c.data.data()), static_cast<std::streamsize>(c.data.size()));
    if (!out) {
        std::fprintf(stderr, "[mc] cannot write %s\\n", g_options.memcard_path.c_str());
        return;
    }
    out.close();
    if (g_options.on_memcard_saved) g_options.on_memcard_saved();
}

'''


def read_donor(assets_root):
    donor_path = assets_root / 'private/references/ps2-web-recomp' / DONOR_SIO2
    raw = donor_path.read_bytes()
    if hashlib.sha256(raw).hexdigest() != DONOR_SIO2_SHA256:
        raise ValueError(f'Donor source differs from the pinned version: {DONOR_SIO2}')
    return raw.decode('utf-8').replace('\r\n', '\n')


def prepare(output, assets_root=None):
    assets_root = Path(assets_root or ROOT).resolve()
    output = output.resolve()
    private = assets_root / 'private'
    if not output.is_relative_to(private) or output == private:
        raise ValueError('Derived donor code must stay inside the assets checkout private/')
    source = read_donor(assets_root)
    derived = apply_overlay(source)
    if remove_overlay(derived) != source:
        raise ValueError('Memcard flush overlay does not reverse to the donor input')
    output.mkdir(parents=True, exist_ok=True)
    (output / 'sio2.cpp').write_text(derived, encoding='utf-8')
    return derived


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=Path)
    parser.add_argument('--assets-root', type=Path)
    options = parser.parse_args()
    prepare(options.output, options.assets_root)
