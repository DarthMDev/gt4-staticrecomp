//! Windows and process code shared by the launcher UI: controller capture (XInput), deletes to the
//! Recycle Bin, one launcher at a time, the kill-on-close job, and starting and watching the game.
//! Nothing here knows about pages, windows or the web view.

pub mod game;
pub mod pad;
pub mod process;
pub mod recycle;
