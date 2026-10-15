//! One running game per memory card. The lock is an exclusive file lock held by the launcher for
//! the whole session. Windows releases it when the process exits, even after a crash, so a stale
//! lock file never blocks the next start (the same idea as `session.lock` in the Python launcher).

use std::fs::{File, OpenOptions};
use std::io;
use std::path::{Path, PathBuf};

#[derive(Debug)]
pub struct GameLock {
    file: File,
    path: PathBuf,
}

#[derive(Debug)]
pub enum LockError {
    AlreadyRunning,
    Io(io::Error),
}

impl std::fmt::Display for LockError {
    fn fmt(&self, formatter: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            LockError::AlreadyRunning => write!(formatter, "Gran Turismo 4 is already running"),
            LockError::Io(error) => write!(formatter, "could not lock the game session: {error}"),
        }
    }
}

impl std::error::Error for LockError {}

impl GameLock {
    /// Takes the lock without waiting. The lock is released when the returned value is dropped.
    pub fn acquire(path: &Path) -> Result<GameLock, LockError> {
        if let Some(parent) = path.parent() {
            std::fs::create_dir_all(parent).map_err(LockError::Io)?;
        }
        let file = OpenOptions::new()
            .create(true)
            .read(true)
            .write(true)
            .truncate(false)
            .open(path)
            .map_err(LockError::Io)?;
        match file.try_lock() {
            Ok(()) => Ok(GameLock { file, path: path.to_path_buf() }),
            Err(std::fs::TryLockError::WouldBlock) => Err(LockError::AlreadyRunning),
            Err(std::fs::TryLockError::Error(error)) => Err(LockError::Io(error)),
        }
    }

    pub fn path(&self) -> &Path {
        &self.path
    }

    /// Keeps the file handle alive for the caller's scope; the lock ends with the value.
    pub fn file(&self) -> &File {
        &self.file
    }
}
