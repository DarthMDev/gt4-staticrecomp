//! System requirements checked before setup and before Play (TASK-081): the processor and Vulkan.
//!
//! This module holds the rules only (pure functions over plain data), so every case is tested without the
//! hardware. `gt4l-platform::system` reads CPUID and probes the Vulkan loader and fills these structs.
//!
//! - **Processor.** The launcher requires AVX2 with FMA, BMI1 and BMI2 (the x86-64-v3 level), and Windows must save
//!   the AVX register state (OSXSAVE and XCR0 bits 1 and 2), otherwise AVX instructions fault even on a capable CPU.
//!   This is the release policy (owner, TASK-081). Measured on slice211 and slice213, `gt4vulkan.exe` itself is built
//!   for baseline x86-64 (`GT4_X86_64_V3` is off; its only AVX code is the CPU-dispatched Visual C++ library code),
//!   so the policy is stricter than the game executable needs. `MINIMUM_CPU` is the single place to change it.
//! - **Vulkan.** The game creates its own Vulkan instance with four instance extensions and needs a GPU that
//!   reports Vulkan 1.2 or newer (`native/native_vulkan.cpp`). The PCSX2 GS renderer (`gt4-gs.exe`) renders with
//!   Vulkan as well (`docs/GS-SETTINGS.md`, row `renderer`), so the check applies to both renderers.

/// What the processor and Windows report (CPUID leaf 1 and leaf 7, and XCR0).
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct CpuFeatures {
    /// CPUID.1:ECX bit 28.
    pub avx: bool,
    /// CPUID.7.0:EBX bit 5.
    pub avx2: bool,
    /// CPUID.1:ECX bit 12.
    pub fma: bool,
    /// CPUID.7.0:EBX bit 3.
    pub bmi1: bool,
    /// CPUID.7.0:EBX bit 8.
    pub bmi2: bool,
    /// CPUID.1:ECX bit 27: the operating system enabled XSAVE (needed to read XCR0).
    pub osxsave: bool,
    /// XCR0 bits 1 (SSE state) and 2 (AVX state) are both set: Windows saves the AVX registers.
    pub os_saves_avx_state: bool,
}

/// The processor level the release requires.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum CpuLevel {
    /// AVX2, FMA, BMI1 and BMI2 with the AVX state enabled by the OS (x86-64-v3 without the MOVBE/F16C/LZCNT details).
    Avx2,
}

/// The release policy (see the module comment).
pub const MINIMUM_CPU: CpuLevel = CpuLevel::Avx2;

/// The message for a processor below the minimum (owner wording, TASK-081).
pub const CPU_UNSUPPORTED_MESSAGE: &str = "Your processor is not supported: GT4Recomp needs a CPU with AVX2 \
(Intel 4th gen Core / AMD Ryzen or newer).";

/// The message for a capable processor whose AVX state Windows does not save.
pub const CPU_AVX_DISABLED_MESSAGE: &str = "Your processor has AVX2, but Windows has AVX turned off, so GT4Recomp \
cannot run. Update Windows, and check that AVX is not disabled in the BIOS or by a boot setting.";

/// The hardware features missing for `level`, by name (empty when the processor has them all).
pub fn missing_cpu_features(features: &CpuFeatures, level: CpuLevel) -> Vec<&'static str> {
    match level {
        CpuLevel::Avx2 => {
            let required = [
                ("AVX", features.avx),
                ("AVX2", features.avx2),
                ("FMA", features.fma),
                ("BMI1", features.bmi1),
                ("BMI2", features.bmi2),
            ];
            required.iter().filter(|(_, present)| !present).map(|(name, _)| *name).collect()
        }
    }
}

/// `None` when the processor and Windows can run the release; otherwise the message for the player.
pub fn cpu_problem(features: &CpuFeatures) -> Option<String> {
    if !missing_cpu_features(features, MINIMUM_CPU).is_empty() {
        return Some(CPU_UNSUPPORTED_MESSAGE.to_string());
    }
    if !features.osxsave || !features.os_saves_avx_state {
        return Some(CPU_AVX_DISABLED_MESSAGE.to_string());
    }
    None
}

/// The lowest Vulkan version the game accepts on its GPU (`native/native_vulkan.cpp`: VK_API_VERSION_1_2).
pub const MINIMUM_VULKAN: (u32, u32) = (1, 2);

