import hashlib
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import release


class ReleaseTest(unittest.TestCase):
    def test_tag_must_match_project(self):
        with patch.dict(os.environ, {"RELEASE_TAG": ""}):
            version = release.project_version()
        with patch.dict(os.environ, {"RELEASE_TAG": f"v{version}"}):
            self.assertEqual(release.project_version(), version)
        for tag in ("v0.0.0-invalid", f"v{version}-rc1", version):
            with self.subTest(tag=tag), patch.dict(os.environ, {"RELEASE_TAG": tag}):
                with self.assertRaises(ValueError):
                    release.project_version()

    def fixtures(self, directory):
        names = [f"obs-quick-paste-1.2.3-{platform}.{extension}" for platform, extension in (
            ("windows-x64", "exe"), ("windows-x64", "zip"),
            ("macos-arm64", "pkg"), ("macos-arm64", "zip"))]
        for name in names:
            (directory / name).write_bytes(b"fixture")
        return names

    def test_complete_set_has_checksums(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            names = self.fixtures(directory)
            release.verify_assets(directory, "1.2.3")
            expected = "".join(f"{hashlib.sha256(b'fixture').hexdigest()}  {name}\n" for name in sorted(names))
            self.assertEqual((directory / "SHA256SUMS.txt").read_text(), expected)

    def test_missing_extra_and_empty_assets_fail(self):
        for failure in ("missing", "extra", "empty"):
            with self.subTest(failure=failure), tempfile.TemporaryDirectory() as temporary:
                directory = Path(temporary)
                names = self.fixtures(directory)
                if failure == "missing":
                    (directory / names[0]).unlink()
                elif failure == "extra":
                    (directory / "unexpected.txt").write_text("unexpected")
                else:
                    (directory / names[0]).write_bytes(b"")
                with self.assertRaises(ValueError):
                    release.verify_assets(directory, "1.2.3")
                self.assertFalse((directory / "SHA256SUMS.txt").exists())


if __name__ == "__main__":
    unittest.main()
