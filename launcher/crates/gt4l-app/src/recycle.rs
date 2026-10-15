//! Moves files and folders to the Recycle Bin (SHFileOperationW with FOF_ALLOWUNDO), so a delete
//! in the launcher can always be undone from the bin.

use std::path::Path;

/// Moves one file or folder to the Recycle Bin without confirmation dialogs.
#[cfg(windows)]
pub fn move_to_recycle_bin(path: &Path) -> Result<(), String> {
    use std::os::windows::ffi::OsStrExt;
    use windows::core::PCWSTR;
    use windows::Win32::Foundation::HWND;
    use windows::Win32::UI::Shell::{
        SHFileOperationW, FOF_ALLOWUNDO, FOF_NOCONFIRMATION, FOF_NOERRORUI, FOF_SILENT, FO_DELETE, SHFILEOPSTRUCTW,
    };

    // pFrom is a double-null-terminated list of paths.
    let mut wide: Vec<u16> = path.as_os_str().encode_wide().collect();
    wide.push(0);
    wide.push(0);
    let mut operation = SHFILEOPSTRUCTW {
        hwnd: HWND(std::ptr::null_mut()),
        wFunc: FO_DELETE,
        pFrom: PCWSTR(wide.as_ptr()),
        fFlags: (FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI).0 as u16,
        ..Default::default()
    };
    // SAFETY: `operation` and the buffer behind `pFrom` live until the call returns.
    let result = unsafe { SHFileOperationW(&mut operation) };
    if result != 0 {
        return Err(format!("could not move {} to the Recycle Bin (error {result:#x})", path.display()));
    }
    if operation.fAnyOperationsAborted.as_bool() {
        return Err(format!("the delete of {} was cancelled", path.display()));
    }
    Ok(())
}

#[cfg(not(windows))]
pub fn move_to_recycle_bin(path: &Path) -> Result<(), String> {
    Err(format!("the Recycle Bin is only available on Windows: {}", path.display()))
}
