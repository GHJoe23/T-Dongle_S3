Import("env")
import os
import subprocess

def build_uf2(source, target, env):
    hex_path = env.subst("$BUILD_DIR/${PROGNAME}.hex")
    uf2_path = env.subst("$BUILD_DIR/${PROGNAME}.uf2")
    framework_dir = env.PioPlatform().get_package_dir("framework-arduinoadafruitnrf52-seeed")
    if not framework_dir:
        return
    uf2conv = os.path.join(framework_dir, "tools", "uf2conv", "uf2conv.py")
    if os.path.isfile(uf2conv) and os.path.isfile(hex_path):
        cmd = [env.subst("$PYTHONEXE"), uf2conv, hex_path, "-c", "-f", "0xADA52840", "-o", uf2_path]
        subprocess.run(cmd, check=True)
        print("UF2 created at", uf2_path)

env.AddPostAction("$BUILD_DIR/${PROGNAME}.hex", build_uf2)
