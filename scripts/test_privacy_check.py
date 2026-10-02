import unittest
from privacy_check import findings

class PrivacyGuardTests(unittest.TestCase):
    def test_private_identifiers(self):
        samples = [b'/' + b'Users/' + b'tester/project/main.c',
                   b'C:' + b'\\Users\\' + b'tester\\project',
                   b'00000000' + b'-' + b'1234567890ABCDEF',
                   b'-----BEGIN ' + b'PRIVATE KEY-----']
        for data in samples:
            with self.subTest(data=data):
                self.assertTrue(findings('source.c', data))

    def test_safe_protocol_values_and_reserved_author(self):
        self.assertFalse(findings('source.c', b'7b61238d-028a-4f65-99d8-7c8f22a11d4e author@ldn-relay.invalid'))

    def test_private_artifacts_and_explicit_identifiers(self):
        self.assertTrue(findings('game.sav', b''))
        self.assertTrue(findings('setup.py', b'SAMPLE PERSON', ['sample person']))
        self.assertFalse(findings('README.md', b'veritrix'))

if __name__ == '__main__':
    unittest.main()
