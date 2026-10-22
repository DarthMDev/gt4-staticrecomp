//! Removes the per-user install of launchers 1.0.0 to 1.2.0 (TASK-091, owner 2026-10-22). From 1.2.1 GT4Recomp
//! installs per machine (`C:\Program Files\GT4Recomp`). The installer removes the old per-user copy, but it runs
//! elevated and may run as another administrator account, whose `HKCU` and `%LOCALAPPDATA%` are not the player's.
//! So the launcher, running as the player, removes what it can still see at its start:
//!
//! - the old program files in the old install folder (only the files that installer wrote, and its `game` folder);
//! - the per-user Start-menu shortcuts (`Programs\Gran Turismo 4\*.lnk`);
//! - the per-user uninstall entry (`HKCU\...\Uninstall\GT4Recomp`) and `HKCU\Software\GT4Recomp`.
//!
//! It never touches data: not `%LOCALAPPDATA%\GT4Recomp`, not a folder that holds a `data` folder (a 1.2.1 layout),
//! not the folder the running launcher is in. Registry entries go only after the files are gone, so a file in use
//! is found again at the next start.

use std::fs;
use std::path::{Path, PathBuf};

/// The per-user uninstall entry of 1.0.0 to 1.2.0 (under `HKCU`).
pub const OLD_UNINSTALL_KEY: &str = r"Software\Microsoft\Windows\CurrentVersion\Uninstall\GT4Recomp";

/// The per-user application key of 1.0.0 to 1.2.0 (under `HKCU`, value `InstallDir`).
pub const OLD_APP_KEY: &str = r"Software\GT4Recomp";

/// The files the 1.0.0 to 1.2.0 installer put in the install folder, besides its `game` folder.
pub const OLD_PROGRAM_FILES: &[&str] = &["GT4Recomp.exe", "SDL3.dll", "NOTICE.md", "LICENSE.txt", "version.json", "uninstall.exe"];

/// The Start-menu folder and the shortcuts in it.
pub const START_MENU_FOLDER: &str = "Gran Turismo 4";
pub const START_MENU_LINKS: &[&str] = &["Gran Turismo 4.lnk", "Uninstall Gran Turismo 4.lnk"];

/// The `HKCU` registry calls the cleanup needs, so tests use a scratch map instead of the real registry.
pub trait UserRegistry {
    fn read_string(&self, key: &str, value: &str) -> Option<String>;
    fn key_exists(&self, key: &str) -> bool;
    fn delete_key(&self, key: &str) -> Result<(), String>;
}

/// Where to look.
pub struct Places<'a> {
    /// The folder of the running `GT4Recomp.exe` (never removed).
    pub exe_dir: &'a Path,
    /// `%LOCALAPPDATA%`: the default old install folder is `Programs\GT4Recomp` in it.
    pub local_app_data: Option<&'a Path>,
    /// The per-user Start-menu Programs folder.
    pub start_menu_programs: Option<&'a Path>,
}

/// What the cleanup did, for the launcher log.
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct Cleanup {
    pub removed: Vec<String>,
    pub problems: Vec<String>,
}

impl Cleanup {
    pub fn is_empty(&self) -> bool {
        self.removed.is_empty() && self.problems.is_empty()
    }

    pub fn log_lines(&self) -> Vec<String> {
        let mut lines: Vec<String> = self.removed.iter().map(|line| format!("old per-user install: removed {line}")).collect();
        lines.extend(self.problems.iter().map(|line| format!("old per-user install: {line}")));
        lines
    }
}

fn lower(path: &Path) -> String {
    path.to_string_lossy().replace('/', "\\").trim_end_matches('\\').to_lowercase()
}

/// Whether `inner` is `outer` or inside it (lexically, ignoring case).
fn within(inner: &Path, outer: &Path) -> bool {
    let (inner, outer) = (lower(inner), lower(outer));
    !outer.is_empty() && (inner == outer || inner.starts_with(&format!("{outer}\\")))
}

/// Whether a folder looks like the old per-user install: the launcher or its uninstaller is there.
fn looks_like_old_install(folder: &Path) -> bool {
    folder.join("GT4Recomp.exe").is_file() || folder.join("uninstall.exe").is_file()
}

