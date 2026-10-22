//! What the per-machine install records in HKLM `Software\GT4Recomp` (TASK-091, release 1.2.1):
//! - `InstallDir`: the program folder of the last per-machine install (the setup writes it; the launcher compares it
//!   with its own folder before a silent self-update).
//! - `PreviousDataDir`: the data folder of an earlier install in another folder. A reinstall into another folder
//!   writes it (when the old folder had a data folder); the launcher moves that folder's large data at its first start.
//!
//! Both are read in the 64-bit view, which is the view of the 64-bit launcher.

use std::path::PathBuf;

/// The key both values live in.
pub const INSTALL_KEY: &str = r"Software\GT4Recomp";

/// The program folder of the last per-machine install.
pub const INSTALL_DIR_VALUE: &str = "InstallDir";

/// The data folder of an earlier install that the launcher still has to move (`<old folder>\data`).
pub const PREVIOUS_DATA_VALUE: &str = "PreviousDataDir";

/// The program folder the last per-machine install recorded, as the registry has it.
pub fn registered_install_folder() -> Option<String> {
    machine_string(INSTALL_KEY, INSTALL_DIR_VALUE).filter(|text| !text.trim().is_empty())
}

/// The data folder of an earlier install, if the setup recorded one.
pub fn previous_data_dir() -> Option<PathBuf> {
    machine_string(INSTALL_KEY, PREVIOUS_DATA_VALUE).filter(|text| !text.trim().is_empty()).map(PathBuf::from)
}

/// A REG_SZ value under HKEY_LOCAL_MACHINE (64-bit view). `None` when the key or the value does not exist.
#[cfg(windows)]
fn machine_string(key: &str, value: &str) -> Option<String> {
    use windows::core::HSTRING;
    use windows::Win32::System::Registry::{RegGetValueW, HKEY_LOCAL_MACHINE, RRF_RT_REG_SZ};
    let (key, value) = (HSTRING::from(key), HSTRING::from(value));
    let mut buffer = vec![0u16; 2048];
    let mut size = (buffer.len() * 2) as u32;
    // SAFETY: the buffer and its size in bytes live for the call; the value is read as REG_SZ only.
    let status = unsafe {
        RegGetValueW(HKEY_LOCAL_MACHINE, &key, &value, RRF_RT_REG_SZ, None, Some(buffer.as_mut_ptr() as *mut _), Some(&mut size))
    };
    if status.is_err() {
        return None;
    }
    let length = (size as usize / 2).min(buffer.len());
    Some(String::from_utf16_lossy(&buffer[..length]).trim_end_matches('\0').to_string())
}

/// Other systems have no registry: nothing is recorded.
#[cfg(not(windows))]
fn machine_string(_key: &str, _value: &str) -> Option<String> {
    None
}
