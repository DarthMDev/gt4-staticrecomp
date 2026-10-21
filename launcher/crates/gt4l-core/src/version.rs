//! Semantic versions of the two release streams (GT4Recomp and gt4-gs), SemVer `MAJOR.MINOR.PATCH`.
//!
//! Pre-release tags and build metadata are refused: the launcher only ever installs stable releases (see
//! `docs/RELEASES.md`). Comparison is numeric, so `1.10.0` is newer than `1.9.0`.

use std::fmt;

#[derive(Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub struct Version(pub u32, pub u32, pub u32);

impl Version {
    /// Parses `1.2.3` or `v1.2.3`. Pre-release tags, build metadata, signs and missing parts are refused.
    pub fn parse(text: &str) -> Option<Version> {
        let body = text.strip_prefix('v').unwrap_or(text);
        let mut parts = body.split('.');
        let major = number(parts.next()?)?;
        let minor = number(parts.next()?)?;
        let patch = number(parts.next()?)?;
        if parts.next().is_some() {
            return None;
        }
        Some(Version(major, minor, patch))
    }

    /// `1.2.3`, without a `v`.
    pub fn text(&self) -> String {
        format!("{}.{}.{}", self.0, self.1, self.2)
    }
}

impl fmt::Display for Version {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(formatter, "{}.{}.{}", self.0, self.1, self.2)
    }
}

/// Digits only: `u32::from_str` alone would also accept a leading `+`.
fn number(part: &str) -> Option<u32> {
    if part.is_empty() || !part.bytes().all(|byte| byte.is_ascii_digit()) {
        return None;
    }
    part.parse().ok()
}

#[cfg(test)]
mod tests {
    use super::Version;

    #[test]
    fn parses_plain_and_v_prefixed_stable_versions() {
        assert_eq!(Version::parse("1.0.0"), Some(Version(1, 0, 0)));
        assert_eq!(Version::parse("v0.3.2"), Some(Version(0, 3, 2)));
        assert_eq!(Version::parse("10.20.30"), Some(Version(10, 20, 30)));
    }

    #[test]
    fn refuses_prereleases_metadata_signs_and_missing_parts() {
        for text in ["1.0.0-rc1", "1.0.0+build7", "1.0", "1.0.0.0", "+1.0.0", "1.-0.0", "", "v", "a.b.c", "1..0"] {
            assert_eq!(Version::parse(text), None, "{text} must be refused");
        }
    }

    #[test]
    fn comparison_is_numeric_not_textual() {
        assert!(Version(1, 10, 0) > Version(1, 9, 0));
        assert!(Version(1, 0, 1) > Version(1, 0, 0));
        assert!(Version(2, 0, 0) > Version(1, 99, 99));
        assert_eq!(Version(1, 0, 1).to_string(), "1.0.1");
    }
}