/// Removes the old per-user install that this account can see. Never panics.
pub fn clean_old_per_user_install(registry: &dyn UserRegistry, places: &Places<'_>) -> Cleanup {
    let mut cleanup = Cleanup::default();
    let mut candidates: Vec<PathBuf> = Vec::new();
    for found in [
        registry.read_string(OLD_APP_KEY, "InstallDir"),
        registry.read_string(OLD_UNINSTALL_KEY, "InstallLocation"),
    ]
    .into_iter()
    .flatten()
    .filter(|text| !text.trim().is_empty())
    {
        candidates.push(PathBuf::from(found.trim().trim_matches('"')));
    }
    if let Some(local) = places.local_app_data {
        candidates.push(local.join("Programs").join("GT4Recomp"));
    }
    let mut seen: Vec<String> = Vec::new();
    candidates.retain(|path| {
        let key = lower(path);
        let new = !seen.contains(&key);
        seen.push(key);
        new
    });

    let registry_present = registry.key_exists(OLD_UNINSTALL_KEY) || registry.key_exists(OLD_APP_KEY);
    let mut files_left = false;
    let mut found_install = false;
    for folder in &candidates {
        if !folder.is_dir() || !looks_like_old_install(folder) {
            continue;
        }
        if within(places.exe_dir, folder) || within(folder, places.exe_dir) {
            // The running launcher is there (someone installed 1.2.1 into the old folder): keep the files.
            continue;
        }
        if folder.join("data").exists() {
            cleanup.problems.push(format!("{} holds a data folder; it is left as it is", folder.display()));
            files_left = true;
            continue;
        }
        found_install = true;
        for name in OLD_PROGRAM_FILES {
            let file = folder.join(name);
            if file.exists() {
                match fs::remove_file(&file) {
                    Ok(()) => {}
                    Err(error) => {
                        cleanup.problems.push(format!("{} could not be removed ({error}); retried at the next start", file.display()));
                        files_left = true;
                    }
                }
            }
        }
        let game = folder.join("game");
        if game.is_dir() {
            if let Err(error) = fs::remove_dir_all(&game) {
                cleanup.problems.push(format!("{} could not be removed ({error}); retried at the next start", game.display()));
                files_left = true;
            }
        }
        match fs::remove_dir(folder) {
            Ok(()) => cleanup.removed.push(format!("the program folder {}", folder.display())),
            Err(_) if !files_left => cleanup.removed.push(format!(
                "the program files in {} (other files there are kept)",
                folder.display()
            )),
            Err(_) => {}
        }
    }

    if found_install || registry_present {
        if let Some(programs) = places.start_menu_programs {
            let menu = programs.join(START_MENU_FOLDER);
            let mut removed_links = 0;
            for link in START_MENU_LINKS {
                if fs::remove_file(menu.join(link)).is_ok() {
                    removed_links += 1;
                }
            }
            let _ = fs::remove_dir(&menu);
            if removed_links > 0 {
                cleanup.removed.push(format!("{removed_links} Start-menu shortcut(s) in {}", menu.display()));
            }
        }
    }

    if registry_present && !files_left {
        for key in [OLD_UNINSTALL_KEY, OLD_APP_KEY] {
            if registry.key_exists(key) {
                match registry.delete_key(key) {
                    Ok(()) => cleanup.removed.push(format!("HKCU\\{key}")),
                    Err(error) => cleanup.problems.push(format!("HKCU\\{key} could not be removed: {error}")),
                }
            }
        }
    }
    cleanup
}

/// The real `HKCU` (current user, 64-bit view).
#[cfg(windows)]
pub struct WindowsUserRegistry;

#[cfg(windows)]
impl UserRegistry for WindowsUserRegistry {
    fn read_string(&self, key: &str, value: &str) -> Option<String> {
        use windows::core::HSTRING;
        use windows::Win32::System::Registry::{RegGetValueW, HKEY_CURRENT_USER, RRF_RT_REG_SZ};
        let (key, value) = (HSTRING::from(key), HSTRING::from(value));
        let mut buffer = vec![0u16; 2048];
        let mut size = (buffer.len() * 2) as u32;
        // SAFETY: the buffer and its size in bytes live for the call; the value is read as REG_SZ only.
        let status = unsafe {
            RegGetValueW(HKEY_CURRENT_USER, &key, &value, RRF_RT_REG_SZ, None, Some(buffer.as_mut_ptr() as *mut _), Some(&mut size))
        };
        if status.is_err() {
            return None;
        }
        let length = (size as usize / 2).min(buffer.len());
        let text = String::from_utf16_lossy(&buffer[..length]);
        Some(text.trim_end_matches('\0').to_string())
    }

