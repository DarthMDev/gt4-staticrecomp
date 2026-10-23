//! Lists the graphics cards and resolves the settings of one run (release 1.3.0, TASK-108). The rules are pure and
//! tested in `gt4l_core::gpu`; this module reads the machine: the Vulkan probe (`system::probe_vulkan`) and, on Windows,
//! the DXGI adapters (`IDXGIFactory1::EnumAdapters1`).

use gt4l_core::gpu::{self, DxgiGpu, GpuDevice, GraphicsPlan};
use gt4l_core::language::Language;
use gt4l_core::settings::Settings;
use gt4l_core::system_check::{self, VulkanProbe};

/// The graphics cards of this machine, as the launcher knows them now. A Vulkan device counts only when the game's
/// instance extensions are offered (`system_check::missing_instance_extensions`).
pub fn probe_gpus() -> Vec<GpuDevice> {
    let vulkan = match super::system::probe_vulkan() {
        VulkanProbe::Ready { instance_extensions, devices }
            if system_check::missing_instance_extensions(&instance_extensions).is_empty() =>
        {
            devices
                .iter()
                .filter(|device| system_check::is_gpu_driver(device))
                .map(|device| gpu::VulkanGpu {
                    name: device.name.clone(),
                    api: (device.api_major, device.api_minor),
                    integrated: device.device_type == system_check::DEVICE_TYPE_INTEGRATED,
                    vendor_id: device.vendor_id,
                    device_id: device.device_id,
                    driver_version: device.driver_version,
                    luid: device.luid,
                })
                .collect()
        }
        _ => Vec::new(),
    };
    gpu::merge(&vulkan, &dxgi_adapters())
}

/// The hardware and software DXGI adapters (Windows). Software ones are flagged, and `gpu::merge` drops them.
#[cfg(windows)]
fn dxgi_adapters() -> Vec<DxgiGpu> {
    use windows::Win32::Graphics::Dxgi::{CreateDXGIFactory1, IDXGIFactory1, DXGI_ADAPTER_FLAG_SOFTWARE};

    let mut adapters = Vec::new();
    // SAFETY: plain DXGI calls. The factory and each adapter are COM objects that the windows crate releases on drop.
    unsafe {
        let Ok(factory) = CreateDXGIFactory1::<IDXGIFactory1>() else {
            return adapters;
        };
        for index in 0u32.. {
            let Ok(adapter) = factory.EnumAdapters1(index) else {
                break;
            };
            let Ok(desc) = adapter.GetDesc1() else {
                continue;
            };
            let length = desc.Description.iter().position(|&unit| unit == 0).unwrap_or(desc.Description.len());
            // The LUID is the adapter's own id on this machine: a 64-bit value from its two halves.
            let luid = (u64::from(desc.AdapterLuid.HighPart as u32) << 32) | u64::from(desc.AdapterLuid.LowPart);
            adapters.push(DxgiGpu {
                name: String::from_utf16_lossy(&desc.Description[..length]),
                dedicated_video_memory: desc.DedicatedVideoMemory as u64,
                software: (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE.0 as u32) != 0,
                vendor_id: desc.VendorId,
                device_id: desc.DeviceId,
                luid: Some(luid),
            });
        }
    }
    adapters
}

#[cfg(not(windows))]
fn dxgi_adapters() -> Vec<DxgiGpu> {
    Vec::new()
}

/// The name of the card a run would use when that card is integrated, else `None` (the 1x integrated default, TASK-105).
pub fn integrated_gpu_name(stored_card: &str) -> Option<String> {
    let cards = probe_gpus();
    gpu::choose_card(stored_card, &cards)
        .map(|index| &cards[index])
        .filter(|card| card.integrated)
        .and_then(|card| card.name().map(str::to_string))
}

/// The settings of one run (TASK-108): the stored values with the API, the card and the Adapter resolved. Returns the
/// settings, the plan (for the log and the player's note) and the cards the machine has now.
pub fn run_settings(settings: &Settings, language: Language) -> (Settings, GraphicsPlan, Vec<GpuDevice>) {
    let cards = probe_gpus();
    let plan = gpu::plan(&settings.gs_value("graphics_api"), settings.get("graphics_card"), &cards, language);
    let mut run = settings.clone();
    run.set_run_gs("graphics_api", Some(&plan.api));
    run.set_run_gs("adapter", (!plan.adapter.is_empty()).then_some(plan.adapter.as_str()));
    let card_name = plan.card.as_ref().and_then(GpuDevice::name).unwrap_or(gpu::AUTOMATIC).to_string();
    // A card name the settings cannot hold (a control character, or over 256 bytes) is left out of the run: the run
    // keeps the stored value, which only matters to the Settings page; the plan's API and Adapter are already set.
    let _ = run.set("graphics_card", &card_name);
    (run, plan, cards)
}

/// The card names for the log, in the order the machine lists them.
pub fn card_list(cards: &[GpuDevice]) -> String {
    if cards.is_empty() {
        return "none".to_string();
    }
    cards.iter().map(|card| card.name().unwrap_or("unnamed")).collect::<Vec<_>>().join("; ")
}

#[cfg(test)]
mod probe_on_this_machine {
    /// Prints the raw Vulkan devices and DXGI adapters with their PCI ids and LUIDs, then the merged cards. Ignored by
    /// default: it loads the GPU drivers. Run with `cargo test -p gt4l-platform -- --ignored --nocapture`.
    #[test]
    #[ignore]
    fn gpu_inputs_on_this_machine() {
        if let gt4l_core::system_check::VulkanProbe::Ready { devices, .. } = super::super::system::probe_vulkan() {
            for device in devices {
                println!("vulkan {} vendor {:#06x} device {:#06x} type {} luid {:?}", device.name, device.vendor_id,
                         device.device_id, device.device_type, device.luid.map(|value| format!("{value:#018x}")));
            }
        }
        for adapter in super::dxgi_adapters() {
            println!("dxgi {} vendor {:#06x} device {:#06x} software {} luid {:?}", adapter.name, adapter.vendor_id,
                     adapter.device_id, adapter.software, adapter.luid.map(|value| format!("{value:#018x}")));
        }
        println!("cards: {}", super::card_list(&super::probe_gpus()));
    }
}