/// The instance extensions `gt4vulkan.exe` enables at `vkCreateInstance` (`native/native_vulkan.cpp`). Without any of
/// them the game cannot create its Vulkan instance.
pub const GAME_INSTANCE_EXTENSIONS: [&str; 4] = [
    "VK_KHR_surface",
    "VK_KHR_win32_surface",
    "VK_KHR_get_surface_capabilities2",
    "VK_KHR_surface_maintenance1",
];

/// One GPU as the Vulkan loader reports it.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct VulkanDevice {
    pub name: String,
    pub api_major: u32,
    pub api_minor: u32,
    /// VkPhysicalDeviceProperties::vendorID (0x1002 AMD, 0x10DE NVIDIA, 0x8086 Intel, 0x1414 Microsoft).
    pub vendor_id: u32,
    /// VkPhysicalDeviceType: 0 other, 1 integrated, 2 discrete, 3 virtual, 4 CPU.
    pub device_type: u32,
}

/// Microsoft's vendor id: the "Microsoft Direct3D12 (...)" devices of the Vulkan-on-D3D12 layer (Dozen, part of the
/// OpenCL, OpenGL and Vulkan Compatibility Pack). They are a translation layer, not the GPU's own Vulkan driver.
pub const MICROSOFT_VENDOR_ID: u32 = 0x1414;
/// VK_PHYSICAL_DEVICE_TYPE_CPU: a software rasterizer (llvmpipe, SwiftShader).
pub const DEVICE_TYPE_CPU: u32 = 4;

/// True for a GPU with its own Vulkan driver: not a software rasterizer and not the D3D12 translation layer.
pub fn is_gpu_driver(device: &VulkanDevice) -> bool {
    device.vendor_id != MICROSOFT_VENDOR_ID && device.device_type != DEVICE_TYPE_CPU
}

/// What the Vulkan probe found.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum VulkanProbe {
    /// `vulkan-1.dll` is not in System32 or does not export `vkGetInstanceProcAddr`.
    LoaderMissing,
    /// The loader is present, but `vkCreateInstance` failed (VkResult).
    InstanceFailed { code: i32 },
    /// The loader works. `instance_extensions` lists what the loader and drivers offer.
    Ready { instance_extensions: Vec<String>, devices: Vec<VulkanDevice> },
}

const DRIVER_ADVICE: &str = "Update your graphics driver from NVIDIA, AMD or Intel, then start GT4Recomp again.";

/// `None` when the game can create its Vulkan instance and a GPU reports at least `MINIMUM_VULKAN`; otherwise the
/// message for the player.
pub fn vulkan_problem(probe: &VulkanProbe) -> Option<String> {
    let (major, minor) = MINIMUM_VULKAN;
    match probe {
        VulkanProbe::LoaderMissing => Some(format!(
            "Vulkan is not installed on this PC. GT4Recomp needs a graphics card with Vulkan {major}.{minor} or newer. \
             {DRIVER_ADVICE}"
        )),
        VulkanProbe::InstanceFailed { code } => Some(format!(
            "Vulkan did not start on this PC (error {code}). GT4Recomp needs a graphics card with Vulkan \
             {major}.{minor} or newer. {DRIVER_ADVICE}"
        )),
        VulkanProbe::Ready { instance_extensions, devices } => {
            let devices: Vec<&VulkanDevice> = devices.iter().filter(|device| is_gpu_driver(device)).collect();
            if devices.is_empty() {
                return Some(format!(
                    "No graphics card with Vulkan was found. GT4Recomp needs a graphics card with Vulkan \
                     {major}.{minor} or newer. {DRIVER_ADVICE}"
                ));
            }
            if !devices.iter().any(|device| (device.api_major, device.api_minor) >= (major, minor)) {
                let best = devices
                    .iter()
                    .max_by_key(|device| (device.api_major, device.api_minor))
                    .expect("devices is not empty");
                return Some(format!(
                    "Your graphics card ({}) reports Vulkan {}.{}; GT4Recomp needs Vulkan {major}.{minor} or newer. \
                     {DRIVER_ADVICE}",
                    best.name, best.api_major, best.api_minor
                ));
            }
            let missing = missing_instance_extensions(instance_extensions);
            if !missing.is_empty() {
                return Some(format!(
                    "Your graphics driver is too old for GT4Recomp (missing Vulkan {}). {DRIVER_ADVICE}",
                    missing.join(", ")
                ));
            }
            None
        }
    }
}

