//! Core of the GT4Recomp launcher: settings, config file, paths, launch plan and exit classification.
//! No UI here. The one Windows-specific piece is the process-lock file, which uses std APIs only.

pub mod config;
pub mod exit;
pub mod launch;
pub mod lock;
pub mod paths;
pub mod settings;

pub use config::ConfigFile;
pub use settings::{Settings, SettingDef, SETTINGS};

/// Version shown in the UI and written to receipts.
pub const LAUNCHER_VERSION: &str = env!("CARGO_PKG_VERSION");
