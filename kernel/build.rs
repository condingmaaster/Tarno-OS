// SPDX-License-Identifier: GPL-2.0-or-later
//! Make cargo rebuild the kernel when the linker script changes (it is passed
//! via `-T` in `.cargo/config.toml`, which cargo does not track on its own), and build the lai AML
//! interpreter (third_party/lai, MIT) as a freestanding static library with the kernel's code model.

use std::path::{Path, PathBuf};
use std::process::Command;

fn main() {
    println!("cargo:rerun-if-changed=linker.ld");
    let root = PathBuf::from(std::env::var("CARGO_MANIFEST_DIR").unwrap()).join("..");
    let lai = root.join("third_party/lai");
    println!("cargo:rerun-if-changed={}", lai.display());
    let out = PathBuf::from(std::env::var("OUT_DIR").unwrap());
    let sources = [
        "core/error.c", "core/eval.c", "core/exec-operand.c", "core/exec.c", "core/libc.c", "core/ns.c", "core/object.c",
        "core/opregion.c", "core/os_methods.c", "core/variable.c", "core/vsnprintf.c",
        "helpers/pc-bios.c", "helpers/pci.c", "helpers/resource.c", "helpers/sci.c", "helpers/pm.c",
        "drivers/ec.c", "drivers/timer.c",
    ];
    let mut objs = Vec::new();
    println!("cargo:rerun-if-changed=c/acpi_glue.c");
    let mut all: Vec<(PathBuf, String)> = sources.iter().map(|s| (lai.join(s), s.to_string())).collect();
    all.push((PathBuf::from(std::env::var("CARGO_MANIFEST_DIR").unwrap()).join("c/acpi_glue.c"), "acpi_glue.c".into()));
    for (src, s) in &all {
        let obj = out.join(Path::new(s).file_name().unwrap()).with_extension("o");
        let st = Command::new("gcc")
            .args(["-c", "-O2", "-ffreestanding", "-fno-pic", "-fno-pie", "-mno-red-zone", "-mcmodel=kernel", "-mgeneral-regs-only",
                   "-fno-stack-protector", "-fno-builtin-printf", "-w", "-std=gnu11", "-I"])
            .arg(lai.join("include"))
            .arg("-I").arg(lai.join("core"))
            .arg("-o").arg(&obj)
            .arg(src)
            .status()
            .expect("gcc");
        assert!(st.success(), "compiling {s}");
        objs.push(obj);
    }
    let lib = out.join("liblai.a");
    let _ = std::fs::remove_file(&lib);
    let st = Command::new("ar").arg("crs").arg(&lib).args(&objs).status().expect("ar");
    assert!(st.success());
    println!("cargo:rustc-link-search=native={}", out.display());
    println!("cargo:rustc-link-lib=static=lai");
}
