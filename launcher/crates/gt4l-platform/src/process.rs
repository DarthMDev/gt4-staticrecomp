//! Windows process rules for the launcher: one launcher at a time, and the game ends with the launcher.
//!
//! The launcher puts itself in a Job Object with kill-on-close. The game is started from the launcher,
//! so it joins the same job; when the launcher exits, the job's last handle closes and Windows ends
//! every process in it. The job handle is intentionally never closed before exit.
//!
//! The job allows breakaway: a process the launcher must outlive on purpose (the GT4Recomp setup program of a
//! self-update) is started with `spawn_outside_job`. Without it, the setup joined the job and Windows ended it the
//! moment the launcher exited to let it replace the program files (launcher 1.0.0 bug, 2026-10-21).

#[cfg(windows)]
mod windows_impl {
    use std::ffi::c_void;
    use std::mem::size_of;

    use windows::core::{w, PCWSTR};
    use windows::Win32::Foundation::{CloseHandle, GetLastError, ERROR_ALREADY_EXISTS};
    use windows::Win32::System::JobObjects::{
        AssignProcessToJobObject, CreateJobObjectW, JobObjectExtendedLimitInformation, SetInformationJobObject,
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION, JOB_OBJECT_LIMIT_BREAKAWAY_OK, JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE,
    };
    use windows::Win32::System::Threading::{CreateMutexW, GetCurrentProcess};
    use windows::Win32::UI::WindowsAndMessaging::{FindWindowW, SetForegroundWindow, ShowWindow, SW_RESTORE};

    /// Title of the launcher window (the window title set by the UI shell). Not the game title: the game window is
    /// "Gran Turismo 4", and focusing it from a second launcher would bring the game forward.
    const WINDOW_TITLE: PCWSTR = w!("GT4Recomp");

    /// Returns `false` when another launcher already runs; that window is brought forward instead.
    pub fn claim_single_instance() -> bool {
        // SAFETY: the name is a static wide string. The handle is kept open until the process exits.
        let created = unsafe { CreateMutexW(None, true, w!("Local\\GT4Recomp.Launcher")) };
        if created.is_err() {
            return true; // cannot tell; start anyway
        }
        // SAFETY: no arguments; reads the error of the call above on this thread.
        if unsafe { GetLastError() } != ERROR_ALREADY_EXISTS {
            return true;
        }
        // SAFETY: FindWindowW with a null class name looks up the title only.
        if let Ok(window) = unsafe { FindWindowW(PCWSTR::null(), WINDOW_TITLE) } {
            // SAFETY: `window` is a window handle returned by FindWindowW.
            unsafe {
                let _ = ShowWindow(window, SW_RESTORE);
                let _ = SetForegroundWindow(window);
            }
        }
        false
    }

    /// Puts this process in a job that ends its members when the launcher exits.
    pub fn join_kill_on_close_job() -> Result<(), String> {
        // SAFETY: all pointers passed are to locals that live for the duration of each call.
        unsafe {
            let job = CreateJobObjectW(None, PCWSTR::null()).map_err(|error| format!("job object: {error}"))?;
            let mut limits = JOBOBJECT_EXTENDED_LIMIT_INFORMATION::default();
            // Children stay in the job unless they ask to leave it (CREATE_BREAKAWAY_FROM_JOB); the game never does.
            limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE | JOB_OBJECT_LIMIT_BREAKAWAY_OK;
            let set = SetInformationJobObject(
                job,
                JobObjectExtendedLimitInformation,
                &limits as *const _ as *const c_void,
                size_of::<JOBOBJECT_EXTENDED_LIMIT_INFORMATION>() as u32,
            );
            if let Err(error) = set {
                let _ = CloseHandle(job);
                return Err(format!("job limits: {error}"));
            }
            if let Err(error) = AssignProcessToJobObject(job, GetCurrentProcess()) {
                let _ = CloseHandle(job);
                return Err(format!("job assignment: {error}"));
            }
            // The handle is not closed: it closes at exit, and then Windows ends the game.
        }
        Ok(())
    }
}

#[cfg(windows)]
pub use windows_impl::{claim_single_instance, join_kill_on_close_job};

/// `CREATE_BREAKAWAY_FROM_JOB`: the new process is not a member of the launcher's kill-on-close job.
#[cfg(windows)]
const CREATE_BREAKAWAY_FROM_JOB: u32 = 0x0100_0000;

/// Starts a program that must keep running after the launcher exits (the setup program of a self-update). On
/// Windows it leaves the launcher's kill-on-close job; any other child (the game) stays in it.
pub fn spawn_outside_job(command: &mut std::process::Command) -> std::io::Result<std::process::Child> {
    #[cfg(windows)]
    {
        use std::os::windows::process::CommandExt;
        command.creation_flags(CREATE_BREAKAWAY_FROM_JOB);
    }
    command.spawn()
}

#[cfg(not(windows))]
pub fn claim_single_instance() -> bool {
    true
}

#[cfg(not(windows))]
pub fn join_kill_on_close_job() -> Result<(), String> {
    Ok(())
}