/// The game's instance extensions that the loader does not offer.
pub fn missing_instance_extensions(offered: &[String]) -> Vec<&'static str> {
    GAME_INSTANCE_EXTENSIONS.iter().copied().filter(|name| !offered.iter().any(|offer| offer == name)).collect()
}

#[cfg(test)]
mod tests {
    use super::*;

    fn haswell() -> CpuFeatures {
        CpuFeatures { avx: true, avx2: true, fma: true, bmi1: true, bmi2: true, osxsave: true, os_saves_avx_state: true }
    }

    #[test]
    fn a_full_avx2_processor_with_the_os_state_passes() {
        assert_eq!(cpu_problem(&haswell()), None);
        assert!(missing_cpu_features(&haswell(), CpuLevel::Avx2).is_empty());
    }

    #[test]
    fn each_missing_feature_is_refused_with_the_owner_message() {
        let cases: [(&str, fn(&mut CpuFeatures)); 5] = [
            ("AVX", |f| f.avx = false),
            ("AVX2", |f| f.avx2 = false),
            ("FMA", |f| f.fma = false),
            ("BMI1", |f| f.bmi1 = false),
            ("BMI2", |f| f.bmi2 = false),
        ];
        for (name, clear) in cases {
            let mut features = haswell();
            clear(&mut features);
            assert_eq!(missing_cpu_features(&features, CpuLevel::Avx2), vec![name]);
            assert_eq!(cpu_problem(&features).as_deref(), Some(CPU_UNSUPPORTED_MESSAGE), "{name}");
        }
    }

    #[test]
    fn sandy_bridge_and_older_amd_fx_are_refused() {
        // Sandy/Ivy Bridge: AVX without AVX2, FMA or BMI. FX (Piledriver): AVX, FMA and BMI1, no AVX2 or BMI2.
        let sandy = CpuFeatures { avx2: false, fma: false, bmi1: false, bmi2: false, ..haswell() };
        let piledriver = CpuFeatures { avx2: false, bmi2: false, ..haswell() };
        assert_eq!(missing_cpu_features(&sandy, CpuLevel::Avx2), vec!["AVX2", "FMA", "BMI1", "BMI2"]);
        assert_eq!(missing_cpu_features(&piledriver, CpuLevel::Avx2), vec!["AVX2", "BMI2"]);
        assert_eq!(cpu_problem(&sandy).as_deref(), Some(CPU_UNSUPPORTED_MESSAGE));
        assert_eq!(cpu_problem(&piledriver).as_deref(), Some(CPU_UNSUPPORTED_MESSAGE));
    }

    #[test]
    fn a_capable_processor_without_the_os_avx_state_gets_the_windows_message() {
        let no_xsave = CpuFeatures { osxsave: false, os_saves_avx_state: false, ..haswell() };
        let no_state = CpuFeatures { os_saves_avx_state: false, ..haswell() };
        assert_eq!(cpu_problem(&no_xsave).as_deref(), Some(CPU_AVX_DISABLED_MESSAGE));
        assert_eq!(cpu_problem(&no_state).as_deref(), Some(CPU_AVX_DISABLED_MESSAGE));
    }

    #[test]
    fn nothing_detected_is_the_unsupported_message() {
        assert_eq!(cpu_problem(&CpuFeatures::default()).as_deref(), Some(CPU_UNSUPPORTED_MESSAGE));
    }

    fn all_extensions() -> Vec<String> {
        let mut list: Vec<String> = GAME_INSTANCE_EXTENSIONS.iter().map(|name| name.to_string()).collect();
        list.push("VK_EXT_debug_utils".into());
        list
    }

    fn device(name: &str, major: u32, minor: u32) -> VulkanDevice {
        VulkanDevice { name: name.into(), api_major: major, api_minor: minor, vendor_id: 0x1002, device_type: 2 }
    }