    fn key_exists(&self, key: &str) -> bool {
        use windows::core::HSTRING;
        use windows::Win32::System::Registry::{RegCloseKey, RegOpenKeyExW, HKEY, HKEY_CURRENT_USER, KEY_READ};
        let key = HSTRING::from(key);
        let mut handle = HKEY::default();
        // SAFETY: `handle` receives an open key, closed right after.
        unsafe {
            if RegOpenKeyExW(HKEY_CURRENT_USER, &key, None, KEY_READ, &mut handle).is_ok() {
                let _ = RegCloseKey(handle);
                true
            } else {
                false
            }
        }
    }

    fn delete_key(&self, key: &str) -> Result<(), String> {
        use windows::core::HSTRING;
        use windows::Win32::System::Registry::{RegDeleteTreeW, HKEY_CURRENT_USER};
        let wide = HSTRING::from(key);
        // SAFETY: deletes the named subkey of HKCU and everything under it; the name is one of the two fixed keys.
        let status = unsafe { RegDeleteTreeW(HKEY_CURRENT_USER, &wide) };
        status.ok().map_err(|error| error.to_string())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::cell::RefCell;
    use std::collections::BTreeMap;

    /// A scratch `HKCU`: key -> value name -> text.
    #[derive(Default)]
    struct FakeRegistry(RefCell<BTreeMap<String, BTreeMap<String, String>>>);

    impl FakeRegistry {
        fn set(&self, key: &str, value: &str, text: &str) {
            self.0.borrow_mut().entry(key.to_string()).or_default().insert(value.to_string(), text.to_string());
        }
    }

    impl UserRegistry for FakeRegistry {
        fn read_string(&self, key: &str, value: &str) -> Option<String> {
            self.0.borrow().get(key)?.get(value).cloned()
        }
        fn key_exists(&self, key: &str) -> bool {
            self.0.borrow().contains_key(key)
        }
        fn delete_key(&self, key: &str) -> Result<(), String> {
            self.0.borrow_mut().remove(key);
            Ok(())
        }
    }

    fn scratch(name: &str) -> PathBuf {
        let path = std::env::temp_dir().join(format!("gt4l-legacy-install-{}-{name}", std::process::id()));
        let _ = fs::remove_dir_all(&path);
        fs::create_dir_all(&path).unwrap();
        path
    }

    /// The 1.2.0 per-user install as its installer left it, with registry entries and shortcuts.
    fn old_install(base: &Path, registry: &FakeRegistry) -> (PathBuf, PathBuf) {
        let folder = base.join("Local").join("Programs").join("GT4Recomp");
        fs::create_dir_all(folder.join("game").join("shaders")).unwrap();
        for name in OLD_PROGRAM_FILES {
            fs::write(folder.join(name), b"x").unwrap();
        }
        fs::write(folder.join("game").join("gt4vulkan.exe"), b"x").unwrap();
        let menu = base.join("Start Menu").join("Programs");
        fs::create_dir_all(menu.join(START_MENU_FOLDER)).unwrap();
        for link in START_MENU_LINKS {
            fs::write(menu.join(START_MENU_FOLDER).join(link), b"lnk").unwrap();
        }
        registry.set(OLD_APP_KEY, "InstallDir", &folder.display().to_string());
        registry.set(OLD_UNINSTALL_KEY, "InstallLocation", &folder.display().to_string());
        registry.set(OLD_UNINSTALL_KEY, "DisplayName", "Gran Turismo 4");
        (folder, menu)
    }

    #[test]
    fn the_old_per_user_install_is_removed_with_its_shortcuts_and_registry_entries() {
        let base = scratch("full");
        let registry = FakeRegistry::default();
        let (folder, menu) = old_install(&base, &registry);
        let data = base.join("Local").join("GT4Recomp");
        fs::create_dir_all(data.join("disc")).unwrap();
        fs::write(data.join("disc").join("SCUS-97328.iso"), b"disc").unwrap();
        let exe_dir = base.join("Program Files").join("GT4Recomp");
        let local = base.join("Local");
        let places = Places { exe_dir: &exe_dir, local_app_data: Some(&local), start_menu_programs: Some(&menu) };
        let cleanup = clean_old_per_user_install(&registry, &places);
        assert!(cleanup.problems.is_empty(), "{cleanup:?}");
        assert!(!folder.exists());
        assert!(!menu.join(START_MENU_FOLDER).exists());
        assert!(!registry.key_exists(OLD_UNINSTALL_KEY) && !registry.key_exists(OLD_APP_KEY));
        assert!(data.join("disc").join("SCUS-97328.iso").exists(), "data is never touched");
        // Nothing left: a second run does nothing.
        assert!(clean_old_per_user_install(&registry, &places).is_empty());
    }

    #[test]
    fn unknown_files_in_the_old_folder_are_kept() {
        let base = scratch("unknown");
        let registry = FakeRegistry::default();
        let (folder, menu) = old_install(&base, &registry);
        fs::write(folder.join("my notes.txt"), b"mine").unwrap();
        let exe_dir = base.join("Program Files").join("GT4Recomp");
        let local = base.join("Local");
        let places = Places { exe_dir: &exe_dir, local_app_data: Some(&local), start_menu_programs: Some(&menu) };
        let cleanup = clean_old_per_user_install(&registry, &places);
        assert!(folder.join("my notes.txt").exists());
        assert!(!folder.join("GT4Recomp.exe").exists() && !folder.join("game").exists());
        assert!(!registry.key_exists(OLD_UNINSTALL_KEY));
        assert!(cleanup.removed.iter().any(|line| line.contains("other files there are kept")), "{cleanup:?}");
    }

    #[test]
    fn the_folder_of_the_running_launcher_and_a_1_2_1_layout_are_never_removed() {
        let base = scratch("running");
        let registry = FakeRegistry::default();
        let (folder, menu) = old_install(&base, &registry);
        let local = base.join("Local");
        // 1.2.1 was installed into the old folder: the launcher runs from there.
        let places = Places { exe_dir: &folder, local_app_data: Some(&local), start_menu_programs: Some(&menu) };
        clean_old_per_user_install(&registry, &places);
        assert!(folder.join("GT4Recomp.exe").exists());
        // The per-user entries of the old install are still removed (the per-machine entry stands for it now).
        assert!(!registry.key_exists(OLD_UNINSTALL_KEY));
        registry.set(OLD_UNINSTALL_KEY, "InstallLocation", &folder.display().to_string());
        // A folder with a data folder is a 1.2.1 layout (a portable copy), whatever the registry says.
        fs::create_dir_all(folder.join("data")).unwrap();
        let elsewhere = base.join("elsewhere");
        let places = Places { exe_dir: &elsewhere, local_app_data: Some(&local), start_menu_programs: Some(&menu) };
        let cleanup = clean_old_per_user_install(&registry, &places);
        assert!(folder.join("GT4Recomp.exe").exists());
        assert!(registry.key_exists(OLD_UNINSTALL_KEY), "the registry stays while files stay");
        assert!(!cleanup.problems.is_empty());
    }

    #[test]
    fn a_registry_entry_without_its_folder_is_removed() {
        let base = scratch("orphan");
        let registry = FakeRegistry::default();
        registry.set(OLD_UNINSTALL_KEY, "InstallLocation", &base.join("gone").display().to_string());
        let exe_dir = base.join("exe");
        let places = Places { exe_dir: &exe_dir, local_app_data: None, start_menu_programs: None };
        let cleanup = clean_old_per_user_install(&registry, &places);
        assert!(!registry.key_exists(OLD_UNINSTALL_KEY));
        assert_eq!(cleanup.removed, vec![format!("HKCU\\{OLD_UNINSTALL_KEY}")]);
    }

    #[test]
    fn nothing_is_done_on_a_pc_without_the_old_install() {
        let base = scratch("none");
        let registry = FakeRegistry::default();
        let menu = base.join("menu");
        fs::create_dir_all(menu.join(START_MENU_FOLDER)).unwrap();
        fs::write(menu.join(START_MENU_FOLDER).join("Gran Turismo 4.lnk"), b"all-users or other").unwrap();
        let exe_dir = base.join("exe");
        let local = base.join("Local");
        let places = Places { exe_dir: &exe_dir, local_app_data: Some(&local), start_menu_programs: Some(&menu) };
        assert!(clean_old_per_user_install(&registry, &places).is_empty());
        assert!(menu.join(START_MENU_FOLDER).join("Gran Turismo 4.lnk").exists());
    }
}
