//! Which graphics card renders the game and which Graphics API it uses (release 1.3.0, TASK-108).
//!
//! The launcher lists the graphics cards itself: the Vulkan device names from its Vulkan probe and the DXGI adapter
//! descriptions (`gt4l-platform::gpu` reads both). This module holds only the rules, as pure functions over plain
//! data, so every case is tested with fake devices:
//!
//! - **Automatic card.** A discrete card over an integrated one; among equals the most dedicated video memory.
//!   Software and CPU adapters are never candidates.
//! - **Automatic API.** Vulkan when the chosen card reports Vulkan 1.2 or newer, else Direct3D 11 (a card without
//!   Vulkan almost never has Direct3D 12, and Direct3D 11 is the fastest of the rest).
//! - **Explicit choices win.** A stored API or card is used as stored. Explicit Vulkan on a card without Vulkan 1.2
//!   runs with Direct3D 11 for that run, with a note for the player. A stored card that is no longer present falls
//!   back to Automatic for that run, with a log line.
//! - **Adapter.** The game's `Adapter` key gets the chosen card's name exactly as the active API reports it: the
//!   Vulkan name for Vulkan, the DXGI description for Direct3D. No name for that API means an empty Adapter (the
//!   system default), with a log line.

use crate::language::Language;
use crate::messages;

/// The stored value of the Automatic choices (`graphics_api` and `graphics_card`).
pub const AUTOMATIC: &str = "auto";
/// The `graphics_api` values of the three APIs the game offers.
pub const VULKAN_API: &str = "vulkan";
pub const DIRECT3D_12_API: &str = "d3d12";
pub const DIRECT3D_11_API: &str = "d3d11";
/// The API a run uses when Vulkan is needed but the chosen card has no Vulkan 1.2.
pub const FALLBACK_API: &str = DIRECT3D_11_API;
/// The lowest Vulkan version the game accepts on its card (`system_check::MINIMUM_VULKAN`).
pub const MINIMUM_VULKAN: (u32, u32) = (1, 2);
/// DXGI only (no Vulkan device type to ask): less dedicated memory than this is shared with the system, so the card
/// counts as integrated.
const SHARED_MEMORY_LIMIT: u64 = 512 * 1024 * 1024;

/// A GPU with its own Vulkan driver, as the Vulkan probe reports it.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct VulkanGpu {
    pub name: String,
    pub api: (u32, u32),
    pub integrated: bool,
}

/// A hardware adapter as DXGI reports it.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct DxgiGpu {
    pub name: String,
    pub dedicated_video_memory: u64,
    /// `DXGI_ADAPTER_FLAG_SOFTWARE` (the Direct3D software rasterizer).
    pub software: bool,
}

/// One graphics card: the Vulkan device and the DXGI adapter that are the same hardware (matched by name).
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct GpuDevice {
    pub vulkan_name: Option<String>,
    pub vulkan_api: (u32, u32),
    pub dxgi_name: Option<String>,
    pub dedicated_video_memory: u64,
    pub integrated: bool,
}

impl GpuDevice {
    /// The name shown to the player and stored as `graphics_card`: the Vulkan name, else the DXGI description.
    pub fn name(&self) -> Option<&str> {
        self.vulkan_name.as_deref().or(self.dxgi_name.as_deref())
    }

    /// True when the card reports Vulkan 1.2 or newer (with its own Vulkan driver).
    pub fn has_vulkan(&self) -> bool {
        self.vulkan_name.is_some() && self.vulkan_api >= MINIMUM_VULKAN
    }
}

/// Software and CPU adapters, by name (the Vulkan CPU type and the DXGI software flag are checked by the platform).
pub fn is_software_name(name: &str) -> bool {
    let lower = name.to_lowercase();
    ["microsoft basic render driver", "microsoft direct3d12", "llvmpipe", "swiftshader"]
        .iter()
        .any(|software| lower.contains(software))
}

/// The alphanumeric, lower-case form of a name, so the Vulkan name and the DXGI description of one card match even
/// when the spacing or the "(R)" marks differ.
fn key(name: &str) -> String {
    name.chars().filter(|c| c.is_alphanumeric()).flat_map(char::to_lowercase).collect()
}

