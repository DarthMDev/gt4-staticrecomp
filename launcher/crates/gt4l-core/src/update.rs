//! The update decision (pure, no network). The launcher only checks and offers; it never installs by itself, except
//! the first gt4-gs install during setup, which has no installed version to replace (see `gs_release`).
//!
//! An offer exists when the stream has a stable release newer than the installed version, at or above the
//! stream's minimum, and the player has not chosen "Skip this version" for exactly that version.

use crate::releases::{self, Release, Stream};
use crate::version::Version;

/// One update the launcher offers (or, with `installed` = None, one first install it performs silently).
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Offer {
    pub stream: Stream,
    /// The installed version, `None` when the stream is not installed yet.
    pub installed: Option<Version>,
    pub latest: Version,
    /// The release notes, as GitHub stores them (plain text; the UI renders a safe subset).
    pub notes: String,
    /// The download asset of the latest version.
    pub package: String,
}

impl Offer {
    /// True when there is something to replace, i.e. the popup applies. A missing install is not an update.
    pub fn is_update(&self) -> bool {
        self.installed.is_some()
    }
}

/// The offer for one stream, given its release list and the installed version. `skipped` is the version the
/// player chose to skip. Returns `None` when there is nothing new.
pub fn decide(stream: Stream, installed: Option<Version>, releases: &[Release], skipped: Option<Version>) -> Option<Offer> {
    let latest_release: &Release = releases::select_latest(releases, stream.minimum())?;
    let latest = latest_release.version?;
    if let Some(current) = installed {
        if latest <= current {
            return None;
        }
    }
    if skipped == Some(latest) {
        return None;
    }
    Some(Offer {
        stream,
        installed,
        latest,
        notes: latest_release.notes.clone(),
        package: stream.package_name(latest),
    })
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::releases::parse_releases;

    const LAUNCHER_LIST: &str = r#"[
      {"tag_name":"v1.0.1","prerelease":false,"draft":false,"body":"Fixes.","assets":[]},
      {"tag_name":"v1.1.0-rc1","prerelease":true,"draft":false,"body":"","assets":[]},
      {"tag_name":"v1.0.0","prerelease":false,"draft":false,"body":"First.","assets":[]}
    ]"#;

    const RENDERER_LIST: &str = r#"[
      {"tag_name":"gt4gs-v1.10.0","prerelease":false,"draft":false,"body":"Faster.","assets":[]},
      {"tag_name":"gt4gs-v1.9.0","prerelease":false,"draft":false,"body":"","assets":[]}
    ]"#;

    fn launcher() -> Vec<Release> {
        parse_releases(LAUNCHER_LIST, Stream::Launcher).unwrap()
    }

    fn renderer() -> Vec<Release> {
        parse_releases(RENDERER_LIST, Stream::Renderer).unwrap()
    }

    #[test]
    fn a_newer_stable_release_is_offered_with_its_notes_and_package() {
        let offer = decide(Stream::Launcher, Some(Version(1, 0, 0)), &launcher(), None).unwrap();
        assert_eq!(offer.latest, Version(1, 0, 1));
        assert_eq!(offer.notes, "Fixes.");
        assert_eq!(offer.package, "GT4Recomp-Setup-1.0.1.exe");
        assert!(offer.is_update());
    }

    #[test]
    fn the_same_or_an_older_version_gives_no_offer() {
        assert!(decide(Stream::Launcher, Some(Version(1, 0, 1)), &launcher(), None).is_none());
        assert!(decide(Stream::Launcher, Some(Version(1, 0, 2)), &launcher(), None).is_none());
    }

    #[test]
    fn prereleases_are_never_offered() {
        let offer = decide(Stream::Launcher, Some(Version(1, 0, 1)), &launcher(), None);
        assert!(offer.is_none(), "1.1.0-rc1 is a prerelease and must not be offered");
    }

    #[test]
    fn numeric_comparison_offers_1_10_over_1_9() {
        let offer = decide(Stream::Renderer, Some(Version(1, 9, 0)), &renderer(), None).unwrap();
        assert_eq!(offer.latest, Version(1, 10, 0));
        assert_eq!(offer.package, "gt4-gs-1.10.0-windows-x64.zip");
    }

    #[test]
    fn a_skipped_version_is_not_offered_again_but_a_later_one_is() {
        let skipped = Some(Version(1, 10, 0));
        assert!(decide(Stream::Renderer, Some(Version(1, 9, 0)), &renderer(), skipped).is_none());
        let newer = parse_releases(
            r#"[{"tag_name":"gt4gs-v1.11.0","prerelease":false,"draft":false,"assets":[]}]"#,
            Stream::Renderer,
        )
        .unwrap();
        let mut all = renderer();
        all.extend(newer);
        assert_eq!(decide(Stream::Renderer, Some(Version(1, 9, 0)), &all, skipped).unwrap().latest, Version(1, 11, 0));
    }

    #[test]
    fn a_missing_install_is_a_first_install_not_an_update() {
        let offer = decide(Stream::Renderer, None, &renderer(), None).unwrap();
        assert!(!offer.is_update());
        assert_eq!(offer.latest, Version(1, 10, 0));
    }

    #[test]
    fn below_the_minimum_nothing_is_offered() {
        let old = parse_releases(r#"[{"tag_name":"gt4gs-v1.0.0","assets":[]}]"#, Stream::Renderer).unwrap();
        assert!(decide(Stream::Renderer, None, &old, None).is_none());
    }
}
