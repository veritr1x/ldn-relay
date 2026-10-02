"""Release checks must reject tampering, stale builds and incomplete notices."""
from pathlib import Path
import hashlib
import json
import tempfile
import unittest
import zipfile
from release_assets import create_assets


class ReleaseTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.package = self.root / 'package'
        self.package.mkdir()
        self.output = self.root / 'downloads'
        self.commit = 'a' * 40
        self.files = {'ldn-relay-v0.5.0.nro': b'validated test payload',
                      'README.md': b'Install instructions', 'LICENSE': b'Notice',
                      'source-files.json': b'{}'}
        self.info = dict(version='0.5.0', file='ldn-relay-v0.5.0.nro',
                         source_commit=self.commit, source_dirty=False,
                         sha256=hashlib.sha256(self.files['ldn-relay-v0.5.0.nro']).hexdigest())
        self.write_package()

    def write_package(self):
        self.files['build-info.json'] = json.dumps(self.info).encode()
        for name, data in self.files.items():
            (self.package / name).write_bytes(data)
        (self.package / 'SHA256SUMS').write_text(''.join(
            hashlib.sha256(data).hexdigest() + '  ' + name + '\n'
            for name, data in self.files.items()))

    def build(self, tag='v0.5.0'):
        create_assets(self.package, self.output, tag, self.commit)

    def test_downloads_match_verified_package(self):
        self.build()
        with zipfile.ZipFile(self.output / 'LDN-Relay-0.5.0-Switch.zip') as z:
            self.assertEqual(set(z.namelist()), set(self.files) | {'SHA256SUMS'})
            for name, data in self.files.items():
                self.assertEqual(z.read(name), data)
        for line in (self.output / 'SHA256SUMS').read_text().splitlines():
            digest, name = line.split('  ', 1)
            self.assertEqual(hashlib.sha256((self.output / name).read_bytes()).hexdigest(), digest)

    def test_rejects_tampered_binary(self):
        (self.package / 'ldn-relay-v0.5.0.nro').write_bytes(b'changed')
        with self.assertRaisesRegex(ValueError, 'checksum mismatch'):
            self.build()

    def test_rejects_wrong_tag_and_stale_or_dirty_source(self):
        with self.assertRaisesRegex(ValueError, 'tag'):
            self.build('v0.5.1')
        for change in ({'source_commit': 'b' * 40}, {'source_dirty': True}):
            original = self.info.copy()
            self.info.update(change)
            self.write_package()
            with self.assertRaisesRegex(ValueError, 'clean CI commit'):
                self.build()
            self.info = original

    def test_rejects_unlisted_files(self):
        (self.package / 'unexpected.txt').write_text('unreviewed file')
        with self.assertRaisesRegex(ValueError, 'Every package file'):
            self.build()

    def test_rejects_path_escape(self):
        (self.package / 'SHA256SUMS').write_text('0' * 64 + '  ../outside\n')
        with self.assertRaisesRegex(ValueError, 'Unsafe'):
            self.build()

    def test_does_not_overwrite_downloads(self):
        self.build()
        with self.assertRaisesRegex(ValueError, 'never overwritten'):
            self.build()


if __name__ == '__main__':
    unittest.main()