/// Joins the Vulkan devices and the DXGI adapters into cards. A DXGI adapter with the same name as a Vulkan device
/// is the same card. Software adapters are dropped. A card known only to DXGI is integrated when its dedicated memory
/// is small.
pub fn merge(vulkan: &[VulkanGpu], dxgi: &[DxgiGpu]) -> Vec<GpuDevice> {
    let dxgi: Vec<&DxgiGpu> = dxgi.iter().filter(|adapter| !adapter.software && !is_software_name(&adapter.name)).collect();
    let mut matched = vec![false; dxgi.len()];
    let mut cards = Vec::new();
    for gpu in vulkan.iter().filter(|gpu| !is_software_name(&gpu.name)) {
        let partner = dxgi.iter().position(|adapter| key(&adapter.name) == key(&gpu.name));
        let (dxgi_name, memory) = match partner {
            Some(index) => {
                matched[index] = true;
                (Some(dxgi[index].name.clone()), dxgi[index].dedicated_video_memory)
            }
            None => (None, 0),
        };
        cards.push(GpuDevice {
            vulkan_name: Some(gpu.name.clone()),
            vulkan_api: gpu.api,
            dxgi_name,
            dedicated_video_memory: memory,
            integrated: gpu.integrated,
        });
    }
    for (index, adapter) in dxgi.iter().enumerate() {
        if !matched[index] {
            cards.push(GpuDevice {
                vulkan_name: None,
                vulkan_api: (0, 0),
                dxgi_name: Some(adapter.name.clone()),
                dedicated_video_memory: adapter.dedicated_video_memory,
                integrated: adapter.dedicated_video_memory < SHARED_MEMORY_LIMIT,
            });
        }
    }
    cards
}

/// The index of the Automatic card: a discrete card before an integrated one, then the most dedicated memory. The
/// first card wins a tie. `None` when no card has a name.
pub fn automatic_index(cards: &[GpuDevice]) -> Option<usize> {
    let mut best: Option<usize> = None;
    for (index, card) in cards.iter().enumerate() {
        if card.name().is_none() {
            continue;
        }
        let better = match best {
            None => true,
            Some(current) => {
                let known = &cards[current];
                (!card.integrated, card.dedicated_video_memory) > (!known.integrated, known.dedicated_video_memory)
            }
        };
        if better {
            best = Some(index);
        }
    }
    best
}

/// The index of the card with exactly this name (`GpuDevice::name`), or `None` when it is not present.
pub fn find_index(cards: &[GpuDevice], name: &str) -> Option<usize> {
    cards.iter().position(|card| card.name() == Some(name))
}

/// The card a run uses: the stored one when present, else (stored Automatic or a missing card) the Automatic card.
pub fn choose_card(stored_card: &str, cards: &[GpuDevice]) -> Option<usize> {
    let stored = stored_card.trim();
    if stored.is_empty() || stored == AUTOMATIC {
        return automatic_index(cards);
    }
    find_index(cards, stored).or_else(|| automatic_index(cards))
}

/// The label of an API in the log and the Automatic choice (product names, not translated).
pub fn api_name(api: &str) -> &'static str {
    match api {
        DIRECT3D_12_API => "Direct3D 12",
        DIRECT3D_11_API => "Direct3D 11",
        _ => "Vulkan",
    }
}

/// The API and card of one run, with the Adapter value and what to log.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct GraphicsPlan {
    /// The card the run uses; `None` when the machine has none.
    pub card: Option<GpuDevice>,
    /// `vulkan`, `d3d12` or `d3d11`.
    pub api: String,
    /// The `Adapter` value to write; empty means the system default.
    pub adapter: String,
    /// The note for the player when Vulkan was chosen but the card has no Vulkan 1.2; `None` otherwise.
    pub notice: Option<String>,
    /// Lines for the launcher log, in the order they happened.
    pub log: Vec<String>,
}

