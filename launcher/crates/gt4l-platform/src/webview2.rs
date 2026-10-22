//! The WebView2 Runtime check at launcher start (TASK-091, issue #80). The launcher is drawn by Microsoft Edge WebView2;
//! without the runtime the window stays blank. This module answers one question before any window exists: is the
//! runtime installed?
//!
//! The registry reads mirror the installer's `WebView2Present` function (`installer/gt4recomp.nsi`): the `pv` value of
//! the client `{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}` is read from HKLM WOW6432Node, then HKLM, then HKCU. An empty
//! value or `0.0.0.0` means absent. The decision is pure: it takes a [`PvSource`] (the registry in the launcher, a fake
//! in the tests) and two flags from the environment, so it is tested without a registry.

/// The Edge WebView2 client id (the same as the installer's `WEBVIEW2_CLIENT`).
pub const WEBVIEW2_CLIENT: &str = "{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}";

/// The registry hive of a `pv` lookup.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Hive {
    LocalMachine,
    CurrentUser,
}

/// The three `pv` lookups of the installer, in the installer's order.
pub const PV_LOOKUPS: [(Hive, &str); 3] = [
    (Hive::LocalMachine, "SOFTWARE\\WOW6432Node\\Microsoft\\EdgeUpdate\\Clients\\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}"),
    (Hive::LocalMachine, "SOFTWARE\\Microsoft\\EdgeUpdate\\Clients\\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}"),
    (Hive::CurrentUser, "Software\\Microsoft\\EdgeUpdate\\Clients\\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}"),
];

/// Reads the `pv` value of one lookup. `None` when the key or the value does not exist.
pub trait PvSource {
    fn read_pv(&self, hive: Hive, subkey: &str) -> Option<String>;
}

/// The result of the check. `version` is the installed runtime version, if one was found (for the log). `missing` is
/// what the launcher acts on.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Check {
    pub version: Option<String>,
    pub missing: bool,
}

/// Whether a `pv` value means an installed runtime: not empty and not `0.0.0.0`.
pub fn usable_version(value: Option<String>) -> Option<String> {
    let value = value?.trim().to_string();
    if value.is_empty() || value == "0.0.0.0" {
        None
    } else {
        Some(value)
    }
}

/// The first usable `pv` of the installer's lookups, or `None` when all are absent.
pub fn installed_version(source: &dyn PvSource) -> Option<String> {
    PV_LOOKUPS.iter().find_map(|(hive, subkey)| usable_version(source.read_pv(*hive, subkey)))
}

/// The check rule.
/// - `forced_missing` (test only, `GT4RECOMP_TEST_WEBVIEW2_MISSING=1`): the runtime counts as missing, whatever the
///   registry says. The version is still read, for the log.
/// - `fixed_runtime` (`WEBVIEW2_BROWSER_EXECUTABLE_FOLDER` set): a fixed-version runtime is in use and the registry
///   is not the judge, so nothing is missing.
/// - otherwise the runtime is missing when no usable `pv` exists.
pub fn check_with(source: &dyn PvSource, fixed_runtime: bool, forced_missing: bool) -> Check {
    let version = installed_version(source);
    let missing = if forced_missing {
        true
    } else if fixed_runtime {
        false
    } else {
        version.is_none()
    };
    Check { version, missing }
}

/// Whether the environment names a fixed-version runtime folder (a non-empty `WEBVIEW2_BROWSER_EXECUTABLE_FOLDER`).
pub fn fixed_runtime_folder_set(value: Option<&str>) -> bool {
    value.map(|text| !text.trim().is_empty()).unwrap_or(false)
}

/// The check on this machine: the registry through [`WindowsPvSource`]. Other systems have no WebView2 registry and
/// are never reported missing.
#[cfg(windows)]
pub fn check_installed(fixed_runtime: bool, forced_missing: bool) -> Check {
    check_with(&WindowsPvSource, fixed_runtime, forced_missing)
}

/// Other systems: nothing is checked.
#[cfg(not(windows))]
pub fn check_installed(_fixed_runtime: bool, _forced_missing: bool) -> Check {
    Check { version: None, missing: false }
}

/// The `pv` values from the Windows registry (64-bit view of this process; the installer's HKLM WOW6432Node key is
/// read by its explicit path).
#[cfg(windows)]
pub struct WindowsPvSource;

