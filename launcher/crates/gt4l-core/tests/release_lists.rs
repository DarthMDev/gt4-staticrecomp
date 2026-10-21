//! Release-list fixtures for both repositories (no network). `gt4gs-list.json` uses the real tag and asset names of
//! `moesuito/gt4recomp-gs` (checked with `gh release view gt4gs-v1.0.1 --json assets` and, for 1.1.0, on 2026-10-21). `gt4recomp-list.json` has
//! the shape GT4Recomp will publish; it is a fixture, not a copy of a real release.

use gt4l_core::releases::{self, Stream};
use gt4l_core::update::{self, Offer};
use gt4l_core::Version;

fn fixture(name: &str) -> String {
    let path = std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("tests/fixtures/releases").join(name);
    std::fs::read_to_string(path).unwrap()
}

#[test]
fn renderer_list_uses_the_real_asset_names_and_skips_drafts_and_prereleases() {
    let releases = releases::parse_releases(&fixture("gt4gs-list.json"), Stream::Renderer).unwrap();
    let chosen = releases::select_latest(&releases, Stream::Renderer.minimum()).unwrap();
    assert_eq!(chosen.tag, "gt4gs-v1.1.0");
    assert_eq!(chosen.version, Some(Version(1, 1, 0)));
    let package = Stream::Renderer.package_name(Version(1, 1, 0));
    assert_eq!(package, "gt4-gs-1.1.0-windows-x64.zip");
    assert!(chosen.asset(&package).is_some(), "the package is listed under the real name");
    assert!(chosen.asset(releases::CHECKSUM_ASSET).is_some());
    assert!(chosen.notes.contains("**Notes**"), "the body is kept as the release notes");
}

#[test]
fn renderer_list_refuses_a_launcher_tag_and_an_old_stable() {
    let releases = releases::parse_releases(&fixture("gt4gs-list.json"), Stream::Renderer).unwrap();
    let launcher_tag = releases.iter().find(|release| release.tag == "v1.0.0").unwrap();
    assert_eq!(launcher_tag.version, None, "a `v` tag is not a renderer version");
    for tag in ["gt4gs-v1.0.0", "gt4gs-v1.0.1"] {
        let old = releases.iter().find(|release| release.tag == tag).unwrap();
        assert!(old.is_stable());
        assert!(old.version.unwrap() < Stream::Renderer.minimum(), "{tag} is below the 1.1.0 minimum");
    }
}

#[test]
fn launcher_list_offers_the_first_stable_release_and_never_the_draft() {
    let releases = releases::parse_releases(&fixture("gt4recomp-list.json"), Stream::Launcher).unwrap();
    let offer: Offer = update::decide(Stream::Launcher, None, &releases, None).unwrap();
    assert_eq!(offer.latest, Version(1, 0, 0), "1.1.0 is a draft, 1.0.1-rc1 a prerelease");
    assert_eq!(offer.package, "GT4Recomp-Setup-1.0.0.exe");
    assert!(offer.notes.contains("First stable"));
}

#[test]
fn the_renderer_update_decision_uses_the_real_list() {
    let releases = releases::parse_releases(&fixture("gt4gs-list.json"), Stream::Renderer).unwrap();
    assert!(update::decide(Stream::Renderer, Some(Version(1, 1, 0)), &releases, None).is_none());
    // Release 1.0.0 installed gt4-gs 1.0.1: it is offered 1.1.0 (and replaced at launch, see gs_release::needs_install).
    let offer = update::decide(Stream::Renderer, Some(Version(1, 0, 1)), &releases, None).unwrap();
    assert_eq!(offer.latest, Version(1, 1, 0));
    assert!(offer.is_update());
}
