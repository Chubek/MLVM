"""Exercise dependency bootstrapping without network access or real checkouts."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


SCRIPT = Path(__file__).resolve().parents[2] / "scripts/get-deps.sh"


class GetDepsTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="mlvm-deps-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name) / "project with spaces"
        (self.root / "scripts").mkdir(parents=True)
        self.script = self.root / "scripts/get-deps.sh"
        shutil.copy2(SCRIPT, self.script)
        self.bin = Path(self.temp.name) / "bin"
        self.bin.mkdir()
        self.log = self.bin / "git.log"
        git = self.bin / "git"
        git.write_text("""#!/usr/bin/env bash
printf '%s\\n' "$*" >> "$GIT_LOG"
if [[ "$1" == clone ]]; then
  [[ "${FAIL_CLONE:-0}" == 0 ]] || exit 37
  dest="${!#}"
  mkdir -p "$dest/.git"
  touch "$dest/toml.c" "$dest/toml.h" "$dest/ketopt.h"
fi
""")
        git.chmod(0o755)
        self.env = dict(os.environ, PATH=f"{self.bin}:{os.environ['PATH']}",
                        GIT_LOG=str(self.log))

    def run_script(self, *args, success=True):
        result = subprocess.run(["bash", str(self.script), *args],
                                cwd=self.temp.name, env=self.env,
                                capture_output=True, text=True)
        if success:
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        else:
            self.assertNotEqual(result.returncode, 0)
        return result

    def test_default_location_is_project_root(self):
        self.run_script("tomlc99")
        self.assertTrue((self.root / "third_party/tomlc99/toml.c").is_file())
        self.assertFalse((self.root / "scripts/third_party").exists())

    def test_all_inventory_and_no_verify_process_every_package(self):
        dest = Path(self.temp.name) / "custom deps"
        self.run_script("--dir", str(dest), "--no-verify", "all")
        expected = {"cparser-iso11", "cpp-httplib", "ctl", "dparser", "kaguya",
                    "klib", "libglr", "lua", "metatk", "qamrpp", "replxx",
                    "satie", "sfsexp", "termlib", "tomlc99", "ucpp"}
        self.assertTrue(expected <= {p.name for p in dest.iterdir()})
        self.assertIn("https://github.com/Chubek/metatk.git", self.log.read_text())
        self.assertIn("--recurse-submodules", self.log.read_text())
        self.assertFalse((dest / "domdsl").exists())

    def test_snapshot_is_verified_and_preserved(self):
        dest = self.root / "third_party/klib"
        dest.mkdir(parents=True)
        marker = dest / "ketopt.h"
        marker.write_text("local source snapshot")
        self.run_script("klib")
        self.assertEqual(marker.read_text(), "local source snapshot")
        self.assertFalse(self.log.exists())
        marker.unlink()
        self.assertIn("verification failed", self.run_script("klib", success=False).stderr)

    def test_snapshot_update_never_pulls_enclosing_repository(self):
        (self.root / "third_party/klib").mkdir(parents=True)
        self.run_script("--update", "klib", success=False)
        self.assertFalse(self.log.exists())

    def test_worktree_git_file_and_recursive_update(self):
        dest = self.root / "third_party/klib"
        dest.mkdir(parents=True)
        (dest / ".git").write_text("gitdir: elsewhere")
        (dest / "ketopt.h").touch()
        self.run_script("--update", "klib")
        self.assertIn("pull --ff-only", self.log.read_text())
        self.assertIn("submodule update --init --recursive", self.log.read_text())

    def test_force_replaces_only_selected_snapshot(self):
        dest = self.root / "third_party/klib"
        dest.mkdir(parents=True)
        (dest / "old").touch()
        other = self.root / "third_party/other"
        other.mkdir()
        self.run_script("--force", "klib")
        self.assertFalse((dest / "old").exists())
        self.assertTrue((dest / "ketopt.h").exists())
        self.assertTrue(other.exists())

    def test_invalid_request_has_no_side_effects(self):
        for args in [("all", "unknown"), ("--dir=", "klib"), ("--dir",)]:
            with self.subTest(args=args):
                self.run_script(*args, success=False)
                self.assertFalse(self.log.exists())

    def test_clone_failure_is_propagated(self):
        self.env["FAIL_CLONE"] = "1"
        result = self.run_script("--no-verify", "klib", "tomlc99", success=False)
        self.assertEqual(result.returncode, 37)
        self.assertEqual(len(self.log.read_text().splitlines()), 1)


if __name__ == "__main__":
    unittest.main()