#[cfg(windows)]
impl PvSource for WindowsPvSource {
    fn read_pv(&self, hive: Hive, subkey: &str) -> Option<String> {
        use windows::core::HSTRING;
        use windows::Win32::System::Registry::{RegGetValueW, HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE, RRF_RT_REG_SZ};
        let root = match hive {
            Hive::LocalMachine => HKEY_LOCAL_MACHINE,
            Hive::CurrentUser => HKEY_CURRENT_USER,
        };
        let (key, value) = (HSTRING::from(subkey), HSTRING::from("pv"));
        let mut buffer = vec![0u16; 512];
        let mut size = (buffer.len() * 2) as u32;
        // SAFETY: the buffer and its size in bytes live for the call; the value is read as REG_SZ only.
        let status = unsafe {
            RegGetValueW(root, &key, &value, RRF_RT_REG_SZ, None, Some(buffer.as_mut_ptr() as *mut _), Some(&mut size))
        };
        if status.is_err() {
            return None;
        }
        let length = (size as usize / 2).min(buffer.len());
        Some(String::from_utf16_lossy(&buffer[..length]).trim_end_matches('\0').to_string())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::collections::BTreeMap;

    /// Fake registry: (hive, subkey) to the `pv` value.
    #[derive(Default)]
    struct FakeRegistry(BTreeMap<(u8, String), String>);

    impl FakeRegistry {
        fn with(mut self, hive: Hive, subkey: &str, value: &str) -> Self {
            self.0.insert((hive as u8, subkey.to_string()), value.to_string());
            self
        }
    }

    impl PvSource for FakeRegistry {
        fn read_pv(&self, hive: Hive, subkey: &str) -> Option<String> {
            self.0.get(&(hive as u8, subkey.to_string())).cloned()
        }
    }

    const WOW_LM: &str = PV_LOOKUPS[0].1;
    const LM: &str = PV_LOOKUPS[1].1;
    const HKCU: &str = PV_LOOKUPS[2].1;

    #[test]
    fn the_client_id_matches_the_installer() {
        assert_eq!(WEBVIEW2_CLIENT, "{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}");
        assert!(WOW_LM.contains(WEBVIEW2_CLIENT) && LM.contains(WEBVIEW2_CLIENT) && HKCU.contains(WEBVIEW2_CLIENT));
    }

    #[test]
    fn all_keys_missing_means_missing() {
        let registry = FakeRegistry::default();
        let check = check_with(&registry, false, false);
        assert!(check.missing);
        assert_eq!(check.version, None);
    }

    #[test]
    fn only_hkcu_present_is_installed() {
        let registry = FakeRegistry::default().with(Hive::CurrentUser, HKCU, "120.0.2210.91");
        let check = check_with(&registry, false, false);
        assert!(!check.missing);
        assert_eq!(check.version.as_deref(), Some("120.0.2210.91"));
    }

    #[test]
    fn the_wow6432_key_is_read_first() {
        let registry = FakeRegistry::default()
            .with(Hive::LocalMachine, WOW_LM, "131.0.1.0")
            .with(Hive::LocalMachine, LM, "130.0.1.0")
            .with(Hive::CurrentUser, HKCU, "129.0.1.0");
        assert_eq!(installed_version(&registry).as_deref(), Some("131.0.1.0"));
    }

    #[test]
    fn a_zero_version_is_absent_and_the_next_key_is_tried() {
        let registry = FakeRegistry::default()
            .with(Hive::LocalMachine, WOW_LM, "0.0.0.0")
            .with(Hive::LocalMachine, LM, "0.0.0.0")
            .with(Hive::CurrentUser, HKCU, "0.0.0.0");
        let check = check_with(&registry, false, false);
        assert!(check.missing, "0.0.0.0 means absent");
        assert_eq!(check.version, None);

        let registry = FakeRegistry::default()
            .with(Hive::LocalMachine, WOW_LM, "0.0.0.0")
            .with(Hive::LocalMachine, LM, "125.0.0.0");
        assert_eq!(installed_version(&registry).as_deref(), Some("125.0.0.0"));
    }

    #[test]
    fn an_empty_value_is_absent() {
        let registry = FakeRegistry::default().with(Hive::LocalMachine, WOW_LM, "   ");
        assert!(check_with(&registry, false, false).missing);
    }

    #[test]
    fn a_normal_version_is_installed() {
        let registry = FakeRegistry::default().with(Hive::LocalMachine, LM, "147.0.3912.86");
        let check = check_with(&registry, false, false);
        assert!(!check.missing);
        assert_eq!(check.version.as_deref(), Some("147.0.3912.86"));
    }

    #[test]
    fn the_fixed_runtime_folder_skips_the_registry_judgement() {
        let registry = FakeRegistry::default();
        let check = check_with(&registry, true, false);
        assert!(!check.missing, "a fixed-version runtime is in use");
        assert_eq!(check.version, None);
    }

    #[test]
    fn the_fixed_runtime_folder_variable_must_not_be_blank() {
        assert!(fixed_runtime_folder_set(Some("C:/runtime/Microsoft.WebView2.FixedVersionRuntime")));
        assert!(!fixed_runtime_folder_set(Some("")));
        assert!(!fixed_runtime_folder_set(Some("  ")));
        assert!(!fixed_runtime_folder_set(None));
    }

    #[test]
    fn the_test_override_forces_missing() {
        let registry = FakeRegistry::default().with(Hive::LocalMachine, LM, "147.0.3912.86");
        let check = check_with(&registry, false, true);
        assert!(check.missing, "GT4RECOMP_TEST_WEBVIEW2_MISSING=1 forces the absent result");
        assert_eq!(check.version.as_deref(), Some("147.0.3912.86"), "the version is still logged");
        assert!(check_with(&registry, true, true).missing, "the override wins over the fixed folder");
    }
}