/// Plans one run from the stored `graphics_api` and `graphics_card` values and the cards the machine has.
pub fn plan(stored_api: &str, stored_card: &str, cards: &[GpuDevice], language: Language) -> GraphicsPlan {
    let mut log = Vec::new();
    let stored = stored_card.trim();
    if !stored.is_empty() && stored != AUTOMATIC && find_index(cards, stored).is_none() {
        log.push(format!("graphics card {stored} is not present; Automatic for this run"));
    }
    let card = choose_card(stored_card, cards).map(|index| cards[index].clone());
    let card_name = card.as_ref().and_then(GpuDevice::name).unwrap_or("none").to_string();
    let vulkan_ok = card.as_ref().is_some_and(GpuDevice::has_vulkan);
    let mut notice = None;
    let api = match stored_api.trim() {
        "" | AUTOMATIC => {
            if vulkan_ok {
                VULKAN_API.to_string()
            } else {
                FALLBACK_API.to_string()
            }
        }
        VULKAN_API if !vulkan_ok => {
            log.push(format!("graphics: {card_name} has no Vulkan 1.2; this run uses Direct3D 11"));
            notice = Some(messages::text("system.vulkan_fallback", language).to_string());
            FALLBACK_API.to_string()
        }
        other => other.to_string(),
    };
    let adapter = match &card {
        None => String::new(),
        Some(card) => {
            let name = if api == VULKAN_API { card.vulkan_name.clone() } else { card.dxgi_name.clone() };
            match name {
                Some(name) => name,
                None => {
                    log.push(format!(
                        "graphics: {card_name} has no name for {}; Adapter left empty (system default)",
                        api_name(&api)
                    ));
                    String::new()
                }
            }
        }
    };
    GraphicsPlan { card, api, adapter, notice, log }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn vulkan(name: &str, api: (u32, u32), integrated: bool) -> VulkanGpu {
        VulkanGpu { name: name.into(), api, integrated }
    }

    fn dxgi(name: &str, memory: u64) -> DxgiGpu {
        DxgiGpu { name: name.into(), dedicated_video_memory: memory, software: false }
    }

    const GIB: u64 = 1024 * 1024 * 1024;

    /// A laptop: Intel integrated graphics and an NVIDIA card, both with Vulkan 1.3 and DXGI names.
    fn laptop() -> Vec<GpuDevice> {
        merge(
            &[vulkan("Intel(R) UHD Graphics", (1, 3), true), vulkan("NVIDIA GeForce RTX 4060", (1, 3), false)],
            &[dxgi("Intel(R) UHD Graphics", 128 * 1024 * 1024), dxgi("NVIDIA GeForce RTX 4060", 8 * GIB)],
        )
    }

    fn english() -> Language {
        Language::fallback()
    }

    #[test]
    fn a_laptop_with_integrated_and_nvidia_picks_the_nvidia_card_on_vulkan() {
        let cards = laptop();
        let run = plan("auto", "auto", &cards, english());
        assert_eq!(run.card.as_ref().and_then(GpuDevice::name), Some("NVIDIA GeForce RTX 4060"));
        assert_eq!(run.api, "vulkan");
        assert_eq!(run.adapter, "NVIDIA GeForce RTX 4060");
        assert!(run.notice.is_none());
    }

    #[test]
    fn an_integrated_only_machine_without_vulkan_runs_direct3d_11_on_its_integrated_card() {
        let cards = merge(&[], &[dxgi("AMD Radeon(TM) Graphics", 512 * 1024 * 1024 - 1)]);
        let run = plan("auto", "auto", &cards, english());
        assert_eq!(run.api, "d3d11");
        assert_eq!(run.adapter, "AMD Radeon(TM) Graphics");
        assert!(run.card.as_ref().unwrap().integrated, "the 1x integrated default applies to the chosen card");
        assert!(run.notice.is_none(), "Automatic falls back without a note");
    }

    #[test]
    fn two_discrete_cards_pick_the_one_with_more_video_memory() {
        let cards = merge(
            &[vulkan("Radeon RX 6600", (1, 3), false), vulkan("Radeon RX 9060 XT", (1, 4), false)],
            &[dxgi("Radeon RX 6600", 8 * GIB), dxgi("Radeon RX 9060 XT", 16 * GIB)],
        );
        assert_eq!(plan("auto", "auto", &cards, english()).adapter, "Radeon RX 9060 XT");
        let cards = merge(
            &[vulkan("Radeon A", (1, 3), false), vulkan("Radeon B", (1, 3), false)],
            &[dxgi("Radeon A", 4 * GIB), dxgi("Radeon B", 12 * GIB)],
        );
        assert_eq!(automatic_index(&cards).map(|index| cards[index].name()), Some(Some("Radeon B")));
    }

    #[test]
    fn an_explicit_card_and_api_are_kept() {
        let cards = laptop();
        let run = plan("d3d12", "Intel(R) UHD Graphics", &cards, english());
        assert_eq!(run.card.as_ref().and_then(GpuDevice::name), Some("Intel(R) UHD Graphics"));
        assert_eq!(run.api, "d3d12");
        assert_eq!(run.adapter, "Intel(R) UHD Graphics", "the DXGI description of the chosen card");
        assert!(run.log.is_empty());
    }

    #[test]
    fn a_stored_card_that_is_gone_falls_back_to_automatic_with_a_log_line() {
        let cards = laptop();
        let run = plan("auto", "NVIDIA eGPU (unplugged)", &cards, english());
        assert_eq!(run.card.as_ref().and_then(GpuDevice::name), Some("NVIDIA GeForce RTX 4060"));
        assert!(run.log.iter().any(|line| line.contains("NVIDIA eGPU (unplugged) is not present")), "{:?}", run.log);
    }

    #[test]
    fn software_adapters_are_never_candidates() {
        let cards = merge(
            &[vulkan("llvmpipe (LLVM 19.1)", (1, 3), true)],
            &[
                DxgiGpu { name: "Microsoft Basic Render Driver".into(), dedicated_video_memory: 0, software: true },
                dxgi("Intel UHD", 128 * 1024 * 1024),
            ],
        );
        assert_eq!(cards.len(), 1, "the software rasterizer and the basic driver are dropped");
        assert_eq!(cards[0].name(), Some("Intel UHD"));
        assert_eq!(automatic_index(&[]), None);
        assert_eq!(plan("auto", "auto", &[], english()).adapter, "", "no card means the system default");
    }

    #[test]
    fn a_name_mismatch_between_vulkan_and_dxgi_leaves_the_adapter_empty_for_direct3d() {
        // Vulkan calls the card one thing, DXGI another: they are two cards, and the Vulkan-only one has no DXGI name.
        let cards = merge(&[vulkan("Intel(R) Graphics (Arrow Lake)", (1, 3), true)], &[dxgi("Intel Arc Graphics", 0)]);
        assert_eq!(cards.len(), 2);
        let run = plan("d3d11", "Intel(R) Graphics (Arrow Lake)", &cards, english());
        assert_eq!(run.adapter, "", "no DXGI name for this card");
        assert!(run.log.iter().any(|line| line.contains("has no name for Direct3D 11")), "{:?}", run.log);
        // The same card on Vulkan keeps its Vulkan name.
        assert_eq!(plan("vulkan", "Intel(R) Graphics (Arrow Lake)", &cards, english()).adapter, "Intel(R) Graphics (Arrow Lake)");
    }

    #[test]
    fn explicit_vulkan_on_a_card_without_vulkan_12_runs_direct3d_11_with_the_note() {
        let cards = merge(&[vulkan("Intel HD 4000", (1, 1), true)], &[dxgi("Intel HD 4000", 128 * 1024 * 1024)]);
        let run = plan("vulkan", "auto", &cards, english());
        assert_eq!(run.api, "d3d11");
        assert_eq!(run.adapter, "Intel HD 4000");
        assert_eq!(
            run.notice.as_deref(),
            Some("No Vulkan 1.2 graphics card was found, so the game uses Direct3D 11. You can change this in Settings > Graphics.")
        );
        let portuguese = Language::from_tag("pt-BR").expect("pt-BR is a language");
        assert_eq!(
            plan("vulkan", "auto", &cards, portuguese).notice.as_deref(),
            Some("Nenhuma placa de vídeo com Vulkan 1.2 foi encontrada, então o jogo usa Direct3D 11. Você pode mudar isso em Configurações > Gráficos.")
        );
    }

    #[test]
    fn explicit_vulkan_with_no_card_at_all_runs_direct3d_11_without_a_card() {
        let run = plan("vulkan", "auto", &[], english());
        assert_eq!(run.api, "d3d11");
        assert_eq!(run.adapter, "");
        assert!(run.notice.is_some());
    }

    #[test]
    fn the_names_of_one_card_match_across_vulkan_and_dxgi() {
        let cards = merge(&[vulkan("NVIDIA GeForce RTX 4060", (1, 3), false)], &[dxgi("NVIDIA GeForce RTX 4060 ", 8 * GIB)]);
        assert_eq!(cards.len(), 1, "a DXGI description with a trailing space is the same card");
        assert_eq!(cards[0].dedicated_video_memory, 8 * GIB);
        assert_eq!(cards[0].dxgi_name.as_deref(), Some("NVIDIA GeForce RTX 4060 "));
    }
}
