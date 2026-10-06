Import("env")
import os
import subprocess
import sys

# Enforce production boot policy both before build and after sdkconfig regeneration.
def check_boot_invariants(source=None, target=None, env=env):
    if env.subst("$PIOENV") in ("factory", "rollback-test"):
        subprocess.run([sys.executable, env.subst("$PROJECT_DIR/scripts/check_invariants.py")], check=True)
        if target:
            toolchain = env.PioPlatform().get_package_dir("toolchain-xtensa-esp-elf")
            if not toolchain:
                raise RuntimeError("Cannot locate toolchain for boot stack verification")
            objdump = os.path.join(toolchain, "bin", "xtensa-esp32-elf-objdump" + (".exe" if os.name == "nt" else ""))
            subprocess.run([sys.executable, env.subst("$PROJECT_DIR/scripts/verify_boot_stack.py"),
                            "--objdump", objdump, "--elf", str(target[0])], check=True)

check_boot_invariants()
env.AddPostAction("$BUILD_DIR/${PROGNAME}.elf", check_boot_invariants)

if env.subst("$PIOENV") in ("factory", "test", "rollback-test"):
    subprocess.run([sys.executable, env.subst("$PROJECT_DIR/scripts/verify_partition_layout.py")], check=True)
    def check_storage_budget(source, target, env):
        subprocess.run([sys.executable, env.subst("$PROJECT_DIR/scripts/verify_partition_layout.py"),
                        "--firmware", str(target[0])], check=True)
    env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", check_storage_budget)

# Firmware and npm builds share one deterministic, parser-based asset generator.
subprocess.run(["node", env.subst("$PROJECT_DIR/../lumigrid-lander-main/scripts/build-embedded.mjs")], check=True)