    #[test]
    fn a_vulkan_1_2_gpu_with_the_game_extensions_passes() {
        let probe = VulkanProbe::Ready { instance_extensions: all_extensions(), devices: vec![device("GPU", 1, 4)] };
        assert_eq!(vulkan_problem(&probe), None);
        let exact = VulkanProbe::Ready { instance_extensions: all_extensions(), devices: vec![device("GPU", 1, 2)] };
        assert_eq!(vulkan_problem(&exact), None);
    }

    #[test]
    fn one_good_gpu_among_old_ones_passes() {
        let probe = VulkanProbe::Ready {
            instance_extensions: all_extensions(),
            devices: vec![device("Old", 1, 0), device("New", 1, 3)],
        };
        assert_eq!(vulkan_problem(&probe), None);
    }

    #[test]
    fn no_loader_no_instance_no_gpu_and_old_gpu_name_the_driver_update() {
        let probes = [
            VulkanProbe::LoaderMissing,
            VulkanProbe::InstanceFailed { code: -9 },
            VulkanProbe::Ready { instance_extensions: all_extensions(), devices: vec![] },
            VulkanProbe::Ready { instance_extensions: all_extensions(), devices: vec![device("HD 4000", 1, 0)] },
        ];
        for probe in probes {
            let message = vulkan_problem(&probe).expect("refused");
            assert!(message.contains("Update your graphics driver"), "{message}");
            assert!(message.contains("Vulkan"), "{message}");
        }
        let old = VulkanProbe::Ready { instance_extensions: all_extensions(), devices: vec![device("HD 4000", 1, 1)] };
        assert!(vulkan_problem(&old).unwrap().contains("(HD 4000) reports Vulkan 1.1"));
    }

    #[test]
    fn the_d3d12_translation_layer_and_software_rasterizers_do_not_count() {
        let dozen = VulkanDevice {
            name: "Microsoft Direct3D12 (Microsoft Basic Render Driver)".into(),
            api_major: 1,
            api_minor: 2,
            vendor_id: MICROSOFT_VENDOR_ID,
            device_type: 1,
        };
        let llvmpipe =
            VulkanDevice { name: "llvmpipe".into(), api_major: 1, api_minor: 3, vendor_id: 0x10005, device_type: 4 };
        assert!(!is_gpu_driver(&dozen) && !is_gpu_driver(&llvmpipe));
        let only_layers = VulkanProbe::Ready { instance_extensions: all_extensions(), devices: vec![dozen.clone(), llvmpipe] };
        assert!(vulkan_problem(&only_layers).unwrap().contains("No graphics card with Vulkan was found"));
        // This machine's real list (2026-10-21): the AMD driver plus two Dozen devices.
        let real = VulkanProbe::Ready {
            instance_extensions: all_extensions(),
            devices: vec![device("AMD Radeon RX 9060 XT", 1, 4), dozen],
        };
        assert_eq!(vulkan_problem(&real), None);
    }

    #[test]
    fn a_driver_without_a_game_instance_extension_is_refused_by_name() {
        let mut offered = all_extensions();
        offered.retain(|name| name != "VK_KHR_surface_maintenance1");
        let probe = VulkanProbe::Ready { instance_extensions: offered.clone(), devices: vec![device("GPU", 1, 3)] };
        let message = vulkan_problem(&probe).expect("refused");
        assert!(message.contains("missing Vulkan VK_KHR_surface_maintenance1"), "{message}");
        assert_eq!(missing_instance_extensions(&offered), vec!["VK_KHR_surface_maintenance1"]);
    }

    #[test]
    fn the_extension_list_matches_the_game_source() {
        // native/native_vulkan.cpp enables exactly these four at vkCreateInstance; a change there must change this list.
        let source = include_str!("../../../../native/native_vulkan.cpp");
        let start = source.find("const char *extensions[] = {").expect("extension list in native_vulkan.cpp");
        let block = &source[start..start + source[start..].find("};").expect("end of list")];
        for name in GAME_INSTANCE_EXTENSIONS {
            assert!(block.contains(&format!("\"{name}\"")), "{name} not in the game's list");
        }
        assert_eq!(block.matches("\"VK_").count(), GAME_INSTANCE_EXTENSIONS.len());
        assert!(source.contains("properties.apiVersion < VK_API_VERSION_1_2"), "the game's minimum is Vulkan 1.2");
    }
}
